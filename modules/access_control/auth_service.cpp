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

std::expected<CardRecord, AuthError> AuthService::validate_card(std::string_view card, bool auth) {
    auto record = _load_card(card, !auth);
    if (!record) {
        return std::unexpected(record.error());
    }
    if (_is_card_blocked(record.value())) {
        return std::unexpected(AuthError::CardBlocked);
    }
    if (_is_card_expired(record.value())) {
        return std::unexpected(AuthError::CardExpired);
    }
    return record.value();
}

std::expected<void, AuthError> AuthService::authenticate_card(std::string_view card, std::string_view pin) {
    auto record = validate_card(card, true);
    auto hash = crypto::sha256_hex(pin, record->salt);
    if (!crypto::constant_time_compare(hash, record->pin_hash)) {
        auto inc_failed = _inc_failed_attempts(card);
        if (!inc_failed) {
            return std::unexpected(inc_failed.error());
        }
        if (inc_failed.value() >= kMaxFailedAttempts) {
            auto block_result = _block_card(card);
            if (!block_result) {
                return std::unexpected(block_result.error());
            }
            return std::unexpected(AuthError::MaxAttemptsExceeded);
        }
        return std::unexpected(AuthError::InvalidPin);
    }
    auto reset = _reset_failed_attempts(card);
    if (!reset) {
        return std::unexpected(reset.error());
    }
    return {};
}

std::expected<CardRecord, AuthError> AuthService::_load_card(std::string_view card, bool validate_only) {
    auto record = _db->load_card(card, validate_only);
    if (!record) {
        return std::unexpected(to_auth_error(record.error()));
    }
    return record.value();
}

std::expected<void, AuthError> AuthService::_block_card(std::string_view card) {
    auto result = _db->block_card(card);
    if (!result) {
        return std::unexpected(to_auth_error(result.error()));
    }
    return {};
}

std::expected<int, AuthError> AuthService::_inc_failed_attempts(std::string_view card) {
    auto current_failed = _db->get_failed_attempts(card);
    if (!current_failed) {
        return std::unexpected(to_auth_error(current_failed.error()));
    }

    auto attempts = current_failed.value();

    if (attempts == kMaxFailedAttempts) {
        return std::unexpected(AuthError::MaxAttemptsExceeded);
    }

    auto result = _db->set_failed_attempts(card, ++attempts);
    if (!result) {
        return std::unexpected(to_auth_error(result.error()));
    }
    return attempts;
}

std::expected<void, AuthError> AuthService::_reset_failed_attempts(std::string_view card) {
    auto result = _db->set_failed_attempts(card, 0);
    if (!result) {
        return std::unexpected(to_auth_error(result.error()));
    }
    return {};
}