/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: cash_vault.hpp
 * Interface for the ATM's cash vault.
 *
 */

#pragma once

#include "cassette.hpp"
#include "vault_error.hpp"

#include <cstdint>
#include <expected>
#include <map>
#include <vector>

class CashVault {
public:
    explicit CashVault(std::map<Denomination, Cassette> cassettes);

    std::expected<std::map<Denomination, int>, VaultError> calculate_dispense(std::int64_t amount_cents) const;

    std::expected<void, VaultError> execute_dispense(const std::map<Denomination, int> &bills);

    std::expected<void, VaultError> deposit_bills(const std::vector<Denomination> &bills);

    std::int64_t get_total_cash_cents() const noexcept;

    const std::map<Denomination, Cassette> &get_cassettes() const noexcept;

    void open_door() noexcept;

    void close_door() noexcept;

    bool is_door_closed() const noexcept;

private:
    std::map<Denomination, Cassette> _cassettes;
    bool _is_door_closed{true};
};
