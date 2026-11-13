/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: audit.hpp
 * Interface for the tamper-evident audit log
 *
 */

#pragma once

#include "db.hpp"
#include "enums.hpp"

#include "ledger.hpp"

#include <atomic>
#include <expected>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct ChainVerification
{
    bool ok{};
    std::size_t first_broken_index{};
    std::string detail;
};

class AuditLog
{
public:
    AuditLog(std::shared_ptr<DatabaseManager> db, std::string hmac_secret_key);
    ~AuditLog() = default;

    AuditLog(const AuditLog&) = delete;
    AuditLog& operator=(const AuditLog&) = delete;
    AuditLog(AuditLog&&) = delete;
    AuditLog& operator=(AuditLog&&) = delete;

    std::expected<std::string, CryptoError> append_record(LedgerEntry& entry);

    std::expected<void, CryptoError> append_batch(std::vector<LedgerEntry>& entries);

    std::expected<void, CryptoError> load_from_database();

    std::expected<ChainVerification, CryptoError> verify_integrity_chain() const;

    std::vector<LedgerEntry> entries() const;

    std::shared_ptr<const std::string> get_last_hash() const noexcept;
    std::size_t size() const;

    // Simulate an attacker editing a SQLite row, TEST ONLY.
    void debug_tamper_amount(std::size_t index, std::int64_t new_amount);

private:
    std::shared_ptr<DatabaseManager> _db; // Optional, null = memory-only log

    std::string _hmac_secret_key;

    std::atomic<std::shared_ptr<const std::string>> _chain_head;

    std::vector<LedgerEntry> _entries;

    mutable std::mutex _mtx;
};
