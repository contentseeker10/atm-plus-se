/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: account.cpp
 * Implementation of the abstract bank account.
 *
 */

#include "account.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

Account::Account(std::string account_id, std::string client_id, std::int64_t balance_cents)
    : _account_id(std::move(account_id)), _client_id(std::move(client_id)), _balance_cents(balance_cents) {
    if (_account_id.empty()) throw std::invalid_argument("account id must not be empty");
    if (_client_id.empty()) throw std::invalid_argument("client id must not be empty");
}

Account::~Account() noexcept = default;

const std::string& Account::get_account_id() const noexcept { return _account_id; }

std::int64_t Account::get_balance() const noexcept { return _balance_cents; }

std::expected<void, AccountError> Account::deposit(std::int64_t amount_cents) {
    if (amount_cents <= 0) return std::unexpected(AccountError::InvalidAmount);
    if (amount_cents > std::numeric_limits<std::int64_t>::max() - _balance_cents) {
        return std::unexpected(AccountError::BalanceOverflow);
    }

    _balance_cents += amount_cents;
    return {};
}
