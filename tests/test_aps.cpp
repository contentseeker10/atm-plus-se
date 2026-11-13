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

#include "test_framework.hpp"

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
