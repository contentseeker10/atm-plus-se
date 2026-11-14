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
    std::expected<void, AuthError>       authenticate_card(std::string_view card, std::string_view pin);
private:
    static constexpr int kMaxFailedAttempts{3};
    
    std::shared_ptr<DatabaseManager> _db;
    std::shared_ptr<AuditLog> _audit_log;

    std::expected<CardRecord, AuthError> _load_card(std::string_view card);
    std::expected<void, AuthError>       _block_card(std::string_view card);
    
    std::expected<int, AuthError>        _inc_failed_attempts(std::string_view card);
    std::expected<void, AuthError>       _reset_failed_attempts(std::string_view card);

    bool _is_card_blocked(const CardRecord& card)    const { return card.is_blocked; }
    bool _is_card_expired(const CardRecord& card)    const { return card.is_expired; }
    int _get_failed_attempts(const CardRecord& card) const { return card.failed_attempts; }
};