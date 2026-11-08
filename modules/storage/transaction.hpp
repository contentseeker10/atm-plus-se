/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: transaction.hpp
 * RAII guard for an SQLite3 transaction.
 *
 */

#pragma once

#include "enums.hpp"

#include <expected>

struct sqlite3;

namespace storage {
    class Transaction {
    public:
        static std::expected<Transaction, DbError> begin(sqlite3* db) noexcept;

        Transaction(Transaction&& other) noexcept;

        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;
        Transaction& operator=(Transaction&&) = delete;

        ~Transaction();

        std::expected<void, DbError> commit() noexcept;

    private:
        explicit Transaction(sqlite3* db) noexcept;

        sqlite3* _db;
    };
}
