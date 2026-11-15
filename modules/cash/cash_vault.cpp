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
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
    constexpr std::int64_t kBillUnitCents = 1000;

    constexpr bool can_be_composed(const std::int64_t amount_cents) noexcept
    { return amount_cents % kBillUnitCents == 0; }

    constexpr std::int64_t to_cents(Denomination denomination) noexcept
    { return static_cast<std::int64_t>(denomination); }

    constexpr int kUnreachable = std::numeric_limits<int>::max();

    struct BillBundle
    {
        Denomination denomination;
        int bills;
        std::size_t units;
    };

    std::vector<BillBundle> split_into_bundles(
        const std::map<Denomination, Cassette>& cassettes, std::size_t target_units)
    {
        std::vector<BillBundle> bundles;
        bundles.reserve(cassettes.size() * 8);
        for (auto it = cassettes.cbegin(); it != cassettes.cend(); ++it)
        {
            const auto bill_units =
                static_cast<std::size_t>(to_cents(it->first) / kBillUnitCents);
            const auto usable = static_cast<int>(
                std::min<std::size_t>(static_cast<std::size_t>(it->second.get_count()),
                                      target_units / bill_units));

            int left = usable;
            for (int chunk = 1; left > 0; chunk *= 2)
            {
                const int bills = std::min(chunk, left);
                bundles.push_back(
                    {it->first, bills, bill_units * static_cast<std::size_t>(bills)});
                left -= bills;
            }
        }
        return bundles;
    }

    std::optional<std::map<Denomination, int>> find_fewest_bills(
        const std::map<Denomination, Cassette>& cassettes, std::size_t target_units)
    {
        const auto bundles = split_into_bundles(cassettes, target_units);

        const std::size_t stride = target_units + 1;
        std::vector<int> fewest(stride, kUnreachable);
        fewest[0] = 0;
        std::vector<std::uint8_t> taken(bundles.size() * stride, 0);

        for (std::size_t i = 0; i < bundles.size(); ++i)
        {
            const auto& bundle = bundles[i];
            const std::size_t row_offset = i * stride;
            for (std::size_t value = target_units; value >= bundle.units; --value)
            {
                const int previous = fewest[value - bundle.units];
                if (previous != kUnreachable && previous + bundle.bills < fewest[value])
                {
                    fewest[value] = previous + bundle.bills;
                    taken[row_offset + value] = 1;
                }
            }
        }

        if (fewest[target_units] == kUnreachable) return std::nullopt;

        std::map<Denomination, int> plan;
        std::size_t remaining = target_units;
        for (std::size_t i = bundles.size(); i-- > 0;)
        {
            if (taken[i * stride + remaining] == 0) continue;
            plan[bundles[i].denomination] += bundles[i].bills;
            remaining -= bundles[i].units;
        }
        return plan;
    }
} // namespace

CashVault::CashVault(std::map<Denomination, Cassette> cassettes)
    : _cassettes(std::move(cassettes))
{
    for (auto it = _cassettes.cbegin(); it != _cassettes.cend(); ++it)
    {
        if (it->first != it->second.get_denomination())
        {
            throw std::invalid_argument("cassette denomination does not match its slot");
        }
    }
}

std::expected<std::map<Denomination, int>, VaultError> CashVault::calculate_dispense(
    std::int64_t amount_cents) const
{
    if (!_is_door_closed) return std::unexpected(VaultError::DoorOpen);
    if (amount_cents <= 0 || !can_be_composed(amount_cents))
        return std::unexpected(VaultError::InvalidAmount);
    if (amount_cents > get_total_cash_cents())
        return std::unexpected(VaultError::InsufficientCash);

    auto plan = find_fewest_bills(
        _cassettes, static_cast<std::size_t>(amount_cents / kBillUnitCents));
    if (!plan) return std::unexpected(VaultError::NoExactCombination);
    return std::move(*plan);
}

std::expected<void, VaultError> CashVault::execute_dispense(
    const std::map<Denomination, int>& bills)
{
    if (!_is_door_closed) return std::unexpected(VaultError::DoorOpen);
    if (bills.empty()) return std::unexpected(VaultError::InvalidCount);

    for (auto it = bills.cbegin(); it != bills.cend(); ++it)
    {
        if (it->second <= 0) return std::unexpected(VaultError::InvalidCount);

        const auto cassette = _cassettes.find(it->first);
        if (cassette == _cassettes.end())
            return std::unexpected(VaultError::MissingCassette);
        if (!cassette->second.can_dispense(it->second))
            return std::unexpected(VaultError::InsufficientBills);
    }

    for (auto it = bills.cbegin(); it != bills.cend(); ++it)
    {
        _cassettes.find(it->first)->second.remove_bills(it->second).value();
    }
    return {};
}

std::expected<void, VaultError> CashVault::deposit_bills(
    const std::vector<Denomination>& bills)
{
    if (!_is_door_closed) return std::unexpected(VaultError::DoorOpen);
    if (bills.empty()) return std::unexpected(VaultError::InvalidCount);

    std::map<Denomination, int> counts;
    for (auto it = bills.cbegin(); it != bills.cend(); ++it) { ++counts[*it]; }

    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
    {
        const auto cassette = _cassettes.find(it->first);
        if (cassette == _cassettes.end())
            return std::unexpected(VaultError::MissingCassette);
        if (!cassette->second.can_accept(it->second))
            return std::unexpected(VaultError::CassetteFull);
    }

    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
    {
        _cassettes.find(it->first)->second.add_bills(it->second).value();
    }
    return {};
}

std::expected<void, VaultError> CashVault::replenish(Denomination denomination, int count)
{
    if (_is_door_closed) return std::unexpected(VaultError::DoorClosed);

    const auto cassette = _cassettes.find(denomination);
    if (cassette == _cassettes.end()) return std::unexpected(VaultError::MissingCassette);

    return cassette->second.add_bills(count);
}

std::expected<int, VaultError> CashVault::replace_cassette(Denomination denomination,
                                                           int new_count)
{
    if (_is_door_closed) return std::unexpected(VaultError::DoorClosed);

    const auto cassette = _cassettes.find(denomination);
    if (cassette == _cassettes.end()) return std::unexpected(VaultError::MissingCassette);

    const int previous = cassette->second.get_count();
    if (auto r = cassette->second.set_bills(new_count); !r)
    {
        return std::unexpected(r.error());
    }
    return new_count - previous;
}

std::int64_t CashVault::get_total_cash_cents() const noexcept
{
    std::int64_t total = 0;
    for (auto it = _cassettes.cbegin(); it != _cassettes.cend(); ++it)
    {
        total += it->second.get_count() * to_cents(it->first);
    }
    return total;
}

const std::map<Denomination, Cassette>& CashVault::get_cassettes() const noexcept
{ return _cassettes; }

void CashVault::open_door() noexcept { _is_door_closed = false; }

void CashVault::close_door() noexcept { _is_door_closed = true; }

bool CashVault::is_door_closed() const noexcept { return _is_door_closed; }
