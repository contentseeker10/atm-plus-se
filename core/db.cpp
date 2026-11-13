/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: db.cpp
 * Implementation of SQLite3 database manager.
 *
 */

#include "db.hpp"

#include "access_control/card.hpp"

#include "accounts/account.hpp"

#include "storage/operations.hpp"
#include "storage/sql.hpp"
#include "storage/sqlite_utils.hpp"
#include "storage/transaction.hpp"

#include <sqlite3.h>

#include <format>
#include <stdexcept>

void DatabaseManager::SqliteDeleter::operator()(sqlite3* db) const noexcept
{ sqlite3_close(db); }

DatabaseManager::DatabaseManager(std::string_view path) : _db_path{path}
{
    sqlite3* raw = nullptr;
    const int rc = sqlite3_open_v2(
        _db_path.c_str(), &raw,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    _db_handle.reset(raw);
    if (rc != SQLITE_OK)
    {
        throw std::runtime_error(
            std::format("cannot open database '{}': {}", _db_path, sqlite3_errstr(rc)));
    }

    sqlite3_busy_timeout(_db_handle.get(), 5000);

    if (!storage::exec(_db_handle.get(), "PRAGMA foreign_keys = ON") ||
        !storage::exec(_db_handle.get(), storage::sql::kSchema))
    {
        throw std::runtime_error(std::format("cannot initialise database '{}': {}",
                                             _db_path, sqlite3_errmsg(_db_handle.get())));
    }
}

std::expected<std::unique_ptr<Account>, DbError> DatabaseManager::load_account(
    std::string_view account_id)
{
    std::scoped_lock lock{_db_mutex};
    return storage::load_account(_db_handle.get(), account_id);
}

std::expected<void, DbError> DatabaseManager::save_account(const Account& account)
{
    std::scoped_lock lock{_db_mutex};
    return storage::save_account(_db_handle.get(), account);
}

std::expected<void, DbError> DatabaseManager::execute_transaction(
    std::function<bool(sqlite3*)> tx_block)
{
    if (!tx_block) return std::unexpected(DbError::TransactionFailed);

    std::scoped_lock lock{_db_mutex};

    auto tx = storage::Transaction::begin(_db_handle.get());
    if (!tx) return std::unexpected(tx.error());

    if (!tx_block(_db_handle.get())) return std::unexpected(DbError::TransactionFailed);

    return tx->commit();
}

std::expected<std::map<Denomination, int>, DbError> DatabaseManager::
    load_vault_inventory()
{
    std::scoped_lock lock{_db_mutex};
    return storage::load_vault_inventory(_db_handle.get());
}

std::expected<CardRecord, DbError> DatabaseManager::load_card(std::string_view card) {
    std::scoped_lock lock{_db_mutex};
    return storage::load_card(_db_handle.get(), card);
}