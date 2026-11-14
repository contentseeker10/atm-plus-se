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

std::expected<CardRecord, AuthError> AuthService::validate_card(std::string_view card) {
    auto record = _load_card(card);
    if (_is_card_blocked(card)) {
        return std::unexpected(AuthError::CardBlocked);
    }
    if (_is_card_expired(card)) {
        return std::unexpected(AuthError::CardExpired);
    }
    return record.value();
}

std::expected<void, AuthError> AuthService::authenticate_card(std::string_view card, std::string_view pin) {
    auto record = _load_card(card);
    auto hash = crypto::sha256_hex(pin);
    if (!crypto::constant_time_compare(hash, record->pin_hash)) {
        auto inc_failed = _inc_failed_attempts(card);
        if (!inc_failed) {
            return std::unexpected(inc_failed.error());
        }
        if (inc_failed.value() >= 3) {
            _block_card(card);
            return std::unexpected(AuthError::MaxAttemptsExceeded);
        }
        return std::unexpected(AuthError::InvalidPin);
    }
    return {};
}

std::expected<CardRecord, AuthError> AuthService::_load_card(std::string_view card) {
    auto record = _db->load_card(card);
    if (!record) {
        return std::unexpected(AuthError::DatabaseError);
    }
    return record.value();
}

std::expected<void, AuthError> AuthService::_block_card(std::string_view card) {
    auto result = _db->block_card(card);
    if (!result) {
        return std::unexpected(AuthError::DatabaseError);
    }
    return {};
}

std::expected<int, AuthError> AuthService::_inc_failed_attempts(std::string_view card) {
    auto current_failed = _db->get_failed_attempts(card);
    if (!current_failed) {
        return std::unexpected(AuthError::DatabaseError);
    }

    auto attempts = current_failed.value();

    if (attempts > 3) {
        return std::unexpected(AuthError::MaxAttemptsExceeded);
    }

    _db->set_failed_attempts(card, ++attempts);
    return attempts;
}

bool AuthService::_is_card_blocked(std::string_view card) const {
    auto record = _db->load_card(card);
    return record->is_blocked;
}

bool AuthService::_is_card_expired(std::string_view card) const {
    auto record = _db->load_card(card);
    return record->is_expired;
}

int AuthService::_get_failed_attempts(std::string_view card) const {
    auto record = _db->load_card(card);
    return record->failed_attempts;
}