/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: audit.hpp
 * Interface for secured audit logging.
 *
 */

#pragma once

#include "enums.hpp"
#include "db.hpp"

#include "ledger.hpp"

#include <mutex>

class AuditLog {
public:
    AuditLog(std::shared_ptr<DatabaseManager> db, std::string hmac_secret_key) 
    : _db{std::move(db)}, _hmac_secret_key{std::move(hmac_secret_key)}, _curr_chain_hash_head{std::string(64, '0')} {} 
    ~AuditLog() = default;

    AuditLog(const AuditLog&)            = delete;
    AuditLog& operator=(const AuditLog&) = delete;
    AuditLog(AuditLog&&)                 = delete;
    AuditLog& operator=(AuditLog&&)      = delete;

    std::expected<std::string, CryptoError> append_record(const LedgerEntry& entry, uint64_t nonce);
    std::expected<bool, CryptoError> verify_integrity_chain() const;
    
    std::string get_last_hash() const noexcept { return _curr_chain_hash_head; };
private:
    std::shared_ptr<DatabaseManager> _db;
    
    std::string _curr_chain_hash_head;
    std::string _hmac_secret_key;

    mutable std::mutex _mtx;
};