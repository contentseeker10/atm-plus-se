/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: checking_account.hpp
 * Interface for the checking account with overdraft and overflow rules.
 *
 */

#pragma once

#include "account.hpp"

#include <optional>

class CheckingAccount final : public Account
{
public:
    CheckingAccount(std::string account_id, std::string client_id,
                    std::int64_t balance_cents, std::int64_t client_min_cents,
                    std::int64_t overflow_max_cents,
                    std::optional<std::string> linked_savings_id);

    std::expected<void, AccountError> withdraw(std::int64_t amount_cents) override;

    bool needs_overdraft_coverage(std::int64_t amount_cents) const noexcept;

    std::int64_t calculate_overflow_excess() const noexcept;

    std::int64_t get_client_min_cents() const noexcept { return _client_min_cents; }

    std::int64_t get_overflow_max_cents() const noexcept { return _overflow_max_cents; }

    const std::optional<std::string>& get_linked_savings_id() const noexcept
    { return _linked_savings_id; }

    bool validate_invariants() const noexcept override;

private:
    std::int64_t _client_min_cents;
    std::int64_t _overflow_max_cents;
    std::optional<std::string> _linked_savings_id;
};
