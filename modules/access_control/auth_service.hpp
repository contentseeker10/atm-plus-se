/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: auth_service.hpp
 * Interface for auth service.
 *
 */

#pragma once

#include "enums.hpp"

#include <string_view>
#include <expected>
#include <memory>

class DatabaseManager;
class AuditLog;
class AuthError;

class AuthService {
public:
    std::expected<UserRole, AuthError> authenticate_card(std::string_view card, std::string_view pin);
    std::expected<bool, AuthError>     block_card(std::string_view card);
    std::expected<void, AuthError>     reset_failed_attempts(std::string_view card);
private:
    static constexpr int kMaxFailedAttempts{3};

    std::shared_ptr<DatabaseManager> _db;
    std::shared_ptr<AuditLog>        _audit_log;
};