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

#include "db.hpp"
#include "enums.hpp"

#include "card.hpp"

#include "accounting/audit.hpp"

#include <string_view>
#include <expected>
#include <memory>

class AuthService {
public:
    AuthService(std::shared_ptr<DatabaseManager> db, std::shared_ptr<AuditLog> log)
        : _db{std::move(db)}, _audit_log{std::move(log)}
    { }

    std::expected<CardRecord, AuthError> validate_card(std::string_view card);
    std::expected<UserRole, AuthError>   authenticate_card(std::string_view card, std::string_view pin);
private:
    static constexpr int kMaxFailedAttempts{3};

    CardRecord _record;
    
    std::shared_ptr<DatabaseManager> _db;
    std::shared_ptr<AuditLog> _audit_log;

    std::expected<void, AuthError> _block_card(std::string_view card);

    bool _is_card_blocked(std::string_view card) const;
    bool _is_card_expired(std::string_view card) const;
};