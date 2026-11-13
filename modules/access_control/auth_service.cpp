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

std::expected<UserRole, AuthError> AuthService::authenticate_card(std::string_view card, std::string_view pin) {
    // TODO: request to DB to get data
    return {};
}

std::expected<bool, AuthError> AuthService::block_card(std::string_view card) {
    // TODO: request to DB to block the card
    return {};
}

std::expected<bool, AuthError> AuthService::is_card_blocked(std::string_view card) {
    // TODO: request to DB to know if card is blocked
    return {};
}

std::expected<void, AuthError> AuthService::reset_failed_attempts(std::string_view card)
{
    // TODO: UPDATE attempts in DB
    return {};
}