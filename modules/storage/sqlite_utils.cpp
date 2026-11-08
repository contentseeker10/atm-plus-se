/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: sqlite_utils.cpp
 * Implementation of SQLite3 C API wrappers.
 *
 */

#include "sqlite_utils.hpp"

#include <cstddef>

namespace storage {
    void StatementDeleter::operator()(sqlite3_stmt* stmt) const noexcept {
        sqlite3_finalize(stmt);
    }

    DbError to_db_error(int rc) noexcept {
        switch (rc & 0xff) {
        case SQLITE_BUSY:
        case SQLITE_LOCKED:     return DbError::Busy;
        case SQLITE_CONSTRAINT: return DbError::ConstraintViolation;
        case SQLITE_CANTOPEN:   return DbError::ConnectionFailed;
        default:                return DbError::QueryFailed;
        }
    }

    std::expected<void, DbError> exec(sqlite3* db, const char* sql) noexcept {
        if (const int rc = sqlite3_exec(db, sql, nullptr, nullptr, nullptr); rc != SQLITE_OK) {
            return std::unexpected(to_db_error(rc));
        }
        return {};
    }

    std::expected<Statement, DbError> prepare(sqlite3* db, std::string_view sql) {
        sqlite3_stmt* raw = nullptr;
        const int rc = sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &raw, nullptr);
        Statement stmt{raw};
        if (rc != SQLITE_OK) return std::unexpected(to_db_error(rc));
        return stmt;
    }

    int bind_text(sqlite3_stmt* stmt, int index, std::string_view text) noexcept {
        return sqlite3_bind_text64(stmt, index, text.data(), text.size(), SQLITE_STATIC, SQLITE_UTF8);
    }

    std::string column_text(sqlite3_stmt* stmt, int column) {
        const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, column));
        if (text == nullptr) return {};
        return {text, static_cast<std::size_t>(sqlite3_column_bytes(stmt, column))};
    }

    std::optional<std::string> column_optional_text(sqlite3_stmt* stmt, int column) {
        if (sqlite3_column_type(stmt, column) == SQLITE_NULL) return std::nullopt;
        return column_text(stmt, column);
    }
}
