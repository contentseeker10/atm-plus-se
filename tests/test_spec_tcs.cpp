/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: test_spec_tcs.cpp
 * Official acceptance scenarios TC-01 to TC-07.
 *
 */

#include "sut.hpp"

#include "test_framework.hpp"

#include "access_control/atm_controller.hpp"

#include "accounting/audit.hpp"

#include "cash/cash_vault.hpp"

#include "crypto.hpp"
#include "db.hpp"

#include "storage/operations.hpp"
#include "storage/sqlite_utils.hpp"

#include <chrono>
#include <cstdlib>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <string_view>

TEST(brute_force_pin_blocks_card)
{
    begin("TC-01");

    constexpr std::string_view card_number = "4000000000000001";
    constexpr std::string_view correct_pin = "1234";
    constexpr std::string_view salt = "salt123";
    const std::string pin_hash = crypto::sha256_hex(correct_pin, salt);

    auto db = std::make_shared<DatabaseManager>(":memory:");
    const std::string sql = std::format(R"sql(
INSERT INTO accounts (account_id, client_id, type, balance_cents, client_min_cents, overflow_max_cents)
VALUES ('CHK-1', 'ivan', 'checking', 50000, 0, 1000000);

INSERT INTO cards (card_number, card_holder, pin_hash, salt, role, is_blocked, is_expired, failed_attempts, checking_account_id)
VALUES ('{0}', 'ivan', '{1}', '{2}', 0, 0, 0, 0, 'CHK-1');
)sql",
                                        card_number, pin_hash, salt);

    const bool seeded =
        db->execute_transaction(
              [&sql](sqlite3* raw) { return storage::exec(raw, sql.c_str()).has_value(); })
            .has_value();
    CHECK(seeded);

    auto audit = std::make_shared<AuditLog>(db, "test-hmac-secret");
    AuthService direct_auth{db, audit};
    AtmController atm{std::make_unique<CashVault>(std::map<Denomination, Cassette>{}), db, audit,
                      std::make_unique<AuthService>(db, audit)};

    const auto inserted = atm.process_insert_card(card_number);
    CHECK(inserted.has_value());
    CHECK(atm.get_current_state_name() == "CardInserted");
    CHECK(atm.get_active_session_id().has_value());

    const auto attempt1 = atm.process_enter_pin("0000");
    CHECK(!attempt1.has_value());
    CHECK(attempt1.error() == AtmError::InvalidPin);
    CHECK(atm.get_current_state_name() == "CardInserted");
    CHECK(atm.get_active_session_id().has_value());
    const auto fails1 = db->get_failed_attempts(card_number);
    CHECK(fails1.has_value() && *fails1 == 1);

    const auto attempt2 = atm.process_enter_pin("1111");
    CHECK(!attempt2.has_value());
    CHECK(attempt2.error() == AtmError::InvalidPin);
    CHECK(atm.get_current_state_name() == "CardInserted");
    CHECK(atm.get_active_session_id().has_value());
    const auto fails2 = db->get_failed_attempts(card_number);
    CHECK(fails2.has_value() && *fails2 == 2);

    const auto attempt3 = atm.process_enter_pin("9999");
    CHECK(!attempt3.has_value());
    CHECK(attempt3.error() == AtmError::MaxAttemptsExceeded);
    CHECK(atm.get_current_state_name() == "Idle");
    CHECK(!atm.get_active_session_id().has_value());

    const auto blocked_record = db->load_card(card_number, false);
    CHECK(blocked_record.has_value());
    CHECK(blocked_record->is_blocked);
    CHECK(blocked_record->failed_attempts == 3);

    const auto reinsert = atm.process_insert_card(card_number);
    CHECK(!reinsert.has_value());
    CHECK(reinsert.error() == AtmError::CardBlocked);

    const auto retry_auth = direct_auth.authenticate_card(card_number, correct_pin);
    CHECK(!retry_auth.has_value());
    CHECK(retry_auth.error() == AuthError::CardBlocked);

    CHECK(audit->size() == 1);
    const auto entries = audit->entries();
    CHECK(entries.size() == 1);
    CHECK(entries[0].operation_type == "CARD_BLOCKED");
    const auto integrity = audit->verify_integrity_chain();
    CHECK(integrity.has_value() && integrity->ok);

    done();
}

