/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: audit.cpp
 * Implementation of secured audit logging.
 *
 */

#include "crypto.hpp"

#include "audit.hpp"

std::expected<std::string, CryptoError> AuditLog::append_record(const LedgerEntry& entry, uint64_t nonce) {
    if (!entry.is_balanced()) {
        return std::unexpected(CryptoError::EntryNotBalanced);
    }

    std::unique_lock lock(_mtx);

    std::string data{std::format("{}{}{}", _curr_chain_hash_head, entry.to_payload_string(), nonce)};
    std::string next_hash = crypto::sha256_hex(data);
    std::string hmac_sig = crypto::hmac_sha256_hex(next_hash, _hmac_secret_key);
    
    // TODO: INSERT entry + hashes to DB

    _curr_chain_hash_head = next_hash;

    return next_hash;
}

std::expected<bool, CryptoError> AuditLog::verify_integrity_chain() const {
    
}