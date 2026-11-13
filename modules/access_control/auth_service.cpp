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

std::expected<CardRecord, AuthError> AuthService::validate_card(std::string_view card) {
    // Card is inserted, but state is still Idle
    // TODO: validate that card is not expired/blocked before transition to CardInserted
    return {};
}


std::expected<UserRole, AuthError> AuthService::authenticate_card(std::string_view card, std::string_view pin) {
    // TODO: compare pin with pin_hash + salt from _record
    // TODO: block card if attempts to auth exceeded
    // TODO: return CardRecord.UserRole or Error
    return {};
}

std::expected<void, AuthError> AuthService::_block_card(std::string_view card) {
    // TODO: request to DB to block the card
    return {};
}

bool AuthService::_is_card_blocked(std::string_view card) const {
    // TODO: request to DB to know if card is blocked
    return {};
}

bool AuthService::_is_card_expired(std::string_view card) const {
    // TODO: request to DB to know if card is expired
    return {};
}