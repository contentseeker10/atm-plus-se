/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: ledger.hpp
 * Interface for ledger entry.
 *
 */

#pragma once

#include <string>
#include <format>

class LedgerEntry {
public:
    int64_t entry_id{};

    std::string debit_account_id;
    std::string credit_account_id;

    int64_t amount_cents{};

    std::string timestamp;
    std::string operation_type;

    bool is_balanced() const noexcept { return (debit_account_id != credit_account_id) && (amount_cents > 0); };
    
    std::string to_payload_string() const {
        return std::format("Entry ID: {}\nDebit account ID: {}\nCredit account ID: {}\nAmount cents: {}\nTimestamp: {}\nOperation type: {}",
            entry_id, debit_account_id, credit_account_id, amount_cents, timestamp, operation_type);
    };
};