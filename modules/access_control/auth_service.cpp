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
    return {};
}