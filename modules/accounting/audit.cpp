/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: audit.cpp
 * Implementation of the tamper-evident audit log.
 *
 */

#include "audit.hpp"

#include "crypto.hpp"

#include "storage/operations.hpp"
#include "storage/sqlite_utils.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

AuditLog::AuditLog(std::shared_ptr<DatabaseManager> db, std::string hmac_secret_key)
    : _db{std::move(db)}, _hmac_secret_key{std::move(hmac_secret_key)},
      _chain_head{std::make_shared<const std::string>(64, '0')}
{ }

AuditLog::~AuditLog() noexcept
{
    crypto::secure_zero(_hmac_secret_key);
}

std::expected<void, CryptoError> AuditLog::load_from_database()
{
    if (!_db) { return std::unexpected(CryptoError::DatabaseError); }

    std::lock_guard lock(_mtx);

    if (!_entries.empty() ||
        *_chain_head.load(std::memory_order_acquire) != std::string(64, '0'))
    {
        return std::unexpected(CryptoError::DatabaseError);
    }

    std::vector<LedgerEntry> loaded;
    const auto q = _db->execute_transaction(
        [&loaded](sqlite3* raw)
        {
            auto rows = storage::load_ledger_entries(raw);
            if (!rows) { return false; }
            loaded = std::move(*rows);
            return true;
        });
    if (!q) { return std::unexpected(CryptoError::DatabaseError); }

    std::unordered_map<std::string, LedgerEntry> by_prev;
    by_prev.reserve(loaded.size() * 2);
    for (auto& e : loaded)
    {
        if (!by_prev.emplace(e.prev_hash, std::move(e)).second)
        {
            return std::unexpected(CryptoError::HashMismatch);
        }
    }

    try
    {
        std::vector<LedgerEntry> ordered;
        ordered.reserve(loaded.size());
        std::string prev(64, '0');
        std::shared_ptr<const std::string> head =
            std::make_shared<const std::string>(64, '0');

        for (;;)
        {
            auto it = by_prev.find(prev);
            if (it == by_prev.end()) { break; }

            LedgerEntry& e = it->second;
            const std::string expected_hash =
                crypto::sha256_hex(e.prev_hash + e.to_payload_string());
            if (!crypto::constant_time_compare(e.curr_hash, expected_hash))
            {
                return std::unexpected(CryptoError::HashMismatch);
            }
            const std::string expected_hmac =
                crypto::hmac_sha256_hex(e.curr_hash, _hmac_secret_key);
            if (!crypto::constant_time_compare(e.hmac_signature, expected_hmac))
            {
                return std::unexpected(CryptoError::HmacVerificationFailed);
            }

            prev = e.curr_hash;
            head = std::make_shared<const std::string>(e.curr_hash);
            ordered.push_back(std::move(e));
        }

        if (ordered.size() != loaded.size())
        {
            return std::unexpected(CryptoError::HashMismatch);
        }

        _entries = std::move(ordered);
        _chain_head.store(std::move(head), std::memory_order_release);
        return {};
    }
    catch (...)
    {
        return std::unexpected(CryptoError::InternalError);
    }
}

std::expected<std::string, CryptoError> AuditLog::append_record(LedgerEntry& entry)
{
    std::vector<LedgerEntry> batch;
    batch.push_back(entry);
    if (auto r = append_batch(batch); !r) { return std::unexpected(r.error()); }
    entry = std::move(batch.front());
    return entry.curr_hash;
}

