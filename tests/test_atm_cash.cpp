/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: test_atm_cash.cpp
 * End-to-end cash withdrawal, deposit and replenishment through AtmController.
 *
 */

#include "test_framework.hpp"

#include "access_control/atm_controller.hpp"

#include "accounting/audit.hpp"
#include "accounting/ledger_service.hpp"

#include "cash/cash_vault.hpp"

#include "crypto.hpp"

#include "storage/operations.hpp"
#include "storage/sqlite_utils.hpp"

#include <format>
#include <map>
#include <memory>
#include <string>

namespace
{
    constexpr std::string_view kCustomerCard = "4000000000000001";
    constexpr std::string_view kEngineerCard = "9000000000000001";

    struct AccountSeed
    {
        std::int64_t checking_cents{};
        std::int64_t client_min_cents{};
        std::int64_t overflow_max_cents{1000000};
        std::int64_t savings_cents{};
    };

    std::string seed_sql(const AccountSeed& seed, std::string_view cassettes)
    {
        return std::format(R"sql(
INSERT INTO accounts (account_id, client_id, type, balance_cents,
                      annual_interest_rate, minimum_balance_cents)
VALUES ('SAV-1', 'yosyp', 'savings', {3}, 0.0, 0);

INSERT INTO accounts (account_id, client_id, type, balance_cents,
                      client_min_cents, overflow_max_cents, linked_savings_id)
VALUES ('CHK-1', 'yosyp', 'checking', {0}, {1}, {2}, 'SAV-1');

INSERT INTO cards (card_number, card_holder, pin_hash, salt, role, checking_account_id)
VALUES ('{5}', 'yosyp', 'hash', 'salt', 1, 'CHK-1'),
       ('{6}', 'engineer', 'hash', 'salt', 2, NULL);

INSERT INTO vault_cassettes (denomination_cents, bill_count, capacity) VALUES {4};
)sql",
                           seed.checking_cents, seed.client_min_cents,
                           seed.overflow_max_cents, seed.savings_cents, cassettes,
                           kCustomerCard, kEngineerCard);
    }

    struct AtmFixture
    {
        std::shared_ptr<DatabaseManager> db;
        std::shared_ptr<AuditLog> audit;
        std::shared_ptr<LedgerService> ledger;
        std::unique_ptr<AtmController> atm;

        AtmFixture(const AccountSeed& seed, std::string_view cassettes)
            : db{std::make_shared<DatabaseManager>(":memory:")},
              audit{std::make_shared<AuditLog>(db, "test-hmac-secret")},
              ledger{std::make_shared<LedgerService>(
                  audit, std::make_shared<AntiReplayManager>())}
        {
            const std::string sql = seed_sql(seed, cassettes);
            const bool seeded =
                db->execute_transaction(
                      [&sql](sqlite3* raw)
                      { return storage::exec(raw, sql.c_str()).has_value(); })
                    .has_value();
            CHECK(seeded);

            std::map<Denomination, Cassette> cassette_map;
            const bool loaded = db->execute_transaction(
                                      [&cassette_map](sqlite3* raw)
                                      {
                                          auto c = storage::load_cassettes(raw);
                                          if (!c) return false;
                                          cassette_map = std::move(*c);
                                          return true;
                                      })
                                    .has_value();
            CHECK(loaded);

            atm = std::make_unique<AtmController>(
                std::make_unique<CashVault>(std::move(cassette_map)), db, audit,
                std::make_unique<AuthService>(db, audit), ledger);
        }

        void login(std::string_view card, UserRole role)
        {
            atm->end_session();
            CardRecord record{std::string(card), false, false};
            record.role = role;
            atm->start_session(record);
        }

        std::int64_t balance(std::string_view account_id)
        {
            auto account = db->load_account(account_id);
            return account ? (*account)->get_balance() : -1;
        }

        int stored_count(Denomination denomination)
        {
            auto inventory = db->load_vault_inventory();
            if (!inventory) return -1;
            const auto it = inventory->find(denomination);
            return it == inventory->end() ? -1 : it->second;
        }

        int vault_count(Denomination denomination)
        {
            const auto& cassettes = atm->get_vault().get_cassettes();
            const auto it = cassettes.find(denomination);
            return it == cassettes.end() ? -1 : it->second.get_count();
        }
    };

    std::int64_t posted_net(const AuditLog& audit, std::string_view account_id)
    {
        std::int64_t net = 0;
        for (const auto& e : audit.entries())
        {
            if (e.debit_account_id == account_id) net -= e.amount_cents;
            if (e.credit_account_id == account_id) net += e.amount_cents;
        }
        return net;
    }
} // namespace

