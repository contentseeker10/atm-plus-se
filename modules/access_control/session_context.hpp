/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: session_context.hpp
 * Interface for user's session.
 *
 */

#pragma once

#include "enums.hpp"

#include <string>
#include <chrono>

class SessionContext {
    using clock = std::chrono::steady_clock;
public:
    SessionContext(std::string id, std::string card_num, UserRole rol)
    : session_id{std::move(id)}, card_number{std::move(card_num)}, role{rol}, last_activity{clock::now()} {}

    std::string session_id;
    std::string card_number;

    UserRole role{UserRole::None};

    clock::time_point last_activity;

    uint64_t current_nonce{};
public:
    void touch() noexcept { last_activity = clock::now(); }
    
    bool is_expired(std::chrono::seconds timeout) const noexcept { return (clock::now() - last_activity) >= timeout; };
    bool is_authenticated()                       const noexcept { return role != UserRole::None; }

    uint64_t next_nonce() noexcept { return ++current_nonce; };
};