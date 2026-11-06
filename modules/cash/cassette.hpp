/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: cassette.hpp
 * Interface for a single-denomination cassette.
 *
 */

#pragma once

#include "../access_control/enums.hpp"
#include "vault_error.hpp"
#include <expected>

class Cassette {
public:
    explicit Cassette(Denomination denomination, int count, int max_capacity);

    Denomination get_denomination() const noexcept;

    int get_count() const noexcept;

    bool can_accept(int count) const noexcept;

    bool can_dispense(int count) const noexcept;

    std::expected<void, VaultError> add_bills(int count);

    std::expected<void, VaultError> remove_bills(int count);

private:
    Denomination _denomination;
    int _current_count;
    int _max_capacity;
};