std::expected<void, CryptoError> AuditLog::append_batch(std::vector<LedgerEntry>& entries)
{
    if (entries.empty()) { return {}; }

    for (const auto& e : entries)
    {
        if (!e.is_balanced()) { return std::unexpected(CryptoError::EntryNotBalanced); }
    }

    std::lock_guard lock(_mtx);

    try
    {
        std::shared_ptr<const std::string> prev =
            _chain_head.load(std::memory_order_acquire);
        for (auto& e : entries)
        {
            if (e.tx_id.empty()) { e.tx_id = crypto::generate_uuid_v4(); }
            if (e.timestamp.empty()) { e.timestamp = make_ledger_timestamp(); }

            e.prev_hash = *prev;
            e.curr_hash = crypto::sha256_hex(e.prev_hash + e.to_payload_string());
            e.hmac_signature = crypto::hmac_sha256_hex(e.curr_hash, _hmac_secret_key);
            prev = std::make_shared<const std::string>(e.curr_hash);
        }

        if (_db)
        {
            const auto r = _db->execute_transaction(
                [&entries](sqlite3* raw)
                {
                    for (const auto& e : entries)
                    {
                        if (!storage::insert_ledger_entry(raw, e)) { return false; }
                    }
                    return true;
                });
            if (!r) { return std::unexpected(CryptoError::DatabaseError); }
        }

        for (const auto& e : entries) { _entries.push_back(e); }
        _chain_head.store(std::move(prev), std::memory_order_release);

        return {};
    }
    catch (...)
    {
        return std::unexpected(CryptoError::InternalError);
    }
}

std::expected<ChainVerification, CryptoError> AuditLog::verify_integrity_chain() const
{
    std::lock_guard lock(_mtx);

    ChainVerification result;
    result.ok = true;
    result.checked_records = _entries.size();

    std::unordered_map<std::string, LedgerEntry> db_by_tx_id;
    bool has_db = false;
    if (_db)
    {
        std::vector<LedgerEntry> db_entries;
        const auto q = _db->execute_transaction(
            [&db_entries](sqlite3* raw)
            {
                auto rows = storage::load_ledger_entries(raw);
                if (!rows) { return false; }
                db_entries = std::move(*rows);
                return true;
            });
        if (!q) { return std::unexpected(CryptoError::DatabaseError); }

        if (db_entries.size() != _entries.size())
        {
            result.ok = false;
            result.first_broken_index = std::min(db_entries.size(), _entries.size());
            result.detail = "database record count mismatch";
            return result;
        }

        has_db = true;
        db_by_tx_id.reserve(db_entries.size() * 2);
        for (auto& db_entry : db_entries)
        {
            db_by_tx_id.emplace(db_entry.tx_id, std::move(db_entry));
        }
    }

    try
    {
        std::string prev(64, '0');

        for (std::size_t i = 0; i < _entries.size(); ++i)
        {
            const LedgerEntry& e = _entries[i];

            if (!crypto::constant_time_compare(e.prev_hash, prev) ||
                is_entry_tampered_unlocked(e))
            {
                result.ok = false;
                result.first_broken_index = i;
                result.detail =
                    std::format("chain link {} broken (prev/hash/HMAC mismatch)", i);
                return result;
            }

            if (has_db)
            {
                const auto db_it = db_by_tx_id.find(e.tx_id);
                if (db_it == db_by_tx_id.end() ||
                    !crypto::constant_time_compare(db_it->second.prev_hash, e.prev_hash) ||
                    !crypto::constant_time_compare(db_it->second.curr_hash, e.curr_hash) ||
                    !crypto::constant_time_compare(db_it->second.hmac_signature,
                                                   e.hmac_signature) ||
                    db_it->second.to_payload_string() != e.to_payload_string() ||
                    is_entry_tampered_unlocked(db_it->second))
                {
                    result.ok = false;
                    result.first_broken_index = i;
                    result.detail = std::format(
                        "database record {} mismatch or tampered", i);
                    return result;
                }
            }

            prev = e.curr_hash;
        }
    }
    catch (...)
    {
        return std::unexpected(CryptoError::InternalError);
    }

    return result;
}

bool AuditLog::is_entry_tampered_unlocked(const LedgerEntry& entry) const
{
    const std::string expected_hash =
        crypto::sha256_hex(entry.prev_hash + entry.to_payload_string());
    const std::string expected_hmac =
        crypto::hmac_sha256_hex(entry.curr_hash, _hmac_secret_key);

    return !crypto::constant_time_compare(entry.curr_hash, expected_hash) ||
           !crypto::constant_time_compare(entry.hmac_signature, expected_hmac);
}

