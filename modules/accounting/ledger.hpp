/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: ledger.hpp
 * Interface for double-entry ledger entries.
 *
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <format>
#include <string>

inline std::string make_ledger_timestamp() {
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}",
        std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
}

class LedgerEntry {
public:
    std::string tx_id;

    std::string debit_account_id;
    std::string credit_account_id;

    std::int64_t amount_cents{};

    std::string timestamp;
    std::string operation_type;

    std::uint64_t nonce{};

    std::string prev_hash;
    std::string curr_hash;
    std::string hmac_signature;

    bool is_balanced() const noexcept {
        return !debit_account_id.empty()
            && !credit_account_id.empty()
            && debit_account_id != credit_account_id
            && amount_cents > 0;
    }
    
    std::string to_payload_string() const {
        return std::format("{}|{}|{}|{}|{}|{}|{}",
            tx_id, debit_account_id, credit_account_id,
            amount_cents, timestamp, operation_type, nonce);
    }
};
