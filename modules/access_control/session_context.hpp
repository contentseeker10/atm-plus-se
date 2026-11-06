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
public:
    std::string session_id;
    std::string card_number;

    UserRole role{UserRole::None};

    std::chrono::steady_clock::time_point last_activity;

    uint64_t current_nonce{};
public:
    void touch() noexcept;
    
    bool is_expired(std::chrono::seconds timeout) const noexcept;
    bool is_authenticated()                       const noexcept;

    uint64_t next_nonce() noexcept;
};