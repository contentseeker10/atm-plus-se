/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: auth_service.cpp
 * Implementation of auth service.
 *
 */

#include "auth_service.hpp"

#include "crypto.hpp"

#include "storage/operations.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <optional>

namespace
{
    int current_hour_utc()
    {
        const std::time_t now =
            std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm utc{};
#ifdef _WIN32
        gmtime_s(&utc, &now);
#else
        gmtime_r(&now, &utc);
#endif
        return utc.tm_hour;
    }
} // namespace

std::expected<CardRecord, AuthError> AuthService::validate_card(std::string_view card,
                                                                bool auth)
{
    auto record = _load_card(card, !auth);
    if (!record) { return std::unexpected(record.error()); }
    if (_is_card_blocked(record.value()))
    {
        return std::unexpected(AuthError::CardBlocked);
    }
    if (_is_card_expired(record.value()))
    {
        return std::unexpected(AuthError::CardExpired);
    }
    if (record.value().is_revoked) { return std::unexpected(AuthError::CardRevoked); }
    if (!_within_access_window(record.value()))
    {
        return std::unexpected(AuthError::AccessWindowDenied);
    }
    return record.value();
}

std::expected<CardRecord, AuthError> AuthService::authenticate_card(std::string_view card,
                                                                    std::string_view pin)
{
    auto record = validate_card(card, true);
    if (!record)
    {
        (void) _log_access_denied(card);
        return std::unexpected(record.error());
    }
    auto hash = crypto::sha256_hex(pin, record->salt);
    const bool pin_ok = crypto::constant_time_compare(hash, record->pin_hash);
    crypto::secure_zero(hash);
    if (!pin_ok)
    {
        auto inc_failed = _inc_failed_attempts(card);
        if (!inc_failed) { return std::unexpected(inc_failed.error()); }
        if (inc_failed.value() == kMaxFailedAttempts)
        {
            auto block_result = _block_card(card);
            if (!block_result) { return std::unexpected(block_result.error()); }
            return std::unexpected(AuthError::MaxAttemptsExceeded);
        }
        (void) _log_access_denied(card);
        return std::unexpected(AuthError::InvalidPin);
    }
    auto reset = _reset_failed_attempts(card);
    if (!reset) { return std::unexpected(reset.error()); }
    return record;
}

std::expected<void, AuthError> AuthService::unlock_card(std::string_view card)
{
    auto result = _db->unlock_card(card);
    if (!result) { return std::unexpected(to_auth_error(result.error())); }
    if (_audit_log)
    {
        LedgerEntry entry;
        entry.debit_account_id = std::string(card);
        entry.credit_account_id = "SECURITY_AUDIT";
        entry.amount_cents = 1;
        entry.operation_type = "CARD_UNLOCKED";
        auto audit_res = _audit_log->append_record(entry);
        if (!audit_res) { return std::unexpected(AuthError::DatabaseError); }
    }
    return {};
}

std::expected<CardRecord, AuthError> AuthService::_load_card(std::string_view card,
                                                             bool validate_only)
{
    auto record = _db->load_card(card, validate_only);
    if (!record) { return std::unexpected(to_auth_error(record.error())); }
    return record.value();
}

std::expected<void, AuthError> AuthService::_block_card(std::string_view card)
{
    auto result = _db->block_card(card);
    if (!result) { return std::unexpected(to_auth_error(result.error())); }
    if (_audit_log)
    {
        LedgerEntry entry;
        entry.debit_account_id = std::string(card);
        entry.credit_account_id = "SECURITY_AUDIT";
        entry.amount_cents = 1;
        entry.operation_type = "CARD_BLOCKED";
        auto audit_res = _audit_log->append_record(entry);
        if (!audit_res) { return std::unexpected(AuthError::DatabaseError); }
    }
    return {};
}

std::expected<int, AuthError> AuthService::_inc_failed_attempts(std::string_view card)
{
    int attempts = 0;
    std::optional<DbError> tx_err;
    auto tx_res = _db->execute_transaction(
        [&](sqlite3* raw)
        {
            auto current_failed = storage::get_failed_attempts(raw, card);
            if (!current_failed)
            {
                tx_err = current_failed.error();
                return false;
            }
            attempts = current_failed.value();
            if (attempts >= kMaxFailedAttempts) { return true; }
            ++attempts;
            auto set_res = storage::set_failed_attempts(raw, card, attempts);
            if (!set_res)
            {
                tx_err = set_res.error();
                return false;
            }
            if (attempts == kMaxFailedAttempts)
            {
                auto blk_res = storage::block_card(raw, card);
                if (!blk_res)
                {
                    tx_err = blk_res.error();
                    return false;
                }
            }
            return true;
        });
    if (!tx_res)
    {
        return std::unexpected(to_auth_error(tx_err.value_or(tx_res.error())));
    }
    return attempts;
}

int AuthService::get_remaining_attempts(std::string_view card) const
{
    if (!_db || card.empty()) { return 0; }
    const auto attempts = _db->get_failed_attempts(card);
    if (!attempts) { return 0; }
    return std::max(0, kMaxFailedAttempts - *attempts);
}

std::expected<void, AuthError> AuthService::_reset_failed_attempts(std::string_view card)
{
    auto result = _db->set_failed_attempts(card, 0);
    if (!result) { return std::unexpected(to_auth_error(result.error())); }
    return {};
}
std::expected<void, AuthError> AuthService::_log_access_denied(std::string_view card)
{
    if (!_audit_log || card.empty()) { return {}; }
    LedgerEntry entry;
    entry.debit_account_id = std::string(card);
    entry.credit_account_id = "SECURITY_AUDIT";
    entry.amount_cents = 1;
    entry.operation_type = "CARD_ACCESS_DENIED";
    if (auto audit_res = _audit_log->append_record(entry); !audit_res)
    {
        return std::unexpected(AuthError::DatabaseError);
    }
    return {};
}

bool AuthService::_within_access_window(const CardRecord& card) const
{
    if (!card.access_window_start.has_value() || !card.access_window_end.has_value())
    {
        return true;
    }
    const int hour = current_hour_utc();
    const int start = *card.access_window_start;
    const int end = *card.access_window_end;
    if (start <= end) { return hour >= start && hour <= end; }
    return hour >= start || hour <= end;
}
