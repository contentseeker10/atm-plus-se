/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: transaction.cpp
 * Implementation of the SQLite3 transaction guard.
 *
 */

#include "transaction.hpp"
#include "sqlite_utils.hpp"
#include <sqlite3.h>

#include <utility>

namespace storage
{
    std::expected<Transaction, DbError> Transaction::begin(sqlite3* db) noexcept
    {
        if (sqlite3_get_autocommit(db) == 0)
            return std::unexpected(DbError::TransactionFailed);
        return exec(db, "BEGIN IMMEDIATE").transform([db] { return Transaction{db}; });
    }

    Transaction::Transaction(sqlite3* db) noexcept : _db{db} { }

    Transaction::Transaction(Transaction&& other) noexcept
        : _db{std::exchange(other._db, nullptr)}
    {
    }

    Transaction::~Transaction()
    {
        if (_db != nullptr && sqlite3_get_autocommit(_db) == 0)
        {
            sqlite3_exec(_db, "ROLLBACK", nullptr, nullptr, nullptr);
        }
    }

    std::expected<void, DbError> Transaction::commit() noexcept
    {
        auto result = exec(_db, "COMMIT");
        if (result) _db = nullptr;
        return result;
    }
} // namespace storage
