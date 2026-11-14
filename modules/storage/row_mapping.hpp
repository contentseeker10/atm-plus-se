/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: row_mapping.hpp
 * Conversion of SQLite3 rows into domain objects.
 *
 */

#pragma once

#include "enums.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>

struct sqlite3_stmt;

class Account;
struct CardRecord;

namespace storage
{
    std::expected<std::unique_ptr<Account>, DbError> read_account(sqlite3_stmt* row);

    std::optional<Denomination> to_denomination(std::int64_t cents) noexcept;

    std::expected<CardRecord, DbError> read_card(sqlite3_stmt* row);
    std::expected<CardRecord, DbError> read_card_limited(sqlite3_stmt* row);
} // namespace storage
