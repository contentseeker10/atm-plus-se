/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: account.hpp
 * Interface for the abstract bank account.
 *
 */

#pragma once

#include "account_error.hpp"

#include <cstdint>
#include <expected>
#include <string>

class Account
{
public:
    virtual ~Account() noexcept;

    const std::string& get_account_id() const noexcept;

    const std::string& get_client_id() const noexcept;

    std::int64_t get_balance() const noexcept;

    std::expected<void, AccountError> deposit(std::int64_t amount_cents);

    virtual std::expected<void, AccountError> withdraw(std::int64_t amount_cents) = 0;

    virtual bool validate_invariants() const noexcept = 0;

protected:
    Account(std::string account_id, std::string client_id, std::int64_t balance_cents);

    Account(const Account&) = default;

    Account(Account&&) = default;

    Account& operator=(const Account&) = default;

    Account& operator=(Account&&) = default;

    std::string _account_id;
    std::string _client_id;
    std::int64_t _balance_cents;
};
