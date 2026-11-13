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

std::expected<UserRole, AuthError> AuthService::authenticate_card(std::string_view pin) {
    // TODO: compare pin with pin_hash + salt from _record
    // TODO: increment failed_attempts count if wrong pin 
    // TODO: block card if attempts to auth exceeded
    // TODO: return CardRecord.UserRole or Error
    return {};
}

std::expected<CardRecord, AuthError> AuthService::_load_card(std::string_view card) {
    auto record = _db->load_card(card);
    if (!record) {
        return std::unexpected(AuthError::DatabaseError);
    }
    return record.value();
}

std::expected<void, AuthError> AuthService::_block_card() {
    // TODO: request to DB to block the card
    return {};
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