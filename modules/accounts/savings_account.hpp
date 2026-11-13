/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: savings_account.hpp
 * Interface for the savings account backing overdraft protection.
 *
 */

#pragma once

#include "account.hpp"

class SavingsAccount final : public Account
{
public:
    SavingsAccount(std::string account_id, std::string client_id,
                   std::int64_t balance_cents, double annual_interest_rate,
                   std::int64_t minimum_balance_cents);

    std::expected<void, AccountError> withdraw(std::int64_t amount_cents) override;

    std::expected<void, AccountError> cover_overdraft(std::int64_t deficit_cents);

    bool validate_invariants() const noexcept override;

private:
    double _annual_interest_rate;
    std::int64_t _minimum_balance_cents;
};