TEST(atm_withdraw_dispenses_greedy_combination)
{
    begin("TC-02 withdraw $250 through AtmController");
    AtmFixture f{{.checking_cents = 30000},
                 "(10000, 10, 100), (5000, 10, 100), (2000, 10, 100)"};
    f.login(kCustomerCard, UserRole::Customer);

    auto r = f.atm->process_withdraw(25000);
    CHECK(r.has_value());

    CHECK(f.vault_count(Denomination::D_100) == 8);
    CHECK(f.vault_count(Denomination::D_50) == 9);
    CHECK(f.vault_count(Denomination::D_20) == 10);
    CHECK(f.stored_count(Denomination::D_100) == 8);
    CHECK(f.stored_count(Denomination::D_50) == 9);
    CHECK(f.stored_count(Denomination::D_20) == 10);
    CHECK(f.balance("CHK-1") == 5000);

    const auto log = f.audit->entries();
    CHECK(log.size() == 1);
    if (log.size() == 1)
    {
        CHECK(log[0].debit_account_id == "CHK-1");
        CHECK(log[0].credit_account_id == ATM_VAULT_ACCOUNT_ID);
        CHECK(log[0].amount_cents == 25000);
        CHECK(log[0].is_balanced());
    }
    done();
}

TEST(atm_withdraw_rejected_without_matching_bills)
{
    begin("TC-03 $70 with only $100 bills");
    AtmFixture f{{.checking_cents = 30000}, "(10000, 10, 100)"};
    f.login(kCustomerCard, UserRole::Customer);

    auto r = f.atm->process_withdraw(7000);
    CHECK(!r.has_value());
    CHECK(r.error() == AtmError::InsufficientCashInAtm);
    CHECK(f.balance("CHK-1") == 30000);
    CHECK(f.vault_count(Denomination::D_100) == 10);
    CHECK(f.stored_count(Denomination::D_100) == 10);
    CHECK(f.audit->size() == 0);
    done();
}

TEST(atm_withdraw_rejected_with_open_door)
{
    begin("TC-03 $70 with the vault door open");
    AtmFixture f{{.checking_cents = 30000}, "(5000, 10, 100), (2000, 10, 100)"};

    f.login(kEngineerCard, UserRole::Engineer);
    CHECK(f.atm->open_vault_door().has_value());

    f.login(kCustomerCard, UserRole::Customer);
    auto r = f.atm->process_withdraw(7000);
    CHECK(!r.has_value());
    CHECK(r.error() == AtmError::VaultDoorOpen);
    CHECK(f.balance("CHK-1") == 30000);
    CHECK(f.vault_count(Denomination::D_50) == 10);
    CHECK(f.vault_count(Denomination::D_20) == 10);
    CHECK(f.audit->size() == 0);

    auto d = f.atm->process_deposit({Denomination::D_20});
    CHECK(!d.has_value());
    CHECK(d.error() == AtmError::VaultDoorOpen);
    done();
}

TEST(atm_withdraw_with_overdraft_coverage)
{
    begin("TC-04 overdraft coverage through AtmController");
    AtmFixture f{
        {.checking_cents = 15000, .client_min_cents = 10000, .savings_cents = 50000},
        "(5000, 10, 100), (2000, 10, 100), (1000, 10, 100)"};
    f.login(kCustomerCard, UserRole::Customer);

    auto r = f.atm->process_withdraw(8000);
    CHECK(r.has_value());
    CHECK(f.balance("CHK-1") == 10000);
    CHECK(f.balance("SAV-1") == 47000);
    CHECK(f.stored_count(Denomination::D_50) == 9);
    CHECK(f.stored_count(Denomination::D_20) == 9);
    CHECK(f.stored_count(Denomination::D_10) == 9);
    CHECK(f.audit->size() == 2);
    done();
}

