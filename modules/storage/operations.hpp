/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: operations.hpp
 * Unlocked database operations on a raw SQLite3 connection.
 *
 */

#pragma once

#include "enums.hpp"

#include <expected>
#include <map>
#include <memory>
#include <string_view>

struct sqlite3;

class Account;

namespace storage
{
    std::expected<std::unique_ptr<Account>, DbError> load_account(
        sqlite3* db, std::string_view account_id);

    std::expected<void, DbError> save_account(sqlite3* db, const Account& account);

    std::expected<std::map<Denomination, int>, DbError> load_vault_inventory(sqlite3* db);
} // namespace storage
