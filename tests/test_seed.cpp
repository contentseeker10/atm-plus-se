/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: test_seed.cpp
 * Demo data seeder: contents, idempotency and authentication against seeded cards.
 *
 */

#include "test_framework.hpp"

#include "access_control/auth_service.hpp"
#include "access_control/card.hpp"

#include "accounting/audit.hpp"
#include "accounting/ledger_service.hpp"

#include "accounts/account.hpp"

#include "db.hpp"

#include "security/anti_replay.hpp"

#include "storage/operations.hpp"
#include "storage/seed.hpp"
#include "storage/sqlite_utils.hpp"

#include <sqlite3.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace
{
    bool seed(DatabaseManager& db)
    {
        return db
            .execute_transaction([](sqlite3* raw)
                                 { return storage::seed_demo_data(raw).has_value(); })
            .has_value();
    }

    std::int64_t count_rows(DatabaseManager& db, std::string_view table)
    {
        std::int64_t count = -1;
        const std::string query = "SELECT COUNT(*) FROM " + std::string(table);
        const auto ran = db.execute_transaction(
            [&](sqlite3* raw)
            {
                auto stmt = storage::prepare(raw, query);
                if (!stmt || sqlite3_step(stmt->get()) != SQLITE_ROW) return false;
                count = sqlite3_column_int64(stmt->get(), 0);
                return true;
            });
        return ran ? count : -1;
    }

    std::int64_t balance(DatabaseManager& db, std::string_view account_id)
    {
        auto account = db.load_account(account_id);
        return account ? (*account)->get_balance() : -1;
    }
} // namespace

TEST(seed_populates_all_tables)
{
    begin("seed: accounts, cards and cassettes are inserted");

    DatabaseManager db{":memory:"};
    CHECK(seed(db));

    CHECK(count_rows(db, "accounts") == 6);
    CHECK(count_rows(db, "cards") == 4);
    CHECK(count_rows(db, "vault_cassettes") == 6);
    CHECK(count_rows(db, "ledger_entries") == 10);

    CHECK(balance(db, "CHK-4444") == 154000);
    CHECK(balance(db, "SAV-4444") == 450000);
    CHECK(balance(db, "CHK-7777") == 0);

    auto inventory = db.load_vault_inventory();
    CHECK(inventory.has_value());
    CHECK(inventory->at(Denomination::D_100) == 300);
    CHECK(inventory->at(Denomination::D_500) == 100);

    bool linked = false;
    const auto ran = db.execute_transaction(
        [&linked](sqlite3* raw)
        {
            auto accounts = storage::load_card_accounts(raw, "4111-2222-3333-4444");
            linked = accounts && accounts->checking_id == "CHK-4444" &&
                     accounts->savings_id == "SAV-4444";
            return true;
        });
    CHECK(ran.has_value() && linked);

    done();
}

TEST(seed_is_idempotent_and_keeps_live_state)
{
    begin("seed: second run inserts nothing and does not reset balances");

    DatabaseManager db{":memory:"};
    CHECK(seed(db));

    auto account = db.load_account("CHK-4444");
    CHECK(account.has_value());
    CHECK((*account)->withdraw(20000).has_value());
    CHECK(db.save_account(**account).has_value());
    CHECK(db.set_failed_attempts("4111-2222-3333-4444", 2).has_value());

    CHECK(seed(db));

    CHECK(count_rows(db, "accounts") == 6);
    CHECK(count_rows(db, "cards") == 4);
    CHECK(count_rows(db, "vault_cassettes") == 6);
    CHECK(count_rows(db, "ledger_entries") == 10);
    CHECK(balance(db, "CHK-4444") == 134000);

    auto attempts = db.get_failed_attempts("4111-2222-3333-4444");
    CHECK(attempts.has_value() && *attempts == 2);

    done();
}

TEST(seeded_cards_authenticate)
{
    begin("seed: PIN hashes, roles and blocked flag work with AuthService");

    auto db = std::make_shared<DatabaseManager>(":memory:");
    CHECK(seed(*db));

    auto audit = std::make_shared<AuditLog>(db, "test-hmac-secret");
    AuthService auth{db, audit};

    auto customer = auth.authenticate_card("4111-2222-3333-4444", "1234");
    CHECK(customer.has_value());
    CHECK(customer && customer->role == UserRole::Customer);

    auto wrong = auth.authenticate_card("4111-5555-6666-7777", "0000");
    CHECK(!wrong.has_value() && wrong.error() == AuthError::InvalidPin);

    auto zero = auth.authenticate_card("4111-5555-6666-7777", "4321");
    CHECK(zero.has_value());

    auto blocked = auth.authenticate_card("4111-0000-0000-9999", "9999");
    CHECK(!blocked.has_value() && blocked.error() == AuthError::CardBlocked);

    auto engineer = auth.authenticate_card("9999-8888-7777-0001", "0000");
    CHECK(engineer.has_value());
    CHECK(engineer && engineer->role == UserRole::Engineer);

    done();
}

TEST(seed_passes_audit_and_inv_check)
{
    begin("seed: genesis ledger entries pass AuditLog load and LedgerService inv_check");

    auto db = std::make_shared<DatabaseManager>(":memory:");
    CHECK(seed(*db));

    auto audit = std::make_shared<AuditLog>(db, "bebra");
    CHECK(audit->load_from_database().has_value());
    CHECK(audit->size() == 10);

    const auto chain = audit->verify_integrity_chain();
    CHECK(chain.has_value() && chain->ok);

    LedgerService ledger{audit, std::make_shared<AntiReplayManager>()};
    constexpr std::array<std::string_view, 6> account_ids{
        "CHK-4444", "SAV-4444", "CHK-9999", "SAV-9999", "CHK-7777", "SAV-7777"};

    std::vector<std::unique_ptr<Account>> loaded;
    std::vector<const Account*> ptrs;
    loaded.reserve(account_ids.size());
    ptrs.reserve(account_ids.size());
    for (const std::string_view id : account_ids)
    {
        auto acc = db->load_account(id);
        CHECK(acc.has_value());
        if (acc)
        {
            ptrs.push_back(acc->get());
            loaded.push_back(std::move(*acc));
        }
    }

    const auto inventory = db->load_vault_inventory();
    CHECK(inventory.has_value());
    std::int64_t vault_cash_cents = 0;
    if (inventory)
    {
        for (const auto& [denom, count] : *inventory)
        {
            vault_cash_cents += static_cast<std::int64_t>(denom) * count;
        }
    }
    CHECK(vault_cash_cents == 12500000);

    const auto report = ledger.inv_check(ptrs, vault_cash_cents, 0);
    CHECK(report.passed);
    CHECK(report.failures.empty());
    done();
}