TEST(atm_deposit_with_overflow_sweep)
{
    begin("TC-05 overflow sweep through AtmController");
    AtmFixture f{
        {.checking_cents = 80000, .overflow_max_cents = 100000, .savings_cents = 20000},
        "(10000, 0, 100)"};
    f.login(kCustomerCard, UserRole::Customer);

    auto r = f.atm->process_deposit({Denomination::D_100, Denomination::D_100,
                                     Denomination::D_100, Denomination::D_100,
                                     Denomination::D_100});
    CHECK(r.has_value());
    CHECK(f.balance("CHK-1") == 100000);
    CHECK(f.balance("SAV-1") == 50000);
    CHECK(f.vault_count(Denomination::D_100) == 5);
    CHECK(f.stored_count(Denomination::D_100) == 5);

    const auto log = f.audit->entries();
    CHECK(log.size() == 2);
    if (log.size() == 2)
    {
        CHECK(log[0].operation_type == "DEPOSIT");
        CHECK(log[1].operation_type == "OVERFLOW_SWEEP");
        CHECK(log[1].amount_cents == 30000);
    }
    done();
}

TEST(atm_withdraw_insufficient_funds)
{
    begin("Withdraw beyond balance keeps vault and account intact");
    AtmFixture f{{.checking_cents = 5000}, "(10000, 10, 100)"};
    f.login(kCustomerCard, UserRole::Customer);

    auto r = f.atm->process_withdraw(10000);
    CHECK(!r.has_value());
    CHECK(r.error() == AtmError::InsufficientFunds);
    CHECK(f.balance("CHK-1") == 5000);
    CHECK(f.vault_count(Denomination::D_100) == 10);
    CHECK(f.audit->size() == 0);
    done();
}

TEST(atm_deposit_rejected_when_cassette_full)
{
    begin("Deposit into a full cassette is rejected");
    AtmFixture f{{.checking_cents = 1000}, "(2000, 1, 1)"};
    f.login(kCustomerCard, UserRole::Customer);

    auto r = f.atm->process_deposit({Denomination::D_20});
    CHECK(!r.has_value());
    CHECK(r.error() == AtmError::CassetteFull);

    auto missing = f.atm->process_deposit({Denomination::D_500});
    CHECK(!missing.has_value());
    CHECK(missing.error() == AtmError::CassetteFull);

    CHECK(f.balance("CHK-1") == 1000);
    CHECK(f.audit->size() == 0);
    done();
}

TEST(atm_database_failure_rolls_back)
{
    begin("Database failure keeps vault and balances, ledger gets a reversal");
    AtmFixture f{{.checking_cents = 30000}, "(10000, 10, 100)"};
    f.login(kCustomerCard, UserRole::Customer);

    const bool dropped =
        f.db->execute_transaction(
                [](sqlite3* raw)
                { return storage::exec(raw, "DELETE FROM vault_cassettes").has_value(); })
            .has_value();
    CHECK(dropped);

    auto r = f.atm->process_withdraw(10000);
    CHECK(!r.has_value());
    CHECK(r.error() == AtmError::DatabaseError);
    CHECK(f.balance("CHK-1") == 30000);
    CHECK(f.vault_count(Denomination::D_100) == 10);

    const auto log = f.audit->entries();
    CHECK(log.size() == 2);
    CHECK(log.size() == 2 && log[1].operation_type == "WITHDRAWAL_REVERSAL");
    CHECK(posted_net(*f.audit, "CHK-1") == 0);
    CHECK(posted_net(*f.audit, ATM_VAULT_ACCOUNT_ID) == 0);

    auto chain = f.audit->verify_integrity_chain();
    CHECK(chain.has_value() && chain->ok);
    done();
}

