/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: ledger_service.hpp
 *
 */

#pragma once

#include "audit.hpp"
#include "ledger.hpp"

#include "security/anti_replay.hpp"

#include "accounts/account.hpp"
#include "accounts/checking_account.hpp"
#include "accounts/savings_account.hpp"

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

enum class LedgerError
{
    InvalidAmount,
    InsufficientFunds,
    NoOverdraftCoverage,
    ReplayDetected,
    UnbalancedEntry,
    AuditFailure,
    IntegrityCompromised,
    InvalidRule
};

// System account for physical cash inside the vault
inline constexpr std::string_view ATM_VAULT_ACCOUNT_ID = "ATM_VAULT";

struct ApsRule
{
    std::string rule_id;
    std::string payer_account_id;
    std::string payee_account_id;
    std::int64_t amount_cents{};
    bool fixed_amount{true};
    std::chrono::seconds period{0};
    std::chrono::system_clock::time_point last_run{};
    std::uint64_t run_count{};
};

struct ApsRunResult
{
    std::size_t executed{};
    std::vector<std::string> failures;
};

struct InvCheckReport
{
    bool passed{};
    std::vector<std::string> passed_checks;
    std::vector<std::string> failures;
};

class LedgerService
{
public:
    LedgerService(std::shared_ptr<AuditLog> audit_log,
                  std::shared_ptr<AntiReplayManager> anti_replay);

    std::expected<void, LedgerError> transfer(Account& from, Account& to,
                                              std::int64_t amount_cents,
                                              std::string_view operation_type,
                                              std::uint64_t nonce,
                                              std::string_view idempotency_key = "");

    std::expected<void, LedgerError> withdraw_with_overdraft(CheckingAccount& checking,
                                                             SavingsAccount& savings,
                                                             std::int64_t amount_cents,
                                                             std::uint64_t nonce);

    std::expected<void, LedgerError> deposit_with_overflow(CheckingAccount& checking,
                                                           SavingsAccount& savings,
                                                           std::int64_t amount_cents,
                                                           std::uint64_t nonce);

    std::expected<void, LedgerError> register_aps_rule(ApsRule rule);

    ApsRunResult run_aps(
        std::chrono::system_clock::time_point now,
        const std::function<Account*(const std::string&)>& resolve_account);

    InvCheckReport inv_check(const std::vector<const Account*>& accounts,
                             std::int64_t vault_cash_cents,
                             std::int64_t bank_min_cents) const;

    bool integrity_ok() const noexcept;

private:
    std::expected<void, LedgerError> append_entries(std::vector<LedgerEntry> entries);
    std::expected<void, LedgerError> claim_nonce(std::uint64_t nonce,
                                                 std::string_view idempotency_key);

    LedgerEntry make_entry(std::string_view debit_id, std::string_view credit_id,
                           std::int64_t amount_cents, std::string_view operation_type,
                           std::uint64_t nonce) const;

    std::shared_ptr<AuditLog> _audit_log;
    std::shared_ptr<AntiReplayManager> _anti_replay;

    std::vector<ApsRule> _aps_rules;
    std::uint64_t _aps_nonce_counter{0};

    mutable bool _integrity_compromised{false};
    mutable std::mutex _aps_mtx;
};
