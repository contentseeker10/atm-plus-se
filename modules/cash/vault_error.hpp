/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: vault_error.hpp
 * Error codes for cash vault operations.
 *
 */

#pragma once

enum class VaultError {
    InvalidCount,
    CassetteFull,
    InsufficientBills,
    DoorOpen,
    InvalidAmount,
    NoExactCombination,
    InsufficientCash,
    MissingCassette,
};
