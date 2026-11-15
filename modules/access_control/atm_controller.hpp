/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: atm_controller.hpp
 * Interface for ATM's controller unit.
 *
 */

#pragma once

#include "db.hpp"

#include "accounting/audit.hpp"
#include "accounting/ledger_service.hpp"

#include "atm_state.hpp"
#include "auth_service.hpp"
#include "card.hpp"
#include "session_context.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace std::literals::chrono_literals;

class Account;
class AntiReplayManager;
class CashVault;

struct CustomerAccountSnapshot
{
    std::string checking_id;
    std::int64_t checking_balance_cents{};
    std::int64_t client_min_cents{};
    std::int64_t overflow_max_cents{};
    std::string savings_id;
    std::int64_t savings_balance_cents{};
};

struct WithdrawResult
{
    std::map<Denomination, int> dispensed_bills;
    std::int64_t overdraft_covered_cents{};
    CustomerAccountSnapshot accounts;
    std::int64_t target_new_balance_cents{};
};

struct DepositResult
{
    std::int64_t total_deposited_cents{};
    std::int64_t overflow_swept_cents{};
    CustomerAccountSnapshot accounts;
    std::int64_t target_new_balance_cents{};
};

struct TransferResult
{
    std::int64_t source_new_balance_cents{};
    CustomerAccountSnapshot accounts;
    std::int64_t overdraft_covered_cents{};
    std::int64_t overflow_swept_cents{};
};

struct CustomerApsRunResult
{
    ApsRunResult run;
    CustomerAccountSnapshot accounts;
};

class AtmController
{
public:
    AtmController(std::unique_ptr<CashVault> vault, std::shared_ptr<DatabaseManager> db,
                  std::shared_ptr<AuditLog> log, std::unique_ptr<AuthService> auth,
                  std::shared_ptr<LedgerService> ledger = nullptr,
                  std::shared_ptr<AntiReplayManager> anti_replay = nullptr);
    ~AtmController();

    AtmController(const AtmController&) = delete;
    AtmController& operator=(const AtmController&) = delete;

    AtmController(AtmController&&) = delete;
    AtmController& operator=(AtmController&&) = delete;

    void transition_to(std::unique_ptr<IAtmState> new_state);
    void start_session(const CardRecord& card, bool authenticated = true);
    void update_session(const CardRecord& card);
    void end_session() noexcept;

    std::expected<void, AtmError> process_insert_card(std::string_view card);
    std::expected<void, AtmError> process_enter_pin(std::string_view pin);
    std::expected<void, AtmError> process_select_service(ServiceType service);
    std::expected<void, AtmError> process_eject_card();
    std::expected<void, AtmError> process_open_vault(std::string_view engineer_key);
    std::expected<WithdrawResult, AtmError> process_withdraw(
        int64_t amount_cents, std::string_view account_type = "Checking",
        std::string_view idempotency_key = "",
        std::optional<std::uint64_t> incoming_nonce = std::nullopt);
    std::expected<DepositResult, AtmError> process_deposit(
        const std::vector<Denomination>& bills,
        std::string_view account_type = "Checking", std::string_view idempotency_key = "",
        std::optional<std::uint64_t> incoming_nonce = std::nullopt);
    std::expected<TransferResult, AtmError> process_transfer(
        std::string_view from_account, std::string_view to_account,
        std::int64_t amount_cents, std::string_view idempotency_key = "",
        std::optional<std::uint64_t> incoming_nonce = std::nullopt);
    std::expected<void, AtmError> process_replenish(Denomination denomination, int count);
    std::expected<void, AtmError> process_replace_cassette(Denomination denomination,
                                                           int new_count);
    std::expected<void, AtmError> process_unlock_card(std::string_view card_number);

