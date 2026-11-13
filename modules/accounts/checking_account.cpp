/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: checking_account.cpp
 * Implementation of the checking account.
 *
 */

#include "checking_account.hpp"

#include <stdexcept>
#include <utility>

CheckingAccount::CheckingAccount(std::string account_id, std::string client_id,
                                 std::int64_t balance_cents,
                                 std::int64_t client_min_cents,
                                 std::int64_t overflow_max_cents,
                                 std::optional<std::string> linked_savings_id)
    : Account(std::move(account_id), std::move(client_id), balance_cents),
      _client_min_cents(client_min_cents), _overflow_max_cents(overflow_max_cents),
      _linked_savings_id(std::move(linked_savings_id))
{
    if (!CheckingAccount::validate_invariants())
    {
        throw std::invalid_argument("checking account violates its invariants");
    }
}

std::expected<void, AccountError> CheckingAccount::withdraw(std::int64_t amount_cents)
{
    if (amount_cents <= 0) return std::unexpected(AccountError::InvalidAmount);
    if (amount_cents > _balance_cents)
        return std::unexpected(AccountError::InsufficientFunds);
    if (needs_overdraft_coverage(amount_cents) && !_linked_savings_id)
    {
        return std::unexpected(AccountError::NoOverdraftProtection);
    }

    _balance_cents -= amount_cents;
    return {};
}

bool CheckingAccount::needs_overdraft_coverage(std::int64_t amount_cents) const noexcept
{ return amount_cents > 0 && amount_cents > _balance_cents - _client_min_cents; }

std::int64_t CheckingAccount::calculate_overflow_excess() const noexcept
{
    return _balance_cents > _overflow_max_cents ? _balance_cents - _overflow_max_cents
                                                : 0;
}

bool CheckingAccount::validate_invariants() const noexcept
{
    if (_client_min_cents < 0 || _client_min_cents > _overflow_max_cents) return false;
    if (_balance_cents < _client_min_cents || _balance_cents > _overflow_max_cents)
        return false;
    if (_linked_savings_id &&
        (_linked_savings_id->empty() || *_linked_savings_id == _account_id))
        return false;
    return true;
}
