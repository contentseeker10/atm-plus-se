/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: test_admin.cpp
 * Card administration, access windows, forced door and audit filtering.
 *
 */

#include "test_framework.hpp"

#include "access_control/atm_controller.hpp"
#include "access_control/auth_service.hpp"
#include "accounting/audit.hpp"
#include "cash/cash_vault.hpp"
#include "crypto.hpp"
#include "db.hpp"
#include "enums.hpp"
#include "storage/operations.hpp"
#include "storage/sqlite_utils.hpp"

#include <sqlite3.h>

#include <chrono>
#include <ctime>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace
{
    constexpr std::string_view kEngineerCard = "9000000000000001";

    struct AdminFixture
    {
        std::shared_ptr<DatabaseManager> db;
        std::shared_ptr<AuditLog> audit;
        std::shared_ptr<LedgerService> ledger;
        std::unique_ptr<AtmController> atm;

        AdminFixture()
            : db{std::make_shared<DatabaseManager>(":memory:")},
              audit{std::make_shared<AuditLog>(db, "test-hmac-secret")},
              ledger{std::make_shared<LedgerService>(
                  audit, std::make_shared<AntiReplayManager>())}
        {
            const char* sql = R"sql(
INSERT INTO accounts (account_id, client_id, type, balance_cents,
                      client_min_cents, overflow_max_cents)
VALUES ('CHK-1', 'yosyp', 'checking', 50000, 0, 1000000);

INSERT INTO cards (card_number, card_holder, pin_hash, salt, role, checking_account_id)
VALUES ('9000000000000001', 'engineer', 'hash', 'salt', 2, NULL);
)sql";
            const bool seeded =
                db->execute_transaction([sql](sqlite3* raw)
                                        { return storage::exec(raw, sql).has_value(); })
                    .has_value();
            CHECK(seeded);

            std::map<Denomination, Cassette> cassette_map;
            cassette_map.emplace(Denomination::D_100,
                                 Cassette{Denomination::D_100, 10, 100});

            atm = std::make_unique<AtmController>(
                std::make_unique<CashVault>(std::move(cassette_map)), db, audit,
                std::make_unique<AuthService>(db, audit), ledger);
        }

        void login_engineer()
        {
            atm->end_session();
            CardRecord record{std::string(kEngineerCard), false, false};
            record.role = UserRole::Engineer;
            atm->start_session(record);
        }
    };

    bool has_event(const AuditLog& audit, std::string_view event,
                   std::string_view card = {})
    {
        for (const auto& e : audit.entries())
        {
            if (e.operation_type != event) continue;
            if (!card.empty() && e.debit_account_id != card) continue;
            return true;
        }
        return false;
    }
} // namespace

TEST(admin_issue_and_revoke_card)
{
    begin("Admin card issuance and revocation");
    AdminFixture f;
    f.login_engineer();

    const auto issued =
        f.atm->admin_issue_card("4000000000000101", "NEW CLIENT", "5152",
                                UserRole::Customer, "CHK-1", std::nullopt, std::nullopt);
    CHECK(issued.has_value());
    CHECK(has_event(*f.audit, "CARD_ISSUED", "4000000000000101"));

    const auto cards = f.atm->admin_list_cards();
    CHECK(cards.has_value());
    bool found = false;
    for (const auto& c : *cards)
    {
        if (c.number == "4000000000000101")
        {
            found = true;
            CHECK(c.role == UserRole::Customer);
            CHECK(!c.is_revoked);
            CHECK(c.checking_account_id == std::optional<std::string>{"CHK-1"});
        }
    }
    CHECK(found);

    AuthService direct_auth{f.db, f.audit};
    const auto auth_ok = direct_auth.authenticate_card("4000000000000101", "5152");
    CHECK(auth_ok.has_value());
    CHECK(auth_ok->role == UserRole::Customer);

    const auto revoked = f.atm->admin_revoke_card("4000000000000101");
    CHECK(revoked.has_value());
    CHECK(has_event(*f.audit, "CARD_REVOKED", "4000000000000101"));

    const auto auth_after = direct_auth.authenticate_card("4000000000000101", "5152");
    CHECK(!auth_after.has_value());
    CHECK(auth_after.error() == AuthError::CardRevoked);
    CHECK(has_event(*f.audit, "CARD_ACCESS_DENIED", "4000000000000101"));

    f.atm->end_session();
    const auto insert_after = f.atm->process_insert_card("4000000000000101");
    CHECK(!insert_after.has_value());
    CHECK(insert_after.error() == AtmError::CardRevoked);

    const auto revoke_missing = f.atm->admin_revoke_card("0000000000000000");
    CHECK(!revoke_missing.has_value());

    const auto integrity = f.audit->verify_integrity_chain();
    CHECK(integrity.has_value() && integrity->ok);
    done();
}

