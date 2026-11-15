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
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct ChainVerification
{
    bool ok{};
    std::size_t first_broken_index{};
    std::string detail;
    std::size_t checked_records{};
};

class AuditLog
{
public:
    AuditLog(std::shared_ptr<DatabaseManager> db, std::string hmac_secret_key);
    ~AuditLog() noexcept;

    AuditLog(const AuditLog&) = delete;
    AuditLog& operator=(const AuditLog&) = delete;
    AuditLog(AuditLog&&) = delete;
    AuditLog& operator=(AuditLog&&) = delete;

    std::expected<std::string, CryptoError> append_record(LedgerEntry& entry);

    std::expected<void, CryptoError> append_batch(std::vector<LedgerEntry>& entries);

    std::expected<void, CryptoError> load_from_database();

    std::expected<ChainVerification, CryptoError> verify_integrity_chain() const;
    bool is_entry_tampered(const LedgerEntry& entry) const;

    std::vector<LedgerEntry> entries() const;
    std::vector<LedgerEntry> entries_from(std::size_t start_index) const;
    std::vector<LedgerEntry> entries_page(std::size_t offset, std::size_t limit) const;
    void for_each_entry(const std::function<void(const LedgerEntry&)>& fn) const;

    std::shared_ptr<const std::string> get_last_hash() const noexcept;
    std::size_t size() const;

    void debug_tamper_amount(std::size_t index, std::int64_t new_amount);
    void restore_tampered_entries();

private:
    bool is_entry_tampered_unlocked(const LedgerEntry& entry) const;

    std::shared_ptr<DatabaseManager> _db;

    std::string _hmac_secret_key;

    std::atomic<std::shared_ptr<const std::string>> _chain_head;

    std::vector<LedgerEntry> _entries;
    std::unordered_map<std::size_t, std::int64_t> _tampered_original_amounts;

    mutable std::mutex _mtx;
};
