/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: ledger_service.cpp
 * Implementation of LedgerService.
 *
 */

#include "ledger_service.hpp"

#include "crypto.hpp"

#include <algorithm>
#include <map>
#include <utility>

namespace
{
    LedgerError map_account_error(AccountError e)
    {
        using enum AccountError;

        switch (e)
        {
        case InvalidAmount:
            return LedgerError::InvalidAmount;
        case InsufficientFunds:
            return LedgerError::InsufficientFunds;
        case BalanceOverflow:
            return LedgerError::AuditFailure;
        case NoOverdraftProtection:
            return LedgerError::NoOverdraftCoverage;
        }
        return LedgerError::AuditFailure;
    }
} // namespace

LedgerService::LedgerService(std::shared_ptr<AuditLog> audit_log,
                             std::shared_ptr<AntiReplayManager> anti_replay)
    : _audit_log{std::move(audit_log)}, _anti_replay{std::move(anti_replay)}
{
}

bool LedgerService::integrity_ok() const noexcept { return !_integrity_compromised; }

LedgerEntry LedgerService::make_entry(std::string_view debit_id,
                                      std::string_view credit_id,
                                      std::int64_t amount_cents,
                                      std::string_view operation_type,
                                      std::uint64_t nonce) const
{
    LedgerEntry entry;
    entry.tx_id = crypto::generate_uuid_v4();
    entry.debit_account_id = std::string(debit_id);
    entry.credit_account_id = std::string(credit_id);
    entry.amount_cents = amount_cents;
    entry.timestamp = make_ledger_timestamp();
    entry.operation_type = std::string(operation_type);
    entry.nonce = nonce;
    return entry;
}

std::expected<void, LedgerError> LedgerService::claim_nonce(
    std::uint64_t nonce, std::string_view idempotency_key)
{
    const std::string key = idempotency_key.empty() ? std::format("tx:{}", nonce)
                                                    : std::string(idempotency_key);

    if (!_anti_replay->is_request_unique(key))
    {
        return std::unexpected(LedgerError::ReplayDetected);
    }
    return {};
}

std::expected<void, LedgerError> LedgerService::append_entries(
    std::vector<LedgerEntry> entries)
{
    for (const auto& e : entries)
    {
        if (!e.is_balanced()) { return std::unexpected(LedgerError::UnbalancedEntry); }
    }

    if (auto r = _audit_log->append_batch(entries); !r)
    {
        switch (r.error())
        {
        case CryptoError::EntryNotBalanced:
            return std::unexpected(LedgerError::UnbalancedEntry);
        default:
            return std::unexpected(LedgerError::AuditFailure);
        }
    }
    return {};
}

std::expected<void, LedgerError> LedgerService::transfer(Account& from, Account& to,
                                                         std::int64_t amount_cents,
                                                         std::string_view operation_type,
                                                         std::uint64_t nonce,
                                                         std::string_view idempotency_key)
{
    if (_integrity_compromised) return std::unexpected(LedgerError::IntegrityCompromised);
    if (amount_cents <= 0) return std::unexpected(LedgerError::InvalidAmount);

    if (auto r = claim_nonce(nonce, idempotency_key); !r) return r;

    if (auto w = from.withdraw(amount_cents); !w)
    {
        return std::unexpected(map_account_error(w.error()));
    }
    if (auto d = to.deposit(amount_cents); !d)
    {
        from.deposit(amount_cents);
        return std::unexpected(map_account_error(d.error()));
    }

    if (auto r = append_entries({make_entry(from.get_account_id(), to.get_account_id(),
                                            amount_cents, operation_type, nonce)});
        !r)
    {
        to.withdraw(amount_cents);
        from.deposit(amount_cents);
        return r;
    }
    return {};
}