TEST(admin_role_and_window_updates)
{
    begin("Admin access rights and time window policy");
    AdminFixture f;
    f.login_engineer();

    const auto issued =
        f.atm->admin_issue_card("4000000000000102", "SHIFT USER", "5152",
                                UserRole::Customer, "CHK-1", std::nullopt, std::nullopt);
    CHECK(issued.has_value());

    const auto role_update =
        f.atm->admin_set_card_role("4000000000000102", UserRole::Engineer);
    CHECK(role_update.has_value());
    CHECK(has_event(*f.audit, "CARD_RIGHTS_UPDATED", "4000000000000102"));

    const auto loaded = f.db->load_card("4000000000000102", false);
    CHECK(loaded.has_value());
    CHECK(loaded->role == UserRole::Engineer);

    AuthService direct_auth{f.db, f.audit};
    const auto auth_ok = direct_auth.authenticate_card("4000000000000102", "5152");
    CHECK(auth_ok.has_value());
    CHECK(auth_ok->role == UserRole::Engineer);

    std::time_t now =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    const int next_hour = (utc.tm_hour + 1) % 24;

    const auto window_outside =
        f.atm->admin_set_card_window("4000000000000102", next_hour, next_hour);
    CHECK(window_outside.has_value());
    CHECK(has_event(*f.audit, "CARD_WINDOW_UPDATED", "4000000000000102"));

    const auto auth_denied = direct_auth.authenticate_card("4000000000000102", "5152");
    CHECK(!auth_denied.has_value());
    CHECK(auth_denied.error() == AuthError::AccessWindowDenied);
    CHECK(has_event(*f.audit, "CARD_ACCESS_DENIED", "4000000000000102"));

    const auto window_always = f.atm->admin_set_card_window("4000000000000102", 0, 23);
    CHECK(window_always.has_value());
    const auto auth_allowed = direct_auth.authenticate_card("4000000000000102", "5152");
    CHECK(auth_allowed.has_value());

    const auto window_half = f.atm->admin_set_card_window(
        "4000000000000102", std::optional<int>{}, std::optional<int>{});
    CHECK(window_half.has_value());
    const auto auth_no_window = direct_auth.authenticate_card("4000000000000102", "5152");
    CHECK(auth_no_window.has_value());

    const auto window_bad =
        f.atm->admin_set_card_window("4000000000000102", 5, std::optional<int>{});
    CHECK(!window_bad.has_value());

    const auto integrity = f.audit->verify_integrity_chain();
    CHECK(integrity.has_value() && integrity->ok);
    done();
}

TEST(admin_requires_engineer_role)
{
    begin("Customer session cannot run admin operations");
    AdminFixture f;

    f.login_engineer();
    const auto issued =
        f.atm->admin_issue_card("4000000000000103", "CUSTOMER", "5152",
                                UserRole::Customer, "CHK-1", std::nullopt, std::nullopt);
    CHECK(issued.has_value());

    f.atm->end_session();
    CardRecord record{"4000000000000103", false, false};
    record.role = UserRole::Customer;
    f.atm->start_session(record);

    const auto list_denied = f.atm->admin_list_cards();
    CHECK(!list_denied.has_value());
    const auto issue_denied =
        f.atm->admin_issue_card("4000000000000104", "X", "5152", UserRole::Customer,
                                std::nullopt, std::nullopt, std::nullopt);
    CHECK(!issue_denied.has_value());
    const auto revoke_denied = f.atm->admin_revoke_card("4000000000000103");
    CHECK(!revoke_denied.has_value());
    done();
}

