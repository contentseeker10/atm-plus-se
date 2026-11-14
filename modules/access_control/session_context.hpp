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

#include "card.hpp"

#include <chrono>
#include <string>

class SessionContext
{
    using clock = std::chrono::steady_clock;

public:
    SessionContext(std::string id, CardRecord card)
        : session_id{std::move(id)}, record{std::move(card)}
    { }

    std::string session_id;

    CardRecord record;

    clock::time_point last_activity{clock::now()};

    uint64_t current_nonce{};

public:
    void touch() noexcept { last_activity = clock::now(); }

    bool is_expired(std::chrono::seconds timeout) const noexcept
    { return (clock::now() - last_activity) >= timeout; };
    bool is_authenticated() const noexcept { return record.role != UserRole::None; }

    uint64_t next_nonce() noexcept { return ++current_nonce; };
};