TEST(atm_cash_operations_require_roles)
{
    begin("Cash operations check the session role");
    AtmFixture f{{.checking_cents = 30000}, "(10000, 10, 100)"};

    auto none = f.atm->process_withdraw(10000);
    CHECK(!none.has_value());
    CHECK(none.error() == AtmError::NoActiveSession);

    f.login(kCustomerCard, UserRole::Customer);
    auto replenish = f.atm->process_replenish(Denomination::D_100, 5);
    CHECK(!replenish.has_value());
    CHECK(replenish.error() == AtmError::Unauthorized);
    auto door = f.atm->open_vault_door();
    CHECK(!door.has_value());
    CHECK(door.error() == AtmError::Unauthorized);

    f.login(kEngineerCard, UserRole::Engineer);
    auto withdraw = f.atm->process_withdraw(10000);
    CHECK(!withdraw.has_value());
    CHECK(withdraw.error() == AtmError::Unauthorized);

    auto closed = f.atm->process_replenish(Denomination::D_100, 5);
    CHECK(!closed.has_value());
    CHECK(closed.error() == AtmError::VaultAccessDenied);

    CHECK(f.vault_count(Denomination::D_100) == 10);
    CHECK(f.balance("CHK-1") == 30000);
    done();
}

TEST(atm_unknown_card_is_rejected)
{
    begin("Withdraw with a card that has no account");
    AtmFixture f{{.checking_cents = 30000}, "(10000, 10, 100)"};
    f.login("1111222233334444", UserRole::Customer);

    auto r = f.atm->process_withdraw(10000);
    CHECK(!r.has_value());
    CHECK(r.error() == AtmError::CardNotFound);
    CHECK(f.vault_count(Denomination::D_100) == 10);
    done();
}

TEST(atm_nonce_unique_across_sessions)
{
    begin("Consecutive sessions are not treated as replays");
    AtmFixture f{{.checking_cents = 30000}, "(10000, 10, 100)"};

    f.login(kCustomerCard, UserRole::Customer);
    CHECK(f.atm->process_withdraw(10000).has_value());

    f.login(kCustomerCard, UserRole::Customer);
    CHECK(f.atm->process_withdraw(10000).has_value());

    CHECK(f.balance("CHK-1") == 10000);
    CHECK(f.stored_count(Denomination::D_100) == 8);
    done();
}

TEST(atm_full_cycle_passes_inv_check)
{
    begin("Replenish, deposit and withdraw keep InvCheck green");
    AtmFixture f{{.checking_cents = 0, .overflow_max_cents = 15000},
                 "(10000, 0, 100), (5000, 0, 100)"};

    f.login(kEngineerCard, UserRole::Engineer);
    CHECK(f.atm->open_vault_door().has_value());
    CHECK(f.atm->process_replenish(Denomination::D_100, 10).has_value());
    CHECK(f.atm->process_replenish(Denomination::D_50, 10).has_value());
    auto overfill = f.atm->process_replenish(Denomination::D_50, 91);
    CHECK(!overfill.has_value());
    CHECK(overfill.error() == AtmError::CassetteFull);
    CHECK(f.atm->close_vault_door().has_value());
    CHECK(f.stored_count(Denomination::D_100) == 10);
    CHECK(f.stored_count(Denomination::D_50) == 10);

    f.login(kCustomerCard, UserRole::Customer);
    CHECK(f.atm->process_deposit({Denomination::D_100, Denomination::D_100}).has_value());
    CHECK(f.balance("CHK-1") == 15000);
    CHECK(f.balance("SAV-1") == 5000);
    CHECK(f.atm->process_withdraw(15000).has_value());
    CHECK(f.balance("CHK-1") == 0);

    auto checking = f.db->load_account("CHK-1");
    auto savings = f.db->load_account("SAV-1");
    CHECK(checking.has_value() && savings.has_value());

    const auto report = f.ledger->inv_check({checking->get(), savings->get()},
                                            f.atm->get_vault().get_total_cash_cents(), 0);
    CHECK(report.passed);
    for (const auto& failure : report.failures) std::cout << "    " << failure << '\n';
    CHECK(f.atm->get_vault().get_total_cash_cents() == 155000);
    done();
}