bool AuditLog::is_entry_tampered(const LedgerEntry& entry) const
{
    std::lock_guard lock(_mtx);
    return is_entry_tampered_unlocked(entry);
}

std::vector<LedgerEntry> AuditLog::entries() const
{
    std::lock_guard lock(_mtx);
    return _entries;
}

std::vector<LedgerEntry> AuditLog::entries_from(std::size_t start_index) const
{
    std::lock_guard lock(_mtx);
    if (start_index >= _entries.size()) { return {}; }
    return std::vector<LedgerEntry>(_entries.cbegin() + static_cast<std::ptrdiff_t>(start_index),
                                    _entries.cend());
}

std::vector<LedgerEntry> AuditLog::entries_page(std::size_t offset, std::size_t limit) const
{
    std::lock_guard lock(_mtx);
    if (offset >= _entries.size()) { return {}; }
    const std::size_t available = _entries.size() - offset;
    const std::size_t count = (limit == 0 || limit > available) ? available : limit;
    const auto begin_it = _entries.cbegin() + static_cast<std::ptrdiff_t>(offset);
    return std::vector<LedgerEntry>(begin_it, begin_it + static_cast<std::ptrdiff_t>(count));
}

void AuditLog::for_each_entry(const std::function<void(const LedgerEntry&)>& fn) const
{
    if (!fn) { return; }
    std::lock_guard lock(_mtx);
    for (const auto& entry : _entries)
    {
        fn(entry);
    }
}

std::shared_ptr<const std::string> AuditLog::get_last_hash() const noexcept
{ return _chain_head.load(std::memory_order_acquire); }

std::size_t AuditLog::size() const
{
    std::lock_guard lock(_mtx);
    return _entries.size();
}

void AuditLog::debug_tamper_amount(std::size_t index, std::int64_t new_amount)
{
    std::lock_guard lock(_mtx);
    if (index >= _entries.size()) { return; }

    _tampered_original_amounts.try_emplace(index, _entries[index].amount_cents);
    _entries[index].amount_cents = new_amount;

    if (_db && new_amount > 0)
    {
        const std::string& tx_id = _entries[index].tx_id;
        std::ignore = _db->execute_transaction(
            [new_amount, &tx_id](sqlite3* raw)
            {
                auto stmt = storage::prepare(
                    raw, "UPDATE ledger_entries SET amount_cents = ?1 WHERE tx_id = ?2");
                if (!stmt) { return false; }
                if (sqlite3_bind_int64(stmt->get(), 1, new_amount) != SQLITE_OK ||
                    storage::bind_text(stmt->get(), 2, tx_id) != SQLITE_OK)
                {
                    return false;
                }
                return sqlite3_step(stmt->get()) == SQLITE_DONE;
            });
    }
}

void AuditLog::restore_tampered_entries()
{
    std::lock_guard lock(_mtx);
    for (const auto& [index, original_amount] : _tampered_original_amounts)
    {
        if (index >= _entries.size()) { continue; }
        _entries[index].amount_cents = original_amount;

        if (_db && original_amount > 0)
        {
            const std::string& tx_id = _entries[index].tx_id;
            std::ignore = _db->execute_transaction(
                [original_amount, &tx_id](sqlite3* raw)
                {
                    auto stmt = storage::prepare(
                        raw,
                        "UPDATE ledger_entries SET amount_cents = ?1 WHERE tx_id = ?2");
                    if (!stmt) { return false; }
                    if (sqlite3_bind_int64(stmt->get(), 1, original_amount) != SQLITE_OK ||
                        storage::bind_text(stmt->get(), 2, tx_id) != SQLITE_OK)
                    {
                        return false;
                    }
                    return sqlite3_step(stmt->get()) == SQLITE_DONE;
                });
        }
    }
    _tampered_original_amounts.clear();
}