TEST(balanced_funds_on_withdrawal)
{
    begin("TC-02");
    Sut sut;

    SavingsAccount sav{"SAV-1", "yosyp", 0, 0.0, 0};
    CheckingAccount chk{"CHK-1", "yosyp", 30000, 0, 1000000, "SAV-1"};

    auto r = sut.ledger.withdraw_with_overdraft(chk, sav, 25000, 1);
    CHECK(r.has_value());
    CHECK(chk.get_balance() == 5000);
    CHECK(sut.audit->size() == 1);

    const auto log = sut.audit->entries();
    CHECK(log[0].debit_account_id == chk.get_account_id());
    CHECK(log[0].credit_account_id == ATM_VAULT_ACCOUNT_ID);
    CHECK(log[0].amount_cents == 25000);
    CHECK(log[0].operation_type == "WITHDRAWAL");
    CHECK(log[0].is_balanced());
    CHECK(log[0].nonce == 1);

    auto chain = sut.audit->verify_integrity_chain();
    CHECK(chain.has_value() && chain->ok);
    done();
}

TEST(ledger_untouched_after_rejection)
{
    begin("TC-03");
    Sut sut;

    SavingsAccount sav{"SAV-1", "yosyp", 0, 0.0, 0};
    CheckingAccount chk{"CHK-1", "yosyp", 30000, 0, 1000000, "SAV-1"};

    CHECK(sut.ledger.transfer(chk, sav, 10000, "TRANSFER", 1).has_value());
    const auto head_before = sut.audit->get_last_hash();
    const auto size_before = sut.audit->size();
    const auto chk_before = chk.get_balance();
    const auto sav_before = sav.get_balance();

    auto r1 = sut.ledger.withdraw_with_overdraft(chk, sav, 70000, 2);
    CHECK(!r1.has_value());
    CHECK(r1.error() == LedgerError::InsufficientFunds);

    auto r2 = sut.ledger.withdraw_with_overdraft(chk, sav, 0, 3);
    CHECK(!r2.has_value());
    CHECK(r2.error() == LedgerError::InvalidAmount);

    CHECK(chk.get_balance() == chk_before);
    CHECK(sav.get_balance() == sav_before);
    CHECK(sut.audit->size() == size_before);
    CHECK(sut.audit->get_last_hash().get() == head_before.get());

    auto chain = sut.audit->verify_integrity_chain();
    CHECK(chain.has_value() && chain->ok);
    done();
}

TEST(overdraft_protection)
{
    begin("TC-04");
    Sut sut;

    SavingsAccount sav{"SAV-1", "yosyp", 50000, 0.0, 0};
    CheckingAccount chk{"CHK-1", "yosyp", 15000, 10000, 1000000, "SAV-1"};

    auto r = sut.ledger.withdraw_with_overdraft(chk, sav, 8000, 1);
    CHECK(r.has_value());
    CHECK(chk.get_balance() == 10000);
    CHECK(sav.get_balance() == 47000);
    CHECK(sut.audit->size() == 2);

    auto log = sut.audit->entries();
    CHECK(log[0].operation_type == "OVERDRAFT_COVERAGE");
    CHECK(log[0].amount_cents == 3000);
    CHECK(log[1].operation_type == "WITHDRAWAL");
    CHECK(log[1].amount_cents == 8000);

    auto r2 = sut.ledger.withdraw_with_overdraft(chk, sav, 20000, 2);
    CHECK(r2.has_value());
    CHECK(chk.get_balance() == 10000);
    CHECK(sav.get_balance() == 27000);
    CHECK(sut.audit->size() == 4);

    log = sut.audit->entries();
    CHECK(log[2].operation_type == "OVERDRAFT_COVERAGE");
    CHECK(log[2].amount_cents == 20000);
    CHECK(log[3].operation_type == "WITHDRAWAL");
    CHECK(log[3].amount_cents == 20000);
    done();
}