TEST(forced_vault_door_is_distinct_audit_event)
{
    begin("Forced vault door opening leaves a distinct alarm event");
    AdminFixture f;
    f.login_engineer();

    const auto forced = f.atm->open_vault_door(true);
    CHECK(forced.has_value());
    CHECK(has_event(*f.audit, "VAULT_DOOR_FORCED"));

    const auto closed = f.atm->close_vault_door();
    CHECK(closed.has_value());

    const auto normal = f.atm->open_vault_door();
    CHECK(normal.has_value());
    CHECK(has_event(*f.audit, "VAULT_DOOR_OPENED"));
    CHECK(has_event(*f.audit, "VAULT_DOOR_CLOSED"));

    const auto integrity = f.audit->verify_integrity_chain();
    CHECK(integrity.has_value() && integrity->ok);
    done();
}

TEST(audit_filtering_and_csv_export)
{
    begin("Audit filtering by card, event and period, CSV export");
    AdminFixture f;
    f.login_engineer();

    const auto issued =
        f.atm->admin_issue_card("4000000000000111", "FILTER USER", "5152",
                                UserRole::Customer, "CHK-1", std::nullopt, std::nullopt);
    CHECK(issued.has_value());
    const auto revoked = f.atm->admin_revoke_card("4000000000000111");
    CHECK(revoked.has_value());

    AuditFilter event_filter;
    event_filter.event = std::optional<std::string>{"CARD_REVOKED"};
    const auto revoked_only = f.atm->get_audit_entries(0, 0, event_filter);
    CHECK(revoked_only.has_value());
    CHECK(revoked_only->size() == 1);
    CHECK((*revoked_only)[0].operation_type == "CARD_REVOKED");
    CHECK((*revoked_only)[0].debit_account_id == "4000000000000111");

    AuditFilter card_filter;
    card_filter.card = std::optional<std::string>{"4000000000000111"};
    const auto by_card = f.atm->get_audit_entries(0, 0, card_filter);
    CHECK(by_card.has_value());
    CHECK(by_card->size() == 2);
    for (const auto& e : *by_card) { CHECK(e.debit_account_id == "4000000000000111"); }

    AuditFilter empty_filter;
    const auto all = f.atm->get_audit_entries(0, 0, empty_filter);
    CHECK(all.has_value());
    CHECK(all->size() == f.audit->size());

    AuditFilter page_filter;
    page_filter.event = std::optional<std::string>{"CARD_REVOKED"};
    const auto first_page = f.atm->get_audit_entries(0, 1, page_filter);
    CHECK(first_page.has_value());
    CHECK(first_page->size() == 1);
    const auto second_page = f.atm->get_audit_entries(1, 1, page_filter);
    CHECK(second_page.has_value());
    CHECK(second_page->empty());

    AuditFilter since_filter;
    since_filter.since = std::optional<std::string>{"2999-01-01T00:00:00Z"};
    const auto future = f.atm->get_audit_entries(0, 0, since_filter);
    CHECK(future.has_value());
    CHECK(future->empty());

    const auto csv = f.atm->export_audit_csv(page_filter);
    CHECK(csv.has_value());
    CHECK(csv->find("id,timestamp,event") == 0);
    CHECK(csv->find("CARD_REVOKED") != std::string::npos);
    CHECK(csv->find("CARD_ISSUED") == std::string::npos);

    const auto csv_all = f.atm->export_audit_csv(AuditFilter{});
    CHECK(csv_all.has_value());
    CHECK(csv_all->find("CARD_ISSUED") != std::string::npos);
    done();
}
