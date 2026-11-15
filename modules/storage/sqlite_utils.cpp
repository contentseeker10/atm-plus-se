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
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace storage
{
    namespace
    {
        struct SqlHash
        {
            using is_transparent = void;
            std::size_t operator()(std::string_view sv) const noexcept
            {
                return std::hash<std::string_view>{}(sv);
            }
        };

        using ConnectionStmtCache =
            std::unordered_map<std::string, sqlite3_stmt*, SqlHash, std::equal_to<>>;

        constexpr std::size_t kMaxCachedStatementsPerConnection = 64;

        std::mutex g_stmt_cache_mtx;
        std::unordered_map<sqlite3*, ConnectionStmtCache> g_stmt_cache;
    }

    void StatementDeleter::operator()(sqlite3_stmt* stmt) const noexcept
    {
        if (stmt == nullptr) return;

        sqlite3* db = sqlite3_db_handle(stmt);
        const char* sql = sqlite3_sql(stmt);
        const bool clean =
            (sqlite3_reset(stmt) == SQLITE_OK) && (sqlite3_clear_bindings(stmt) == SQLITE_OK);

        if (clean && db != nullptr && sql != nullptr)
        {
            try
            {
                std::scoped_lock lock{g_stmt_cache_mtx};
                auto& conn_cache = g_stmt_cache[db];
                if (conn_cache.size() < kMaxCachedStatementsPerConnection &&
                    !conn_cache.contains(std::string_view{sql}))
                {
                    conn_cache.emplace(std::string{sql}, stmt);
                    return;
                }
            }
            catch (...)
            {
            }
        }

        sqlite3_finalize(stmt);
    }

    DbError to_db_error(int rc) noexcept
    {
        switch (rc & 0xff)
        {
        case SQLITE_BUSY:
        case SQLITE_LOCKED: return DbError::Busy;
        case SQLITE_CONSTRAINT: return DbError::ConstraintViolation;
        case SQLITE_CANTOPEN: return DbError::ConnectionFailed;
        default: return DbError::QueryFailed;
        }
    }

    std::expected<void, DbError> exec(sqlite3* db, const char* sql) noexcept
    {
        if (const int rc = sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
            rc != SQLITE_OK)
        {
            return std::unexpected(to_db_error(rc));
        }
        return {};
    }

    std::expected<Statement, DbError> prepare(sqlite3* db, std::string_view sql)
    {
        if (db != nullptr && !sql.empty())
        {
            std::scoped_lock lock{g_stmt_cache_mtx};
            if (const auto db_it = g_stmt_cache.find(db); db_it != g_stmt_cache.end())
            {
                if (const auto stmt_it = db_it->second.find(sql);
                    stmt_it != db_it->second.end())
                {
                    sqlite3_stmt* cached = stmt_it->second;
                    db_it->second.erase(stmt_it);
                    return Statement{cached};
                }
            }
        }

        sqlite3_stmt* raw = nullptr;
        const int rc = sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()),
                                          &raw, nullptr);
        Statement stmt{raw};
        if (rc != SQLITE_OK) return std::unexpected(to_db_error(rc));
        return stmt;
    }

    int bind_text(sqlite3_stmt* stmt, int index, std::string_view text) noexcept
    {
        return sqlite3_bind_text64(stmt, index, text.data(), text.size(), SQLITE_TRANSIENT,
                                   SQLITE_UTF8);
    }

    std::string column_text(sqlite3_stmt* stmt, int column)
    {
        const auto* text =
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, column));
        if (text == nullptr) return {};
        return {text, static_cast<std::size_t>(sqlite3_column_bytes(stmt, column))};
    }

    std::optional<std::string> column_optional_text(sqlite3_stmt* stmt, int column)
    {
        if (sqlite3_column_type(stmt, column) == SQLITE_NULL) return std::nullopt;
        return column_text(stmt, column);
    }

    void finalize_cached_statements(sqlite3* db) noexcept
    {
        if (db == nullptr) return;

        ConnectionStmtCache to_finalize;
        {
            std::scoped_lock lock{g_stmt_cache_mtx};
            if (const auto it = g_stmt_cache.find(db); it != g_stmt_cache.end())
            {
                to_finalize = std::move(it->second);
                g_stmt_cache.erase(it);
            }
        }

        for (auto& [sql, stmt] : to_finalize)
        {
            sqlite3_finalize(stmt);
        }
    }
}