    std::expected<CustomerAccountSnapshot, AtmError> get_customer_accounts();
    std::expected<std::vector<ApsRule>, AtmError> get_customer_aps_rules();
    std::expected<ApsRule, AtmError> create_customer_aps_rule(
        std::string_view from_account, std::string_view to_account,
        std::int64_t amount_cents, bool fixed_amount, std::int64_t period_seconds);
    std::expected<CustomerApsRunResult, AtmError> run_customer_aps();
    std::expected<void, AtmError> delete_customer_aps_rule(std::string_view rule_id);
    std::expected<std::vector<LedgerEntry>, AtmError> get_audit_entries(
        std::size_t offset = 0, std::size_t limit = 0,
        const AuditFilter& filter = AuditFilter{});
    std::expected<std::string, AtmError> export_audit_csv(const AuditFilter& filter);

    std::expected<std::vector<storage::CardAdminRecord>, AtmError> admin_list_cards();
    std::expected<void, AtmError> admin_issue_card(
        std::string_view card_number, std::string_view holder, std::string_view pin,
        UserRole role, std::optional<std::string> checking_account_id,
        std::optional<int> window_start, std::optional<int> window_end);
    std::expected<void, AtmError> admin_revoke_card(std::string_view card_number);
    std::expected<void, AtmError> admin_set_card_role(std::string_view card_number,
                                                      UserRole role);
    std::expected<void, AtmError> admin_set_card_window(std::string_view card_number,
                                                        std::optional<int> window_start,
                                                        std::optional<int> window_end);
    std::expected<ChainVerification, AtmError> verify_audit_integrity();
    bool is_audit_entry_tampered(const LedgerEntry& entry) const;
    std::expected<void, AtmError> simulate_audit_tamper(bool restore = false);

    std::expected<void, AtmError> open_vault_door(bool forced = false);
    std::expected<void, AtmError> close_vault_door();
    std::expected<void, AtmError> _append_security_event(
        std::string_view operation_type,
        std::string_view debit_id = std::string_view{BANK_TREASURY_ACCOUNT_ID});
    bool verify_engineer_key(std::string_view engineer_key) const noexcept;

    const CashVault& get_vault() const noexcept { return *_vault; }

    std::expected<CardRecord, AtmError> validate_card(std::string_view card);
    std::expected<CardRecord, AtmError> authenticate_card(std::string_view pin);
    std::expected<void, AtmError> validate_session();

    std::optional<std::string> get_active_session_id() const;
    std::optional<CardRecord> get_active_card() const;
    UserRole get_active_role() const;
    int get_remaining_pin_attempts(std::string_view card) const;

    std::string_view get_current_state_name() const
    { return _current_state->get_state_name(); }

private:
    inline static constexpr std::chrono::seconds kTimeout = 45s;
    inline static constexpr std::string_view kDefaultEngineerKey = "ENG-MASTER-KEY-2026";

    std::expected<void, AtmError> require_role(UserRole role);
    std::expected<void, AtmError> check_customer_replay_guard(
        std::string_view idempotency_key,
        std::optional<std::uint64_t> incoming_nonce) const;
    std::expected<void, AtmError> consume_session_nonce(
        std::optional<std::uint64_t> incoming_nonce = std::nullopt);
    void rollback_session_nonce(std::uint64_t previous_nonce);
    std::expected<void, AtmError> commit_operation(
        std::optional<CashVault> next_vault, const std::vector<const Account*>& accounts,
        std::uint64_t nonce, std::size_t audit_mark);

    std::shared_ptr<DatabaseManager> _db;
    std::shared_ptr<AuditLog> _audit_log;
    std::shared_ptr<LedgerService> _ledger;
    std::shared_ptr<AntiReplayManager> _anti_replay;

    std::optional<SessionContext> _active_session{std::nullopt};

    std::unique_ptr<IAtmState> _current_state{std::make_unique<IdleState>()};
    std::unique_ptr<CashVault> _vault;
    std::unique_ptr<AuthService> _auth_service;

    std::uint64_t _tx_nonce{0};

    mutable std::recursive_mutex _mtx;
};