TEST(atm_customer_accounts_and_transfer)
{
    begin("Customer account query and internal transfers through AtmController");
    AtmFixture f{{.checking_cents = 50000,
                  .client_min_cents = 10000,
                  .overflow_max_cents = 100000,
                  .savings_cents = 20000},
                 "(10000, 10, 100)"};

    auto unauth = f.atm->get_customer_accounts();
    CHECK(!unauth.has_value());
    CHECK(unauth.error() == AtmError::NoActiveSession);

    f.login(kCustomerCard, UserRole::Customer);

    const auto snap = f.atm->get_customer_accounts();
    CHECK(snap.has_value());
    if (snap)
    {
        CHECK(snap->checking_id == "CHK-1");
        CHECK(snap->checking_balance_cents == 50000);
        CHECK(snap->client_min_cents == 10000);
        CHECK(snap->overflow_max_cents == 100000);
        CHECK(snap->savings_id == "SAV-1");
        CHECK(snap->savings_balance_cents == 20000);
    }

    CHECK(f.atm->process_transfer("Checking", "Savings", 20000).has_value());
    CHECK(f.balance("CHK-1") == 30000);
    CHECK(f.balance("SAV-1") == 40000);

    CHECK(f.atm->process_transfer("Savings", "Checking", 10000).has_value());
    CHECK(f.balance("CHK-1") == 40000);
    CHECK(f.balance("SAV-1") == 30000);

    const auto below_min = f.atm->process_transfer("Checking", "Savings", 35000);
    CHECK(!below_min.has_value());
    CHECK(below_min.error() == AtmError::InsufficientFunds);
    CHECK(f.balance("CHK-1") == 40000);
    CHECK(f.balance("SAV-1") == 30000);

    const auto self_tx = f.atm->process_transfer("Checking", "Checking", 1000);
    CHECK(!self_tx.has_value());
    CHECK(self_tx.error() == AtmError::InvalidAmount);
    done();
}

TEST(atm_transfer_to_external_account_and_replay_check)
{
    begin("External card transfer and idempotency key replay protection");
    AtmFixture f{{.checking_cents = 60000,
                  .client_min_cents = 0,
                  .overflow_max_cents = 200000,
                  .savings_cents = 10000},
                 "(10000, 10, 100)"};

    const bool seeded_ext =
        f.db->execute_transaction(
                [](sqlite3* raw)
                {
                    return storage::exec(
                               raw,
                               "INSERT INTO accounts (account_id, client_id, type, "
                               "balance_cents, client_min_cents, overflow_max_cents) "
                               "VALUES ('CHK-2', 'olena', 'checking', 5000, 0, 200000); "
                               "INSERT INTO cards (card_number, card_holder, pin_hash, "
                               "salt, role, checking_account_id) "
                               "VALUES ('4000000000000002', 'olena', 'h', 's', 0, "
                               "'CHK-2');")
                        .has_value();
                })
            .has_value();
    CHECK(seeded_ext);

    f.login(kCustomerCard, UserRole::Customer);

    const auto tx =
        f.atm->process_transfer("Checking", "4000000000000002", 15000, "idemp-ext-1");
    CHECK(tx.has_value());
    CHECK(f.balance("CHK-1") == 45000);
    CHECK(f.balance("CHK-2") == 20000);

    const auto log = f.audit->entries();
    CHECK(log.size() == 1);
    if (log.size() == 1)
    {
        CHECK(log[0].operation_type == "TRANSFER");
        CHECK(log[0].debit_account_id == "CHK-1");
        CHECK(log[0].credit_account_id == "CHK-2");
        CHECK(log[0].amount_cents == 15000);
    }

    const auto replay =
        f.atm->process_transfer("Checking", "4000000000000002", 15000, "idemp-ext-1");
    CHECK(!replay.has_value());
    CHECK(replay.error() == AtmError::ReplayAttackDetected);
    CHECK(f.balance("CHK-1") == 45000);
    CHECK(f.balance("CHK-2") == 20000);

    const auto unknown = f.atm->process_transfer("Checking", "9999999999999999", 1000);
    CHECK(!unknown.has_value());
    CHECK(unknown.error() == AtmError::CardNotFound);
    done();
}