TEST(overdraft_insufficient_coverage)
{
    begin("TC-04");
    Sut sut;

    SavingsAccount sav{"SAV-1", "yosyp", 1000, 0.0, 500};
    CheckingAccount chk{"CHK-1", "yosyp", 15000, 10000, 1000000, "SAV-1"};

    auto r = sut.ledger.withdraw_with_overdraft(chk, sav, 8000, 1);
    CHECK(!r.has_value());
    CHECK(r.error() == LedgerError::NoOverdraftCoverage);
    CHECK(chk.get_balance() == 15000);
    CHECK(sav.get_balance() == 1000);
    CHECK(sut.audit->size() == 0);
    done();
}

TEST(overflow_sweep)
{
    begin("TC-05");
    Sut sut;

    SavingsAccount sav{"SAV-1", "yosyp", 20000, 0.0, 0};
    CheckingAccount chk{"CHK-1", "yosyp", 80000, 0, 100000, "SAV-1"};

    auto r = sut.ledger.deposit_with_overflow(chk, sav, 50000, 1);
    CHECK(r.has_value());
    CHECK(chk.get_balance() == 100000);
    CHECK(sav.get_balance() == 50000);
    CHECK(sut.audit->size() == 2);

    auto log = sut.audit->entries();
    CHECK(log[0].operation_type == "DEPOSIT");
    CHECK(log[1].operation_type == "OVERFLOW_SWEEP");
    CHECK(log[1].amount_cents == 30000);
    done();
}

TEST(hash_chain_and_invcheck)
{
    begin("TC-06");
    Sut sut;

    SavingsAccount equity{"EQUITY", "bank", 1000000, 0.0, 0};
    SavingsAccount sav{"SAV-1", "yosyp", 0, 0.0, 0};
    CheckingAccount chk{"CHK-1", "yosyp", 0, 0, 100000, "SAV-1"};

    CHECK(sut.ledger.transfer(equity, chk, 80000, "OPENING", 1).has_value());
    CHECK(sut.ledger.transfer(equity, sav, 20000, "OPENING", 2).has_value());
    CHECK(sut.ledger.deposit_with_overflow(chk, sav, 50000, 3).has_value());
    CHECK(sut.ledger.withdraw_with_overdraft(chk, sav, 20000, 4).has_value());

    const std::int64_t vault_cash = 30000;

    auto report = sut.ledger.inv_check({&chk, &sav}, vault_cash, 0);
    CHECK(report.passed);
    CHECK(report.failures.empty());

    sut.audit->debug_tamper_amount(0, 12345);

    auto chain = sut.audit->verify_integrity_chain();
    CHECK(chain.has_value());
    CHECK(!chain->ok);
    CHECK(chain->first_broken_index == 0);

    report = sut.ledger.inv_check({&chk, &sav}, vault_cash, 0);
    CHECK(!report.passed);

    auto blocked = sut.ledger.withdraw_with_overdraft(chk, sav, 1000, 5);
    CHECK(!blocked.has_value());
    CHECK(blocked.error() == LedgerError::IntegrityCompromised);
    done();
}

TEST(anti_replay)
{
    begin("TC-07");
    Sut sut;

    SavingsAccount sav{"SAV-1", "yosyp", 0, 0.0, 0};
    CheckingAccount chk{"CHK-1", "yosyp", 50000, 0, 1000000, "SAV-1"};

    CHECK(sut.ledger.transfer(chk, sav, 10000, "TRANSFER", 42).has_value());
    const auto chk_after = chk.get_balance();
    const auto sav_after = sav.get_balance();

    auto replay = sut.ledger.transfer(chk, sav, 10000, "TRANSFER", 42);
    CHECK(!replay.has_value());
    CHECK(replay.error() == LedgerError::ReplayDetected);
    CHECK(chk.get_balance() == chk_after);
    CHECK(sav.get_balance() == sav_after);
    CHECK(sut.audit->size() == 1);

    sut.replay->register_session("sess-1", 5);
    CHECK(!sut.replay->validate_and_consume("sess-1", 3));
    CHECK(!sut.replay->validate_and_consume("sess-1", 5));
    CHECK(sut.replay->validate_and_consume("sess-1", 6));

    CHECK(sut.ledger.transfer(chk, sav, 5000, "TRANSFER", 43, "req-uuid-1").has_value());
    auto idemp_replay = sut.ledger.transfer(chk, sav, 5000, "TRANSFER", 44, "req-uuid-1");
    CHECK(!idemp_replay.has_value());
    CHECK(idemp_replay.error() == LedgerError::ReplayDetected);
    CHECK(chk.get_balance() == chk_after - 5000);
    CHECK(sav.get_balance() == sav_after + 5000);
    CHECK(sut.audit->size() == 2);
    done();
}

