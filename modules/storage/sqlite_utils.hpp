/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: sqlite_utils.hpp
 * RAII statement handle and thin wrappers over the SQLite3 C API.
 *
 */

#pragma once

#include "enums.hpp"

#include <sqlite3.h>

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace storage {
    struct StatementDeleter {
        void operator()(sqlite3_stmt* stmt) const noexcept;
    };

    using Statement = std::unique_ptr<sqlite3_stmt, StatementDeleter>;

    DbError to_db_error(int rc) noexcept;

    std::expected<void, DbError> exec(sqlite3* db, const char* sql) noexcept;

    std::expected<Statement, DbError> prepare(sqlite3* db, std::string_view sql);

    int bind_text(sqlite3_stmt* stmt, int index, std::string_view text) noexcept;

    std::string column_text(sqlite3_stmt* stmt, int column);

    std::optional<std::string> column_optional_text(sqlite3_stmt* stmt, int column);
}
