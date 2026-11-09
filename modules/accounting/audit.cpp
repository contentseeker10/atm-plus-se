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

#include "storage/sqlite_utils.hpp"

#include <sqlite3.h>

#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace
{
    constexpr char kLedgerEntriesSchema[] = R"sql(
CREATE TABLE IF NOT EXISTS ledger_entries (
    tx_id           TEXT    NOT NULL PRIMARY KEY,
    debit_account   TEXT    NOT NULL CHECK (length(debit_account) > 0),
    credit_account  TEXT    NOT NULL CHECK (length(credit_account) > 0),
    amount_cents    INTEGER NOT NULL CHECK (amount_cents > 0),
    timestamp       TEXT    NOT NULL,
    operation_type  TEXT    NOT NULL,
    tx_nonce        INTEGER NOT NULL CHECK (tx_nonce >= 0),
    prev_hash       TEXT    NOT NULL,
    curr_hash       TEXT    NOT NULL,
    hmac_signature  TEXT    NOT NULL,
    CHECK (debit_account <> credit_account)
) STRICT;
)sql";

    constexpr std::string_view kInsertLedgerEntry = R"sql(
INSERT INTO ledger_entries
    (tx_id, debit_account, credit_account, amount_cents,
     timestamp, operation_type, tx_nonce, prev_hash, curr_hash, hmac_signature)
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)
)sql";

    std::expected<void, CryptoError> insert_entry(sqlite3* db, const LedgerEntry& e)
    {
        auto stmt = storage::prepare(db, kInsertLedgerEntry);
        if (!stmt) { return std::unexpected(CryptoError::DatabaseError); }

        sqlite3_stmt* s = stmt->get();
        storage::bind_text(s, 1, e.tx_id);
        storage::bind_text(s, 2, e.debit_account_id);
        storage::bind_text(s, 3, e.credit_account_id);
        sqlite3_bind_int64(s, 4, e.amount_cents);
        storage::bind_text(s, 5, e.timestamp);
        storage::bind_text(s, 6, e.operation_type);
        sqlite3_bind_int64(s, 7, static_cast<std::int64_t>(e.nonce));
        storage::bind_text(s, 8, e.prev_hash);
        storage::bind_text(s, 9, e.curr_hash);
        storage::bind_text(s, 10, e.hmac_signature);

        if (sqlite3_step(s) != SQLITE_DONE)
        {
            return std::unexpected(CryptoError::DatabaseError);
        }
        return {};
    }
} // namespace

AuditLog::AuditLog(std::shared_ptr<DatabaseManager> db, std::string hmac_secret_key)
    : _db{std::move(db)}, _hmac_secret_key{std::move(hmac_secret_key)},
      _chain_head{std::make_shared<const std::string>(64, '0')}
{
    if (_db)
    {
        const auto r = _db->execute_transaction(
            [](sqlite3* raw)
            { return storage::exec(raw, kLedgerEntriesSchema).has_value(); });
        if (!r)
        {
            throw std::runtime_error("AuditLog: cannot create ledger_entries table");
        }
    }
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
            constexpr std::string_view kSelectEntries = R"sql(
SELECT tx_id, debit_account, credit_account, amount_cents,
       timestamp, operation_type, tx_nonce,
       prev_hash, curr_hash, hmac_signature
FROM ledger_entries)sql";
            auto stmt = storage::prepare(raw, kSelectEntries);
            if (!stmt) { return false; }
            sqlite3_stmt* s = stmt->get();
            for (;;)
            {
                const int rc = sqlite3_step(s);
                if (rc == SQLITE_DONE) { break; }
                if (rc != SQLITE_ROW) { return false; }

                LedgerEntry e;
                e.tx_id = storage::column_text(s, 0);
                e.debit_account_id = storage::column_text(s, 1);
                e.credit_account_id = storage::column_text(s, 2);
                e.amount_cents = sqlite3_column_int64(s, 3);
                e.timestamp = storage::column_text(s, 4);
                e.operation_type = storage::column_text(s, 5);
                e.nonce = static_cast<std::uint64_t>(sqlite3_column_int64(s, 6));
                e.prev_hash = storage::column_text(s, 7);
                e.curr_hash = storage::column_text(s, 8);
                e.hmac_signature = storage::column_text(s, 9);
                loaded.push_back(std::move(e));
            }
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
            if (e.curr_hash != expected_hash)
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
                        if (!insert_entry(raw, e)) { return false; }
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

    try
    {
        std::string prev(64, '0');

        for (std::size_t i = 0; i < _entries.size(); ++i)
        {
            const LedgerEntry& e = _entries[i];

            const std::string expected_hash =
                crypto::sha256_hex(e.prev_hash + e.to_payload_string());
            const std::string expected_hmac =
                crypto::hmac_sha256_hex(e.curr_hash, _hmac_secret_key);

            if (e.prev_hash != prev || e.curr_hash != expected_hash ||
                !crypto::constant_time_compare(e.hmac_signature, expected_hmac))
            {
                result.ok = false;
                result.first_broken_index = i;
                result.detail =
                    std::format("chain link {} broken (prev/hash/HMAC mismatch)", i);
                return result;
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

std::vector<LedgerEntry> AuditLog::entries() const
{
    std::lock_guard lock(_mtx);
    return _entries;
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
    if (index < _entries.size()) { _entries[index].amount_cents = new_amount; }
}
