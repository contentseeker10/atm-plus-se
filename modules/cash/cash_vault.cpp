/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: cash_vault.cpp
 * Implementation of the ATM's cash vault.
 *
 */

#include "cash_vault.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace {
    constexpr bool can_be_composed(const std::int64_t amount_cents) noexcept {
        return amount_cents % 1000 == 0;
    }

    constexpr std::int64_t to_cents(Denomination denomination) noexcept {
        return static_cast<std::int64_t>(denomination);
    }
}

CashVault::CashVault(std::map<Denomination, Cassette> cassettes) : _cassettes(std::move(cassettes)) {
    for (auto it = _cassettes.cbegin(); it != _cassettes.cend(); ++it) {
        if (it->first != it->second.get_denomination()) {
            throw std::invalid_argument("cassette denomination does not match its slot");
        }
    }
}

std::expected<std::map<Denomination, int>, VaultError> CashVault::calculate_dispense(std::int64_t amount_cents) const {
    if (!_is_door_closed) return std::unexpected(VaultError::DoorOpen);
    if (amount_cents <= 0 || !can_be_composed(amount_cents)) return std::unexpected(VaultError::InvalidAmount);
    if (amount_cents > get_total_cash_cents()) return std::unexpected(VaultError::InsufficientCash);

    std::map<Denomination, int> plan;
    std::int64_t remaining = amount_cents;
    for (auto it = _cassettes.crbegin(); it != _cassettes.crend(); ++it) {
        const std::int64_t bill_cents = to_cents(it->first);
        const std::int64_t bills = std::min<std::int64_t>(it->second.get_count(), remaining / bill_cents);
        if (bills > 0) {
            plan[it->first] = static_cast<int>(bills);
            remaining -= bills * bill_cents;
        }
    }

    if (remaining != 0) return std::unexpected(VaultError::NoExactCombination);
    return plan;
}

std::expected<void, VaultError> CashVault::execute_dispense(const std::map<Denomination, int> &bills) {
    if (!_is_door_closed) return std::unexpected(VaultError::DoorOpen);
    if (bills.empty()) return std::unexpected(VaultError::InvalidCount);

    for (auto it = bills.cbegin(); it != bills.cend(); ++it) {
        if (it->second <= 0) return std::unexpected(VaultError::InvalidCount);

        const auto cassette = _cassettes.find(it->first);
        if (cassette == _cassettes.end()) return std::unexpected(VaultError::MissingCassette);
        if (!cassette->second.can_dispense(it->second)) return std::unexpected(VaultError::InsufficientBills);
    }

    for (auto it = bills.cbegin(); it != bills.cend(); ++it) {
        _cassettes.find(it->first)->second.remove_bills(it->second).value();
    }
    return {};
}

std::expected<void, VaultError> CashVault::deposit_bills(const std::vector<Denomination> &bills) {
    if (!_is_door_closed) return std::unexpected(VaultError::DoorOpen);
    if (bills.empty()) return std::unexpected(VaultError::InvalidCount);

    std::map<Denomination, int> counts;
    for (auto it = bills.cbegin(); it != bills.cend(); ++it) {
        ++counts[*it];
    }

    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        const auto cassette = _cassettes.find(it->first);
        if (cassette == _cassettes.end()) return std::unexpected(VaultError::MissingCassette);
        if (!cassette->second.can_accept(it->second)) return std::unexpected(VaultError::CassetteFull);
    }

    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        _cassettes.find(it->first)->second.add_bills(it->second).value();
    }
    return {};
}

std::int64_t CashVault::get_total_cash_cents() const noexcept {
    std::int64_t total = 0;
    for (auto it = _cassettes.cbegin(); it != _cassettes.cend(); ++it) {
        total += it->second.get_count() * to_cents(it->first);
    }
    return total;
}

const std::map<Denomination, Cassette> &CashVault::get_cassettes() const noexcept { return _cassettes; }

void CashVault::open_door() noexcept { _is_door_closed = false; }

void CashVault::close_door() noexcept { _is_door_closed = true; }

bool CashVault::is_door_closed() const noexcept { return _is_door_closed; }