TEST(security_hardening)
{
    begin("Security hardening: secure_zero, env engineer key override and audit tamper check");

    std::string secret = "super-sensitive-pin-1234";
    const std::size_t secret_len = secret.size();
    crypto::secure_zero(secret);
    for (std::size_t i = 0; i < secret_len; ++i)
    {
        CHECK(secret[i] == '\0');
    }

    auto db = std::make_shared<DatabaseManager>(":memory:");
    auto audit = std::make_shared<AuditLog>(db, "test-hmac-secret");
    auto replay = std::make_shared<AntiReplayManager>();
    auto ledger = std::make_shared<LedgerService>(audit, replay);
    AtmController atm{
        std::make_unique<CashVault>(std::map<Denomination, Cassette>{}),
        db,
        audit,
        std::make_unique<AuthService>(db, audit),
        ledger
    };

#ifdef _WIN32
    _putenv_s("ATM_ENGINEER_MASTER_KEY", "");
#else
    unsetenv("ATM_ENGINEER_MASTER_KEY");
#endif
    CHECK(atm.verify_engineer_key("ENG-MASTER-KEY-2026"));
    CHECK(!atm.verify_engineer_key("WRONG-KEY"));

#ifdef _WIN32
    _putenv_s("ATM_ENGINEER_MASTER_KEY", "CUSTOM-ENG-KEY-9999");
#else
    setenv("ATM_ENGINEER_MASTER_KEY", "CUSTOM-ENG-KEY-9999", 1);
#endif
    CHECK(atm.verify_engineer_key("CUSTOM-ENG-KEY-9999"));
    CHECK(!atm.verify_engineer_key("ENG-MASTER-KEY-2026"));

#ifdef _WIN32
    _putenv_s("ATM_ENGINEER_MASTER_KEY", "");
#else
    unsetenv("ATM_ENGINEER_MASTER_KEY");
#endif
    CHECK(atm.verify_engineer_key("ENG-MASTER-KEY-2026"));

    Sut sut;
    SavingsAccount sav{"SAV-1", "yosyp", 0, 0.0, 0};
    CheckingAccount chk{"CHK-1", "yosyp", 50000, 0, 1000000, "SAV-1"};
    CHECK(sut.ledger.transfer(chk, sav, 10000, "TRANSFER", 1).has_value());

    const auto valid_chain = sut.audit->verify_integrity_chain();
    CHECK(valid_chain.has_value() && valid_chain->ok);

    sut.audit->debug_tamper_amount(0, 999999);
    const auto broken_chain = sut.audit->verify_integrity_chain();
    CHECK(broken_chain.has_value() && !broken_chain->ok);
    CHECK(broken_chain->first_broken_index == 0);

    done();
}

