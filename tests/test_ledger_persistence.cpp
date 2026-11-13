/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: test_ledger_persistence.cpp
 * SQLite persistence and restart recovery tests.
 *
 */

#include "sut.hpp"

#include "db.hpp"
#include "storage/sqlite_utils.hpp"

#include "test_framework.hpp"

#include <sqlite3.h>

#include <cstdio>
#include <memory>
#include <string>

TEST(ledger_persistence)
{
    begin("Ledger persistence");
    const std::string db_path = "atm_test_ledger.sqlite";
    std::remove(db_path.c_str());

    {
        auto db = std::make_shared<DatabaseManager>(db_path);
        auto audit = std::make_shared<AuditLog>(db, "test-hmac-secret");
        auto replay = std::make_shared<AntiReplayManager>();
        LedgerService ledger{audit, replay};

        SavingsAccount sav{"SAV-1", "yosyp", 0, 0.0, 0};
        CheckingAccount chk{"CHK-1", "yosyp", 30000, 0, 1000000, "SAV-1"};
        CHECK(ledger.withdraw_with_overdraft(chk, sav, 25000, 1).has_value());
        CHECK(audit->size() == 1);
        const LedgerEntry mem = audit->entries()[0];

        bool row_ok = false;
        bool hash_ok = false;
        bool count_ok = false;
        const auto q = db->execute_transaction(
            [&](sqlite3* raw)
            {
                auto stmt = storage::prepare(
                    raw, "SELECT tx_id, amount_cents, curr_hash, hmac_signature "
                         "FROM ledger_entries");
                if (!stmt) { return false; }
                int rows = 0;
                while (sqlite3_step(stmt->get()) == SQLITE_ROW)
                {
                    ++rows;
                    row_ok = storage::column_text(stmt->get(), 0) == mem.tx_id &&
                             sqlite3_column_int64(stmt->get(), 1) == mem.amount_cents;
                    hash_ok = storage::column_text(stmt->get(), 2) == mem.curr_hash &&
                              storage::column_text(stmt->get(), 3) == mem.hmac_signature;
                }
                count_ok = (rows == 1);
                return true;
            });
        CHECK(q.has_value());
        CHECK(count_ok);
        CHECK(row_ok);
        CHECK(hash_ok);

        LedgerEntry dup = mem;
        const auto r = audit->append_record(dup);
        CHECK(!r.has_value());
        CHECK(r.error() == CryptoError::DatabaseError);
        CHECK(audit->size() == 1);
    }

    CHECK(std::remove(db_path.c_str()) == 0);
    done();
}

TEST(ledger_restart_recovery)
{
    begin("Ledger recovery after shutdown");
    const std::string db_path = "atm_test_ledger_restart.sqlite";
    std::remove(db_path.c_str());

    std::string last_hash;
    const std::size_t n_ops = 2;

    {
        auto db = std::make_shared<DatabaseManager>(db_path);
        auto audit = std::make_shared<AuditLog>(db, "test-hmac-secret");
        auto replay = std::make_shared<AntiReplayManager>();
        LedgerService ledger{audit, replay};

        SavingsAccount sav{"SAV-R", "yosyp", 0, 0.0, 0};
        CheckingAccount chk{"CHK-R", "yosyp", 30000, 0, 1000000, "SAV-R"};
        CHECK(ledger.withdraw_with_overdraft(chk, sav, 25000, 1).has_value());
        CHECK(ledger.deposit_with_overflow(chk, sav, 5000, 2).has_value());
        CHECK(audit->size() == n_ops);
        last_hash = *audit->get_last_hash();
    }

    {
        auto db = std::make_shared<DatabaseManager>(db_path);
        auto audit = std::make_shared<AuditLog>(db, "test-hmac-secret");
        auto replay = std::make_shared<AntiReplayManager>();
        LedgerService ledger{audit, replay};

        const auto load = audit->load_from_database();
        CHECK(load.has_value());
        CHECK(audit->size() == n_ops);
        CHECK(*audit->get_last_hash() == last_hash);
        CHECK(audit->verify_integrity_chain()->ok);

        SavingsAccount sav{"SAV-R", "yosyp", 0, 0.0, 0};
        CheckingAccount chk{"CHK-R", "yosyp", 10000, 0, 1000000, "SAV-R"};
        CHECK(ledger.withdraw_with_overdraft(chk, sav, 4000, 3).has_value());
        CHECK(audit->size() == n_ops + 1);
        CHECK(audit->entries()[n_ops].prev_hash == last_hash);
        CHECK(audit->verify_integrity_chain()->ok);

        CHECK(!audit->load_from_database().has_value());
    }

    {
        auto db = std::make_shared<DatabaseManager>(db_path);
        const auto t = db->execute_transaction(
            [](sqlite3* raw)
            {
                return storage::exec(
                           raw,
                           "UPDATE ledger_entries SET amount_cents = amount_cents + 1 "
                           "WHERE tx_id = (SELECT tx_id FROM ledger_entries LIMIT 1)")
                    .has_value();
            });
        CHECK(t.has_value());

        auto audit = std::make_shared<AuditLog>(db, "test-hmac-secret");
        const auto r = audit->load_from_database();
        CHECK(!r.has_value());
        CHECK(r.error() == CryptoError::HashMismatch);
        CHECK(audit->size() == 0);
    }

    CHECK(std::remove(db_path.c_str()) == 0);
    done();
}
