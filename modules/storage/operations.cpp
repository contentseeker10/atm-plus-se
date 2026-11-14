/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: operations.cpp
 * Implementation of unlocked database operations.
 *
 */

#include "operations.hpp"

#include "row_mapping.hpp"
#include "sql.hpp"
#include "sqlite_utils.hpp"

#include <access_control/card.hpp>
#include "accounts/account.hpp"
#include "cash/cassette.hpp"

#include <sqlite3.h>

#include <cstdint>
#include <stdexcept>
#include <utility>

namespace storage
{
    std::expected<std::unique_ptr<Account>, DbError> load_account(
        sqlite3* db, std::string_view account_id)
    {
        auto stmt = prepare(db, sql::kSelectAccount);
        if (!stmt) return std::unexpected(stmt.error());

        if (const int rc = bind_text(stmt->get(), 1, account_id); rc != SQLITE_OK)
        {
            return std::unexpected(to_db_error(rc));
        }

        switch (const int rc = sqlite3_step(stmt->get()))
        {
        case SQLITE_ROW: return read_account(stmt->get());
        case SQLITE_DONE: return std::unexpected(DbError::RecordNotFound);
        default: return std::unexpected(to_db_error(rc));
        }
    }

    std::expected<void, DbError> save_account(sqlite3* db, const Account& account)
    {
        if (!account.validate_invariants())
            return std::unexpected(DbError::ConstraintViolation);

        auto stmt = prepare(db, sql::kUpdateBalance);
        if (!stmt) return std::unexpected(stmt.error());

        if (const int rc = sqlite3_bind_int64(stmt->get(), 1, account.get_balance());
            rc != SQLITE_OK)
        {
            return std::unexpected(to_db_error(rc));
        }
        if (const int rc = bind_text(stmt->get(), 2, account.get_account_id());
            rc != SQLITE_OK)
        {
            return std::unexpected(to_db_error(rc));
        }

        if (const int rc = sqlite3_step(stmt->get()); rc != SQLITE_DONE)
        {
            return std::unexpected(to_db_error(rc));
        }
        if (sqlite3_changes64(db) == 0) return std::unexpected(DbError::RecordNotFound);

        return {};
    }

    std::expected<std::map<Denomination, int>, DbError> load_vault_inventory(sqlite3* db)
    {
        auto stmt = prepare(db, sql::kSelectInventory);
        if (!stmt) return std::unexpected(stmt.error());

        std::map<Denomination, int> inventory;
        int rc = SQLITE_ROW;
        while ((rc = sqlite3_step(stmt->get())) == SQLITE_ROW)
        {
            const auto denomination =
                to_denomination(sqlite3_column_int64(stmt->get(), 0));
            const std::int64_t count = sqlite3_column_int64(stmt->get(), 1);
            if (!denomination || !std::in_range<int>(count))
                return std::unexpected(DbError::ConstraintViolation);
            inventory.emplace(*denomination, static_cast<int>(count));
        }
        if (rc != SQLITE_DONE) return std::unexpected(to_db_error(rc));

        return inventory;
    }

    std::expected<void, DbError> save_vault_inventory(
        sqlite3* db, const std::map<Denomination, int>& inventory)
    {
        auto stmt = prepare(db, sql::kUpdateCassetteCount);
        if (!stmt) return std::unexpected(stmt.error());

        for (auto it = inventory.cbegin(); it != inventory.cend(); ++it)
        {
            sqlite3_reset(stmt->get());

            if (const int rc = sqlite3_bind_int64(stmt->get(), 1, it->second);
                rc != SQLITE_OK)
            {
                return std::unexpected(to_db_error(rc));
            }
            if (const int rc = sqlite3_bind_int64(stmt->get(), 2,
                                                  static_cast<std::int64_t>(it->first));
                rc != SQLITE_OK)
            {
                return std::unexpected(to_db_error(rc));
            }

            if (const int rc = sqlite3_step(stmt->get()); rc != SQLITE_DONE)
            {
                return std::unexpected(to_db_error(rc));
            }
            if (sqlite3_changes64(db) == 0)
                return std::unexpected(DbError::RecordNotFound);
        }
        return {};
    }

    std::expected<std::map<Denomination, Cassette>, DbError> load_cassettes(sqlite3* db)
    {
        auto stmt = prepare(db, sql::kSelectCassettes);
        if (!stmt) return std::unexpected(stmt.error());

        std::map<Denomination, Cassette> cassettes;
        int rc = SQLITE_ROW;
        while ((rc = sqlite3_step(stmt->get())) == SQLITE_ROW)
        {
            const auto denomination =
                to_denomination(sqlite3_column_int64(stmt->get(), 0));
            const std::int64_t count = sqlite3_column_int64(stmt->get(), 1);
            const std::int64_t capacity = sqlite3_column_int64(stmt->get(), 2);
            if (!denomination || !std::in_range<int>(count) ||
                !std::in_range<int>(capacity))
            {
                return std::unexpected(DbError::ConstraintViolation);
            }

            try
            {
                cassettes.emplace(*denomination,
                                  Cassette{*denomination, static_cast<int>(count),
                                           static_cast<int>(capacity)});
            }
            catch (const std::invalid_argument&)
            {
                return std::unexpected(DbError::ConstraintViolation);
            }
        }
        if (rc != SQLITE_DONE) return std::unexpected(to_db_error(rc));

        return cassettes;
    }