TEST(performance_optimizations)
{
    begin("Performance optimizations: anti-replay auto-purge, audit pagination and sqlite stmt cache");

    AntiReplayManager replay;
    replay.register_session("sess-perf", 10);
    CHECK(!replay.validate_and_consume("sess-perf", 10));
    CHECK(replay.validate_and_consume("sess-perf", 11));
    replay.reset_session("sess-perf");
    CHECK(!replay.validate_and_consume("sess-perf", 12));

    CHECK(!replay.is_request_unique(""));
    for (std::size_t i = 0; i <= AntiReplayManager::kAutoPurgeThreshold + 16; ++i)
    {
        CHECK(replay.is_request_unique(std::format("k-{}", i)));
    }
    CHECK(!replay.is_request_unique("k-0"));
    replay.purge_expired_keys(std::chrono::seconds{-1});
    CHECK(replay.is_request_unique("k-0"));

    Sut sut;
    SavingsAccount sav{"SAV-1", "yosyp", 0, 0.0, 0};
    CheckingAccount chk{"CHK-1", "yosyp", 50000, 0, 1000000, "SAV-1"};
    for (int i = 1; i <= 5; ++i)
    {
        CHECK(sut.ledger.transfer(chk, sav, i * 1000, "TRANSFER", static_cast<std::uint64_t>(i)).has_value());
    }

    const auto page1 = sut.audit->entries_page(1, 2);
    CHECK(page1.size() == 2);
    if (page1.size() == 2)
    {
        CHECK(page1[0].amount_cents == 2000);
        CHECK(page1[1].amount_cents == 3000);
    }

    const auto tail_page = sut.audit->entries_page(4, 10);
    CHECK(tail_page.size() == 1);
    if (!tail_page.empty())
    {
        CHECK(tail_page[0].amount_cents == 5000);
    }
    CHECK(sut.audit->entries_page(10, 5).empty());

    std::int64_t total_amount = 0;
    std::size_t visited = 0;
    sut.audit->for_each_entry([&](const LedgerEntry& e)
    {
        total_amount += e.amount_cents;
        ++visited;
    });
    CHECK(visited == 5);
    CHECK(total_amount == 15000);

    const auto verified = sut.audit->verify_integrity_chain();
    CHECK(verified.has_value() && verified->ok);
    CHECK(verified->checked_records == 5);

    auto db = std::make_shared<DatabaseManager>(":memory:");
    const bool seeded =
        db->execute_transaction(
              [](sqlite3* raw)
              {
                  return storage::exec(raw, R"sql(
INSERT INTO accounts (account_id, client_id, type, balance_cents, client_min_cents, overflow_max_cents)
VALUES ('CHK-CACHE', 'yosyp', 'checking', 10000, 0, 100000);
INSERT INTO cards (card_number, card_holder, pin_hash, salt, role, checking_account_id)
VALUES ('4000000000000099', 'yosyp', 'hash', 'salt', 0, 'CHK-CACHE');
)sql")
                      .has_value();
              })
            .has_value();
    CHECK(seeded);

    for (int i = 0; i < 25; ++i)
    {
        const std::string acc_id = "CHK-CACHE";
        const std::string card_id = "4000000000000099";
        auto acc = db->load_account(acc_id);
        CHECK(acc.has_value() && (*acc)->get_balance() == 10000);
        CHECK(db->set_failed_attempts(card_id, i % 3).has_value());
        auto attempts = db->get_failed_attempts(card_id);
        CHECK(attempts.has_value() && *attempts == (i % 3));
    }
    db.reset();

    done();
}

