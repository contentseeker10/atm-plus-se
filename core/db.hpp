/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: db.hpp
 * Interface for SQLite3 database manager.
 *
 */

#pragma once

#include "enums.hpp"

#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

struct sqlite3;

class Account;

class DatabaseManager
{
public:
    explicit DatabaseManager(std::string_view path);

    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;
    DatabaseManager(DatabaseManager&&) = delete;
    DatabaseManager& operator=(DatabaseManager&&) = delete;

    std::expected<std::unique_ptr<Account>, DbError> load_account(
        std::string_view account_id);
    std::expected<void, DbError> save_account(const Account& account);
    std::expected<void, DbError> execute_transaction(
        std::function<bool(sqlite3*)> tx_block);
    std::expected<std::map<Denomination, int>, DbError> load_vault_inventory();

private:
    struct SqliteDeleter
    { void operator()(sqlite3* db) const noexcept; };

    std::unique_ptr<sqlite3, SqliteDeleter> _db_handle;
    std::string _db_path;
    std::mutex _db_mutex;
};