    std::expected<CardAccounts, DbError> load_card_accounts(sqlite3* db,
                                                            std::string_view card_number)
    {
        auto stmt = prepare(db, sql::kSelectCardAccounts);
        if (!stmt) return std::unexpected(stmt.error());

        if (const int rc = bind_text(stmt->get(), 1, card_number); rc != SQLITE_OK)
        {
            return std::unexpected(to_db_error(rc));
        }

        switch (const int rc = sqlite3_step(stmt->get()))
        {
        case SQLITE_ROW:
            return CardAccounts{column_text(stmt->get(), 0),
                                column_optional_text(stmt->get(), 1)};
        case SQLITE_DONE: return std::unexpected(DbError::RecordNotFound);
        default: return std::unexpected(to_db_error(rc));
        }
    }

    std::expected<CardRecord, DbError> load_card(sqlite3* db, std::string_view card) {
        auto stmt = prepare(db, sql::kSelectCardRecord);
        if (!stmt) {
            return std::unexpected(stmt.error());
        }

        if (const int rc = bind_text(stmt->get(), 1, card); rc != SQLITE_OK) {
            return std::unexpected(to_db_error(rc));
        }

        switch (const int rc = sqlite3_step(stmt->get())) {
            case SQLITE_ROW:  return read_card(stmt->get());
            case SQLITE_DONE: return std::unexpected(DbError::RecordNotFound);
            default:          return std::unexpected(to_db_error(rc));
        }
    }

    std::expected<CardRecord, DbError> load_card_limited(sqlite3* db, std::string_view card) {
        auto stmt = prepare(db, sql::kSelectLimitedCardRecord);
        if (!stmt) {
            return std::unexpected(stmt.error());
        }

        if (const int rc = bind_text(stmt->get(), 1, card); rc != SQLITE_OK) {
            return std::unexpected(to_db_error(rc));
        }

        switch (const int rc = sqlite3_step(stmt->get())) {
            case SQLITE_ROW:  return read_card_limited(stmt->get());
            case SQLITE_DONE: return std::unexpected(DbError::RecordNotFound);
            default:          return std::unexpected(to_db_error(rc));
        }
    }

    std::expected<void, DbError> block_card(sqlite3* db, std::string_view card) {
        auto stmt = prepare(db, sql::kUpdateBlocked);
        if (!stmt) {
            return std::unexpected(stmt.error());
        }

        if (const int rc = sqlite3_bind_int64(stmt->get(), 1, 1); rc != SQLITE_OK) {
            return std::unexpected(to_db_error(rc));
        }
        if (const int rc = bind_text(stmt->get(), 2, card); rc != SQLITE_OK) {
            return std::unexpected(to_db_error(rc));
        }

        if (const int rc = sqlite3_step(stmt->get()); rc != SQLITE_DONE) {
            return std::unexpected(to_db_error(rc));
        }
        if (sqlite3_changes64(db) == 0) {
            return std::unexpected(DbError::RecordNotFound);
        }

        return {};
    }

    std::expected<int, DbError> get_failed_attempts(sqlite3* db, std::string_view card) {
        auto stmt = prepare(db, sql::kSelectAttempts);
        if (!stmt) {
            return std::unexpected(stmt.error());
        }

        if (const int rc = bind_text(stmt->get(), 1, card); rc != SQLITE_OK) {
            return std::unexpected(to_db_error(rc));
        }

        switch (const int rc = sqlite3_step(stmt->get())) {
            case SQLITE_ROW:  return static_cast<int>(sqlite3_column_int64(stmt->get(), 0));
            case SQLITE_DONE: return std::unexpected(DbError::RecordNotFound);
            default:          return std::unexpected(to_db_error(rc));
        }
    }

    std::expected<void, DbError> set_failed_attempts(sqlite3* db, std::string_view card, int amount) {
        auto stmt = prepare(db, sql::kUpdateAttempts);
        if (!stmt) {
            return std::unexpected(stmt.error());
        }

        if (const int rc = sqlite3_bind_int64(stmt->get(), 1, amount); rc != SQLITE_OK) {
            return std::unexpected(to_db_error(rc));
        }

        if (const int rc = bind_text(stmt->get(), 2, card); rc != SQLITE_OK) {
            return std::unexpected(to_db_error(rc));
        }

        if (const int rc = sqlite3_step(stmt->get()); rc != SQLITE_DONE) {
            return std::unexpected(to_db_error(rc));
        }
        if (sqlite3_changes64(db) == 0) {
            return std::unexpected(DbError::RecordNotFound);
        }

        return {};
    }
} // namespace storage