TEST(controller_qa_fixes_and_service_ops)
{
    begin("Controller QA fixes: anti-replay, transfer protection, APS, tamper lock and service ops");

    constexpr std::string_view card1 = "4111000000000001";
    constexpr std::string_view card2 = "4111000000000002";
    constexpr std::string_view eng_card = "9999000000000001";

    const std::string hash1 = crypto::sha256_hex("1234", "s1");
    const std::string hash2 = crypto::sha256_hex("4321", "s2");
    const std::string eng_hash = crypto::sha256_hex("0000", "se");

    auto db = std::make_shared<DatabaseManager>(":memory:");
    const std::string sql = std::format(R"sql(
INSERT INTO accounts (account_id, client_id, type, balance_cents, annual_interest_rate, minimum_balance_cents)
VALUES ('SAV-A', 'c1', 'savings', 30000, 0.0, 0),
       ('SAV-B', 'c2', 'savings', 20000, 0.0, 0);

INSERT INTO accounts (account_id, client_id, type, balance_cents, client_min_cents, overflow_max_cents, linked_savings_id)
VALUES ('CHK-A', 'c1', 'checking', 50000, 10000, 100000, 'SAV-A'),
       ('CHK-B', 'c2', 'checking', 80000, 0, 100000, 'SAV-B');

INSERT INTO cards (card_number, card_holder, pin_hash, salt, role, is_blocked, is_expired, failed_attempts, checking_account_id)
VALUES ('{0}', 'Client One', '{1}', 's1', 0, 0, 0, 0, 'CHK-A'),
       ('{2}', 'Client Two', '{3}', 's2', 0, 0, 0, 0, 'CHK-B'),
       ('{4}', 'Engineer', '{5}', 'se', 1, 0, 0, 0, NULL);

INSERT INTO vault_cassettes (denomination_cents, bill_count, capacity)
VALUES (10000, 10, 100), (5000, 10, 100);
)sql",
                                        card1, hash1, card2, hash2, eng_card, eng_hash);

    const bool seeded = db->execute_transaction([&sql](sqlite3* raw)
                                                { return storage::exec(raw, sql.c_str()).has_value(); })
                            .has_value();
    CHECK(seeded);

    std::map<Denomination, Cassette> cassettes;
    const bool loaded = db->execute_transaction(
                              [&cassettes](sqlite3* raw)
                              {
                                  auto c = storage::load_cassettes(raw);
                                  if (!c) return false;
                                  cassettes = std::move(*c);
                                  return true;
                              })
                            .has_value();
    CHECK(loaded);

    auto audit = std::make_shared<AuditLog>(db, "test-hmac-secret");
    auto replay = std::make_shared<AntiReplayManager>();
    auto ledger = std::make_shared<LedgerService>(audit, replay);
    AtmController atm{std::make_unique<CashVault>(std::move(cassettes)), db, audit,
                      std::make_unique<AuthService>(db, audit), ledger, replay};

    CHECK(atm.process_insert_card(card1).has_value());
    CHECK(atm.process_enter_pin("1234").has_value());

    const auto wd1 = atm.process_withdraw(10000, "Checking", "wd-key-1", 1);
    CHECK(wd1.has_value());
    CHECK(wd1 && wd1->accounts.checking_balance_cents == 40000);
    CHECK(atm.get_vault().get_cassettes().at(Denomination::D_100).get_count() == 9);

    const auto wd_dup_key = atm.process_withdraw(10000, "Checking", "wd-key-1", 2);
    CHECK(!wd_dup_key.has_value());
    CHECK(wd_dup_key.error() == AtmError::ReplayAttackDetected);
    CHECK((*db->load_account("CHK-A"))->get_balance() == 40000);
    CHECK(atm.get_vault().get_cassettes().at(Denomination::D_100).get_count() == 9);

    const auto wd_stale_nonce = atm.process_withdraw(10000, "Checking", "wd-key-2", 1);
    CHECK(!wd_stale_nonce.has_value());
    CHECK(wd_stale_nonce.error() == AtmError::ReplayAttackDetected);
    CHECK((*db->load_account("CHK-A"))->get_balance() == 40000);

    const auto dep1 = atm.process_deposit({Denomination::D_100}, "Checking", "dep-key-1", 2);
    CHECK(dep1.has_value());
    CHECK(dep1 && dep1->accounts.checking_balance_cents == 50000);
    CHECK(atm.get_vault().get_cassettes().at(Denomination::D_100).get_count() == 10);

    const auto dep_dup_key = atm.process_deposit({Denomination::D_100}, "Checking", "dep-key-1", 3);
    CHECK(!dep_dup_key.has_value());
    CHECK(dep_dup_key.error() == AtmError::ReplayAttackDetected);
    CHECK((*db->load_account("CHK-A"))->get_balance() == 50000);
    CHECK(atm.get_vault().get_cassettes().at(Denomination::D_100).get_count() == 10);

    const auto dep_stale_nonce = atm.process_deposit({Denomination::D_100}, "Checking", "dep-key-2", 2);
    CHECK(!dep_stale_nonce.has_value());
    CHECK(dep_stale_nonce.error() == AtmError::ReplayAttackDetected);
    CHECK((*db->load_account("CHK-A"))->get_balance() == 50000);

    const auto wd2 = atm.process_withdraw(10000, "Checking", "wd-key-3", 3);
    CHECK(wd2.has_value());
    CHECK(wd2 && wd2->accounts.checking_balance_cents == 40000);

    const auto tr = atm.process_transfer("Checking", card2, 35000, "tr-key-1", 4);
    CHECK(tr.has_value());
    if (tr)
    {
        CHECK(tr->overdraft_covered_cents == 5000);
        CHECK(tr->overflow_swept_cents == 15000);
        CHECK(tr->accounts.checking_balance_cents == 10000);
        CHECK(tr->accounts.savings_balance_cents == 25000);
    }
    CHECK((*db->load_account("CHK-B"))->get_balance() == 100000);
    CHECK((*db->load_account("SAV-B"))->get_balance() == 35000);

    const auto rule = atm.create_customer_aps_rule("Savings", "Checking", 5000, true, 3600);
    CHECK(rule.has_value());
    const auto rules_list = atm.get_customer_aps_rules();
    CHECK(rules_list.has_value() && rules_list->size() == 1);

    const auto aps_run = atm.run_customer_aps();
    CHECK(aps_run.has_value());
    if (aps_run)
    {
        CHECK(aps_run->run.executed == 1);
        CHECK(aps_run->accounts.savings_balance_cents == 20000);
        CHECK(aps_run->accounts.checking_balance_cents == 15000);
    }

    if (rule)
    {
        CHECK(atm.delete_customer_aps_rule(rule->rule_id).has_value());
    }
    const auto rules_after = atm.get_customer_aps_rules();
    CHECK(rules_after.has_value() && rules_after->empty());

    CHECK(atm.process_eject_card().has_value());

    CHECK(atm.process_insert_card(card2).has_value());
    CHECK(atm.process_enter_pin("9999").error() == AtmError::InvalidPin);
    CHECK(atm.process_enter_pin("9999").error() == AtmError::InvalidPin);
    CHECK(atm.process_enter_pin("9999").error() == AtmError::MaxAttemptsExceeded);
    CHECK(atm.process_insert_card(card2).error() == AtmError::CardBlocked);

    CHECK(atm.process_insert_card(eng_card).has_value());
    CHECK(atm.process_enter_pin("0000").has_value());
    CHECK(atm.process_replace_cassette(Denomination::D_100, 0).error() == AtmError::VaultAccessDenied);
    CHECK(atm.process_open_vault("ENG-MASTER-KEY-2026").has_value());
    CHECK(atm.process_replace_cassette(Denomination::D_100, 0).has_value());
    CHECK(atm.get_vault().get_cassettes().at(Denomination::D_100).get_count() == 0);
    CHECK(atm.process_replace_cassette(Denomination::D_100, 10).has_value());
    CHECK(atm.get_vault().get_cassettes().at(Denomination::D_100).get_count() == 10);
    CHECK(atm.close_vault_door().has_value());

    CHECK(atm.process_unlock_card(card2).has_value());
    CHECK(atm.simulate_audit_tamper(false).has_value());

    const auto entries_tampered = atm.get_audit_entries();
    CHECK(entries_tampered.has_value() && !entries_tampered->empty());
    if (entries_tampered && !entries_tampered->empty())
    {
        CHECK(atm.is_audit_entry_tampered(entries_tampered->back()));
    }

    const auto broken_verify = atm.verify_audit_integrity();
    CHECK(broken_verify.has_value() && !broken_verify->ok);
    CHECK(atm.process_eject_card().has_value());

    CHECK(atm.process_insert_card(card2).has_value());
    CHECK(atm.process_enter_pin("4321").has_value());
    const auto blocked_wd = atm.process_withdraw(10000);
    CHECK(!blocked_wd.has_value());
    CHECK(blocked_wd.error() == AtmError::CryptoVerificationFailed);
    CHECK(atm.process_eject_card().has_value());

    CHECK(atm.process_insert_card(eng_card).has_value());
    CHECK(atm.process_enter_pin("0000").has_value());
    CHECK(atm.simulate_audit_tamper(true).has_value());
    const auto restored_verify = atm.verify_audit_integrity();
    CHECK(restored_verify.has_value() && restored_verify->ok);
    CHECK(atm.process_eject_card().has_value());

    CHECK(atm.process_insert_card(card2).has_value());
    CHECK(atm.process_enter_pin("4321").has_value());
    const auto wd_after_restore = atm.process_withdraw(10000);
    CHECK(wd_after_restore.has_value());
    CHECK(wd_after_restore && wd_after_restore->accounts.checking_balance_cents == 90000);

    done();
}
