/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: test_aps.cpp
 * Automatic Payment System tests.
 *
 */

#include "sut.hpp"

#include "db.hpp"

#include "storage/sqlite_utils.hpp"

#include "test_framework.hpp"

#include <sqlite3.h>

using Clock = std::chrono::system_clock;

TEST(aps)
{
    begin("APS FAS and VAS rules");
    Sut sut;

    SavingsAccount equity{"EQUITY", "bank", 1000000, 0.0, 0};
    SavingsAccount sav{"SAV-1", "alice", 0, 0.0, 0};
    CheckingAccount chk{"CHK-1", "alice", 0, 0, 1000000, "SAV-1"};
    SavingsAccount util{"UTIL-1", "power-company", 0, 0.0, 0};

    CHECK(sut.ledger.transfer(equity, chk, 30000, "OPENING", 1).has_value());
    CHECK(sut.ledger.transfer(equity, sav, 100000, "OPENING", 2).has_value());

    ApsRule fas;
    fas.rule_id = "R-FAS";
    fas.payer_account_id = "CHK-1";
    fas.payee_account_id = "SAV-1";
    fas.amount_cents = 5000;
    fas.fixed_amount = true;
    fas.period = std::chrono::seconds{3600};
    CHECK(sut.ledger.register_aps_rule(fas).has_value());

    ApsRule vas;
    vas.rule_id = "R-VAS";
    vas.payer_account_id = "SAV-1";
    vas.payee_account_id = "UTIL-1";
    vas.amount_cents = 100000;
    vas.fixed_amount = false;
    vas.period = std::chrono::seconds{0};
    CHECK(sut.ledger.register_aps_rule(vas).has_value());

    auto resolve = [&chk, &sav, &util](const std::string& id) -> Account*
    {
        if (id == chk.get_account_id()) return &chk;
        if (id == sav.get_account_id()) return &sav;
        if (id == util.get_account_id()) return &util;
        return nullptr;
    };

    const auto now = Clock::now();

    auto r1 = sut.ledger.run_aps(now, resolve);
    CHECK(r1.executed == 2);
    CHECK(r1.failures.empty());
    CHECK(chk.get_balance() == 25000);
    CHECK(sav.get_balance() == 5000);
    CHECK(util.get_balance() == 100000);

    auto r2 = sut.ledger.run_aps(now, resolve);
    CHECK(r2.executed == 1);
    CHECK(sav.get_balance() == 0);
    CHECK(util.get_balance() == 105000);

    const auto later = now + std::chrono::seconds{7200};
    auto r3 = sut.ledger.run_aps(later, resolve);
    CHECK(r3.executed == 2);
    CHECK(r3.failures.empty());
    CHECK(chk.get_balance() == 20000);
    CHECK(sav.get_balance() == 0);
    CHECK(util.get_balance() == 110000);

    auto r4 = sut.ledger.run_aps(later, resolve);
    CHECK(r4.executed == 0);
    CHECK(r4.failures.size() == 1);

    auto report = sut.ledger.inv_check({&chk, &sav, &util}, 0, 0);
    CHECK(report.passed);
    done();
}

TEST(aps_rule_validation)
{
    begin("APS rule validation");
    Sut sut;

    ApsRule bad;
    bad.rule_id = "R1";
    bad.payer_account_id = "A";
    bad.payee_account_id = "A";
    bad.amount_cents = 100;
    CHECK(!sut.ledger.register_aps_rule(bad).has_value());

    ApsRule zero;
    zero.rule_id = "R2";
    zero.payer_account_id = "A";
    zero.payee_account_id = "B";
    zero.amount_cents = 0;
    CHECK(!sut.ledger.register_aps_rule(zero).has_value());
    done();
}

TEST(savings_interest_and_persistent_aps)
{
    begin("Savings basis-point interest accrual and persistent APS execution");

    SavingsAccount sav{"SAV-1", "alice", 100000, 0.025, 0};
    CHECK(sav.get_interest_rate_bps() == 250);
    CHECK(sav.calculate_interest_cents(365) == 2500);

    Sut sut;
    const auto accrued = sut.ledger.accrue_interest(sav, 1, 365);
    CHECK(accrued.has_value() && *accrued == 2500);
    CHECK(sav.get_balance() == 102500);
    CHECK(sut.audit->size() == 1);
    CHECK(sut.audit->entries()[0].operation_type == "INTEREST");
    CHECK(sut.audit->entries()[0].debit_account_id == BANK_TREASURY_ACCOUNT_ID);
    CHECK(sut.audit->entries()[0].credit_account_id == "SAV-1");

    SavingsAccount sav2{"SAV-2", "bob", 200000, 0.05, 0};
    CHECK(sav2.get_interest_rate_bps() == 500);
    const auto direct = sav2.apply_interest(365);
    CHECK(direct.has_value() && *direct == 10000);
    CHECK(sav2.get_balance() == 210000);

    auto db = std::make_shared<DatabaseManager>(":memory:");
    const bool seeded = db->execute_transaction(
                              [](sqlite3* raw)
                              {
                                  return storage::exec(raw, R"sql(
INSERT INTO accounts (account_id, client_id, type, balance_cents, annual_interest_rate, minimum_balance_cents)
VALUES ('SAV-1', 'alice', 'savings', 10000, 0.025, 0);
INSERT INTO accounts (account_id, client_id, type, balance_cents, client_min_cents, overflow_max_cents, linked_savings_id)
VALUES ('CHK-1', 'alice', 'checking', 30000, 0, 1000000, 'SAV-1');
)sql")
                                      .has_value();
                              })
                            .has_value();
    CHECK(seeded);

    auto audit = std::make_shared<AuditLog>(db, "test-hmac-secret");
    auto replay = std::make_shared<AntiReplayManager>();
    LedgerService ledger{audit, replay};

    ApsRule rule;
    rule.rule_id = "R-DB";
    rule.payer_account_id = "CHK-1";
    rule.payee_account_id = "SAV-1";
    rule.amount_cents = 5000;
    rule.fixed_amount = true;
    rule.period = std::chrono::seconds{60};
    CHECK(ledger.register_aps_rule(rule).has_value());

    const auto res = ledger.run_aps(Clock::now(), *db);
    CHECK(res.executed == 1);
    CHECK(res.failures.empty());

    const auto chk_db = db->load_account("CHK-1");
    const auto sav_db = db->load_account("SAV-1");
    CHECK(chk_db.has_value() && (*chk_db)->get_balance() == 25000);
    CHECK(sav_db.has_value() && (*sav_db)->get_balance() == 15000);
    CHECK(audit->size() == 1);

    const auto chain = audit->verify_integrity_chain();
    CHECK(chain.has_value() && chain->ok);
    done();
}
