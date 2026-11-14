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
    CardRecord(std::string card_number, std::string card_holder, std::string pin, std::string sal, 
            std::optional<std::string> account, UserRole rol, bool blocked, bool expired, int failed_atmps) 
    : number{std::move(card_number)},
      holder{std::move(card_holder)},
      pin_hash{std::move(pin)},
      salt{std::move(sal)},
      account_id{std::move(account)},
      role{rol},
      is_blocked{blocked},
      is_expired{expired},
      failed_attempts{failed_atmps}
    {}

    CardRecord(std::string card_number, bool blocked, bool expired)
    : number{std::move(card_number)},
      is_blocked{blocked},
      is_expired{expired}
    {}

    std::string number;
    std::string holder;
    std::string pin_hash;
    std::string salt;

    std::optional<std::string> account_id;
    
    UserRole role = UserRole::None;

    bool is_blocked{false};
    bool is_expired{false};

    int failed_attempts{};
};