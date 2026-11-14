/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: cassette.cpp
 * Implementation of cassette bill accounting.
 *
 */

#include "cassette.hpp"

#include <format>
#include <stdexcept>

Cassette::Cassette(Denomination denomination, int count, int max_capacity)
    : _denomination(denomination), _current_count(count), _max_capacity(max_capacity)
{
    if (_max_capacity <= 0)
    {
        throw std::invalid_argument(
            std::format("max capacity must be positive, got {}", _max_capacity));
    }
    if (_current_count < 0 || _current_count > _max_capacity)
    {
        throw std::invalid_argument(
            std::format("count must be in range from 0 to {}, got {}", _max_capacity,
                        _current_count));
    }
}

Denomination Cassette::get_denomination() const noexcept { return _denomination; }

int Cassette::get_count() const noexcept { return _current_count; }

int Cassette::get_max_capacity() const noexcept { return _max_capacity; }

bool Cassette::can_accept(int count) const noexcept
{ return count > 0 && count <= _max_capacity - _current_count; }

bool Cassette::can_dispense(int count) const noexcept
{ return count > 0 && count <= _current_count; }

std::expected<void, VaultError> Cassette::add_bills(int count)
{
    if (count <= 0) return std::unexpected(VaultError::InvalidCount);
    if (!can_accept(count)) return std::unexpected(VaultError::CassetteFull);

    _current_count += count;
    return {};
}

std::expected<void, VaultError> Cassette::remove_bills(int count)
{
    if (count <= 0) return std::unexpected(VaultError::InvalidCount);
    if (!can_dispense(count)) return std::unexpected(VaultError::InsufficientBills);

    _current_count -= count;
    return {};
}
