/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: account_error.hpp
 * Error codes for bank account operations.
 *
 */

#pragma once

enum class AccountError
{
    InvalidAmount,
    InsufficientFunds,
    BalanceOverflow,
    NoOverdraftProtection,
};
