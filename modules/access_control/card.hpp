/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: card.hpp
 * Interface for banking card record.
 *
 */

#pragma once

#include "enums.hpp"

#include <string>
#include <optional>

struct CardRecord final {
    std::string card_number;
    std::string card_holder;
    std::string pin_hash;
    std::string salt;

    std::optional<std::string> account_id;
    
    UserRole role = UserRole::None;

    bool is_blocked{false};
    bool is_expired{false};

    int failed_attempts{};
};