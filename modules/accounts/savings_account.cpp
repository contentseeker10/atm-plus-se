/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: savings_account.cpp
 * Implementation of the savings account.
 *
 */

#include "savings_account.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

SavingsAccount::SavingsAccount(std::string account_id, std::string client_id,
                               std::int64_t balance_cents, double annual_interest_rate,
                               std::int64_t minimum_balance_cents)
    : Account(std::move(account_id), std::move(client_id), balance_cents),
      _annual_interest_rate(annual_interest_rate),
      _interest_rate_bps(std::isfinite(annual_interest_rate)
                             ? static_cast<std::int32_t>(
                                   std::llround(annual_interest_rate * 10000.0))
                             : 0),
      _minimum_balance_cents(minimum_balance_cents)
{
    if (!SavingsAccount::validate_invariants())
    {
        throw std::invalid_argument("savings account violates its invariants");
    }
}

std::expected<void, AccountError> SavingsAccount::withdraw(std::int64_t amount_cents)
{
    if (amount_cents <= 0) return std::unexpected(AccountError::InvalidAmount);
    if (amount_cents > _balance_cents - _minimum_balance_cents)
    {
        return std::unexpected(AccountError::InsufficientFunds);
    }

    _balance_cents -= amount_cents;
    return {};
}

std::expected<void, AccountError> SavingsAccount::cover_overdraft(
    std::int64_t deficit_cents)
{ return withdraw(deficit_cents); }

std::int64_t SavingsAccount::calculate_interest_cents(int days) const noexcept
{
    if (days <= 0 || _balance_cents <= 0 || _interest_rate_bps <= 0) { return 0; }
    return (_balance_cents * static_cast<std::int64_t>(_interest_rate_bps) *
            static_cast<std::int64_t>(days)) /
           (10000LL * 365LL);
}

std::expected<std::int64_t, AccountError> SavingsAccount::apply_interest(int days)
{
    if (days <= 0) return std::unexpected(AccountError::InvalidAmount);
    const std::int64_t interest = calculate_interest_cents(days);
    if (interest > 0)
    {
        if (auto d = deposit(interest); !d) return std::unexpected(d.error());
    }
    return interest;
}

bool SavingsAccount::validate_invariants() const noexcept
{
    if (!std::isfinite(_annual_interest_rate) || _annual_interest_rate < 0.0)
        return false;
    if (_minimum_balance_cents < 0 || _balance_cents < _minimum_balance_cents)
        return false;
    return true;
}