TEST(atm_fsm_service_selection_and_engineer_audit)
{
    begin("FSM service selection, engineer vault open key and audit verification");
    AtmFixture f{{.checking_cents = 40000}, "(10000, 10, 100)"};

    const std::string customer_hash = crypto::sha256_hex("1234", "salt123");
    const std::string engineer_hash = crypto::sha256_hex("0000", "salt456");
    const std::string update_cards_sql = std::format(R"sql(
UPDATE cards SET pin_hash = '{0}', salt = 'salt123', role = 0 WHERE card_number = '{1}';
UPDATE cards SET pin_hash = '{2}', salt = 'salt456', role = 1 WHERE card_number = '{3}';
)sql",
                                                     customer_hash, kCustomerCard,
                                                     engineer_hash, kEngineerCard);

    const bool updated =
        f.db->execute_transaction(
                [&update_cards_sql](sqlite3* raw)
                { return storage::exec(raw, update_cards_sql.c_str()).has_value(); })
            .has_value();
    CHECK(updated);

    CHECK(f.atm->process_insert_card(kCustomerCard).has_value());
    CHECK(f.atm->get_current_state_name() == "CardInserted");
    CHECK(f.atm->process_enter_pin("1234").has_value());
    CHECK(f.atm->get_current_state_name() == "CustomerSession");
    CHECK(f.atm->get_active_card().has_value());
    CHECK(f.atm->get_active_card()->holder == "yosyp");

    CHECK(f.atm->process_select_service(ServiceType::Transfer).has_value());
    const auto forbidden_svc =
        f.atm->process_select_service(ServiceType::EngineerMaintenance);
    CHECK(!forbidden_svc.has_value());
    CHECK(forbidden_svc.error() == AtmError::Unauthorized);

    const auto customer_vault = f.atm->process_open_vault("ENG-MASTER-KEY-2026");
    CHECK(!customer_vault.has_value());
    CHECK(customer_vault.error() == AtmError::InvalidStateTransition);

    CHECK(f.atm->process_eject_card().has_value());
    CHECK(f.atm->get_current_state_name() == "Idle");
    CHECK(!f.atm->get_active_session_id().has_value());
    CHECK(!f.atm->get_active_card().has_value());

    CHECK(f.atm->process_insert_card(kEngineerCard).has_value());
    CHECK(f.atm->process_enter_pin("0000").has_value());
    CHECK(f.atm->get_current_state_name() == "EngineerSession");
    CHECK(f.atm->get_vault().is_door_closed());

    const auto bad_key = f.atm->process_open_vault("WRONG-KEY");
    CHECK(!bad_key.has_value());
    CHECK(bad_key.error() == AtmError::VaultAccessDenied);
    CHECK(f.atm->get_vault().is_door_closed());

    CHECK(f.atm->process_open_vault("ENG-MASTER-KEY-2026").has_value());
    CHECK(!f.atm->get_vault().is_door_closed());
    CHECK(f.atm->process_replenish(Denomination::D_100, 2).has_value());
    CHECK(f.atm->close_vault_door().has_value());
    CHECK(f.atm->get_vault().is_door_closed());

    const auto audit_entries = f.atm->get_audit_entries();
    CHECK(audit_entries.has_value());
    if (audit_entries)
    {
        CHECK(audit_entries->size() == 1);
        CHECK((*audit_entries)[0].operation_type == "REPLENISHMENT");
    }

    const auto chain = f.atm->verify_audit_integrity();
    CHECK(chain.has_value() && chain->ok);

    CHECK(f.atm->process_eject_card().has_value());
    CHECK(f.atm->get_current_state_name() == "Idle");
    CHECK(!f.atm->get_active_session_id().has_value());
    done();
}
