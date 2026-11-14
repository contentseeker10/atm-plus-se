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
#include <optional>
#include <string>
#include <string_view>

struct sqlite3;

class Account;
class Cassette;
struct CardRecord;

namespace storage
{
    struct CardAccounts
    {
        std::string checking_id;
        std::optional<std::string> savings_id;
    };

    std::expected<std::unique_ptr<Account>, DbError> load_account(
        sqlite3* db, std::string_view account_id);

    std::expected<void, DbError> save_account(sqlite3* db, const Account& account);

    std::expected<std::map<Denomination, int>, DbError> load_vault_inventory(sqlite3* db);

    std::expected<void, DbError> save_vault_inventory(
        sqlite3* db, const std::map<Denomination, int>& inventory);

    std::expected<std::map<Denomination, Cassette>, DbError> load_cassettes(sqlite3* db);

    std::expected<CardAccounts, DbError> load_card_accounts(sqlite3* db,
                                                            std::string_view card_number);

    std::expected<CardRecord, DbError> load_card(sqlite3* db, std::string_view card);
    std::expected<CardRecord, DbError> load_card_limited(sqlite3* db, std::string_view card);
    std::expected<void, DbError> block_card(sqlite3* db, std::string_view card);
    std::expected<int, DbError> get_failed_attempts(sqlite3* db, std::string_view card);
    std::expected<void, DbError> set_failed_attempts(sqlite3* db, std::string_view card, int amount);
} // namespace storage
