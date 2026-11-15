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

#include "accounting/ledger.hpp"
#include "accounts/account.hpp"
#include "cash/cassette.hpp"
#include <access_control/card.hpp>

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

    std::expected<CardRecord, DbError> load_card(sqlite3* db, std::string_view card)
    {
        auto stmt = prepare(db, sql::kSelectCardRecord);
        if (!stmt) { return std::unexpected(stmt.error()); }

        if (const int rc = bind_text(stmt->get(), 1, card); rc != SQLITE_OK)
        {
            return std::unexpected(to_db_error(rc));
        }

        switch (const int rc = sqlite3_step(stmt->get()))
        {
        case SQLITE_ROW: return read_card(stmt->get());
        case SQLITE_DONE: return std::unexpected(DbError::RecordNotFound);
        default: return std::unexpected(to_db_error(rc));
        }
    }

    std::expected<CardRecord, DbError> load_card_limited(sqlite3* db,
                                                         std::string_view card)
    {
        auto stmt = prepare(db, sql::kSelectLimitedCardRecord);
        if (!stmt) { return std::unexpected(stmt.error()); }

        if (const int rc = bind_text(stmt->get(), 1, card); rc != SQLITE_OK)
        {
            return std::unexpected(to_db_error(rc));
        }

        switch (const int rc = sqlite3_step(stmt->get()))
        {
        case SQLITE_ROW: return read_card_limited(stmt->get());
        case SQLITE_DONE: return std::unexpected(DbError::RecordNotFound);
        default: return std::unexpected(to_db_error(rc));
        }
    }

    std::expected<void, DbError> block_card(sqlite3* db, std::string_view card)
    {
        auto stmt = prepare(db, sql::kUpdateBlocked);
        if (!stmt) { return std::unexpected(stmt.error()); }

        if (const int rc = sqlite3_bind_int64(stmt->get(), 1, 1); rc != SQLITE_OK)
        {
            return std::unexpected(to_db_error(rc));
        }
        if (const int rc = bind_text(stmt->get(), 2, card); rc != SQLITE_OK)
        {
            return std::unexpected(to_db_error(rc));
        }

        if (const int rc = sqlite3_step(stmt->get()); rc != SQLITE_DONE)
        {
            return std::unexpected(to_db_error(rc));
        }
        if (sqlite3_changes64(db) == 0)
        {
            return std::unexpected(DbError::RecordNotFound);
        }

        return {};
    }

    std::expected<void, DbError> unlock_card(sqlite3* db, std::string_view card)
    {
        auto stmt = prepare(db, sql::kUnlockCard);
        if (!stmt) return std::unexpected(stmt.error());

        if (const int rc = bind_text(stmt->get(), 1, card); rc != SQLITE_OK)
        {
            return std::unexpected(to_db_error(rc));
        }

        if (const int rc = sqlite3_step(stmt->get()); rc != SQLITE_DONE)
        {
            return std::unexpected(to_db_error(rc));
        }
        if (sqlite3_changes64(db) == 0)
        {
            return std::unexpected(DbError::RecordNotFound);
        }
        return {};
    }

    std::expected<int, DbError> get_failed_attempts(sqlite3* db, std::string_view card)
    {
        auto stmt = prepare(db, sql::kSelectAttempts);
        if (!stmt) { return std::unexpected(stmt.error()); }

        if (const int rc = bind_text(stmt->get(), 1, card); rc != SQLITE_OK)
        {
            return std::unexpected(to_db_error(rc));
        }

        switch (const int rc = sqlite3_step(stmt->get()))
        {
        case SQLITE_ROW: return static_cast<int>(sqlite3_column_int64(stmt->get(), 0));
        case SQLITE_DONE: return std::unexpected(DbError::RecordNotFound);
        default: return std::unexpected(to_db_error(rc));
        }
    }

    std::expected<void, DbError> set_failed_attempts(sqlite3* db, std::string_view card,
                                                     int amount)
    {
        auto stmt = prepare(db, sql::kUpdateAttempts);
        if (!stmt) { return std::unexpected(stmt.error()); }

        if (const int rc = sqlite3_bind_int64(stmt->get(), 1, amount); rc != SQLITE_OK)
        {
            return std::unexpected(to_db_error(rc));
        }

        if (const int rc = bind_text(stmt->get(), 2, card); rc != SQLITE_OK)
        {
            return std::unexpected(to_db_error(rc));
        }

        if (const int rc = sqlite3_step(stmt->get()); rc != SQLITE_DONE)
        {
            return std::unexpected(to_db_error(rc));
        }
        if (sqlite3_changes64(db) == 0)
        {
            return std::unexpected(DbError::RecordNotFound);
        }

        return {};
    }

    namespace
    {
        int bind_optional_int(sqlite3_stmt* stmt, int index,
                              std::optional<int> value) noexcept
        {
            if (!value.has_value()) return sqlite3_bind_null(stmt, index);
            return sqlite3_bind_int64(stmt, index, static_cast<sqlite3_int64>(*value));
        }
    } // namespace

    std::expected<void, DbError> revoke_card(sqlite3* db, std::string_view card)
    {
        auto stmt = prepare(db, sql::kRevokeCard);
        if (!stmt) return std::unexpected(stmt.error());

        if (const int rc = bind_text(stmt->get(), 1, card); rc != SQLITE_OK)
            return std::unexpected(to_db_error(rc));

        if (const int rc = sqlite3_step(stmt->get()); rc != SQLITE_DONE)
            return std::unexpected(to_db_error(rc));
        if (sqlite3_changes64(db) == 0) return std::unexpected(DbError::RecordNotFound);
        return {};
    }

    std::expected<void, DbError> insert_card_admin(sqlite3* db, const CardRecord& card,
                                                   const std::string& salt)
    {
        auto stmt = prepare(db, sql::kInsertCardAdmin);
        if (!stmt) return std::unexpected(stmt.error());

        if (const int rc = bind_text(stmt->get(), 1, card.number); rc != SQLITE_OK)
            return std::unexpected(to_db_error(rc));
        if (const int rc = bind_text(stmt->get(), 2, card.holder); rc != SQLITE_OK)
            return std::unexpected(to_db_error(rc));
        if (const int rc = bind_text(stmt->get(), 3, card.pin_hash); rc != SQLITE_OK)
            return std::unexpected(to_db_error(rc));
        if (const int rc = bind_text(stmt->get(), 4, salt); rc != SQLITE_OK)
            return std::unexpected(to_db_error(rc));
        if (const int rc = sqlite3_bind_int64(stmt->get(), 5,
                                              card.role == UserRole::Engineer ? 1 : 0);
            rc != SQLITE_OK)
            return std::unexpected(to_db_error(rc));
        if (card.account_id.has_value())
        {
            if (const int rc = bind_text(stmt->get(), 6, *card.account_id);
                rc != SQLITE_OK)
                return std::unexpected(to_db_error(rc));
        }
        else if (const int rc = sqlite3_bind_null(stmt->get(), 6); rc != SQLITE_OK)
            return std::unexpected(to_db_error(rc));
        if (const int rc = bind_optional_int(stmt->get(), 7, card.access_window_start);
            rc != SQLITE_OK)
            return std::unexpected(to_db_error(rc));
        if (const int rc = bind_optional_int(stmt->get(), 8, card.access_window_end);
            rc != SQLITE_OK)
            return std::unexpected(to_db_error(rc));

        if (const int rc = sqlite3_step(stmt->get()); rc != SQLITE_DONE)
            return std::unexpected(to_db_error(rc));
        return {};
    }

    std::expected<void, DbError> set_card_role(sqlite3* db, std::string_view card,
                                               UserRole role)
    {
        auto stmt = prepare(db, sql::kSetCardRole);
        if (!stmt) return std::unexpected(stmt.error());

        if (const int rc =
                sqlite3_bind_int64(stmt->get(), 1, role == UserRole::Engineer ? 1 : 0);
            rc != SQLITE_OK)
            return std::unexpected(to_db_error(rc));
        if (const int rc = bind_text(stmt->get(), 2, card); rc != SQLITE_OK)
            return std::unexpected(to_db_error(rc));

        if (const int rc = sqlite3_step(stmt->get()); rc != SQLITE_DONE)
            return std::unexpected(to_db_error(rc));
        if (sqlite3_changes64(db) == 0) return std::unexpected(DbError::RecordNotFound);
        return {};
    }

    std::expected<void, DbError> set_card_window(sqlite3* db, std::string_view card,
                                                 std::optional<int> start,
                                                 std::optional<int> end)
    {
        if ((start.has_value() != end.has_value()))
            return std::unexpected(DbError::ConstraintViolation);
        auto stmt = prepare(db, sql::kSetCardWindow);
        if (!stmt) return std::unexpected(stmt.error());

        if (const int rc = bind_optional_int(stmt->get(), 1, start); rc != SQLITE_OK)
            return std::unexpected(to_db_error(rc));
        if (const int rc = bind_optional_int(stmt->get(), 2, end); rc != SQLITE_OK)
            return std::unexpected(to_db_error(rc));
        if (const int rc = bind_text(stmt->get(), 3, card); rc != SQLITE_OK)
            return std::unexpected(to_db_error(rc));

        if (const int rc = sqlite3_step(stmt->get()); rc != SQLITE_DONE)
            return std::unexpected(to_db_error(rc));
        if (sqlite3_changes64(db) == 0) return std::unexpected(DbError::RecordNotFound);
        return {};
    }

    std::expected<std::vector<CardAdminRecord>, DbError> list_cards(sqlite3* db)
    {
        auto stmt = prepare(db, sql::kListCards);
        if (!stmt) return std::unexpected(stmt.error());

        std::vector<CardAdminRecord> cards;
        while (true)
        {
            const int rc = sqlite3_step(stmt->get());
            if (rc == SQLITE_DONE) break;
            if (rc != SQLITE_ROW) return std::unexpected(to_db_error(rc));

            CardAdminRecord rec;
            rec.number = column_text(stmt->get(), 0);
            rec.holder = column_text(stmt->get(), 1);
            switch (static_cast<int>(sqlite3_column_int64(stmt->get(), 2)))
            {
            case 0: rec.role = UserRole::Customer; break;
            case 1: rec.role = UserRole::Engineer; break;
            default: rec.role = UserRole::None; break;
            }
            rec.is_blocked = sqlite3_column_int64(stmt->get(), 3) != 0;
            rec.is_expired = sqlite3_column_int64(stmt->get(), 4) != 0;
            rec.is_revoked = sqlite3_column_int64(stmt->get(), 5) != 0;
            rec.failed_attempts = static_cast<int>(sqlite3_column_int64(stmt->get(), 6));
            rec.window_start = sqlite3_column_type(stmt->get(), 7) == SQLITE_NULL
                                   ? std::nullopt
                                   : std::optional<int>{static_cast<int>(
                                         sqlite3_column_int64(stmt->get(), 7))};
            rec.window_end = sqlite3_column_type(stmt->get(), 8) == SQLITE_NULL
                                 ? std::nullopt
                                 : std::optional<int>{static_cast<int>(
                                       sqlite3_column_int64(stmt->get(), 8))};
            rec.checking_account_id = column_optional_text(stmt->get(), 9);
            cards.push_back(std::move(rec));
        }
        return cards;
    }

    std::expected<void, DbError> insert_ledger_entry(sqlite3* db,
                                                     const LedgerEntry& entry)
    {
        auto stmt = prepare(db, sql::kInsertLedgerEntry);
        if (!stmt) return std::unexpected(stmt.error());

        sqlite3_stmt* s = stmt->get();
        if (const int rc = bind_text(s, 1, entry.tx_id); rc != SQLITE_OK)
            return std::unexpected(to_db_error(rc));
        if (const int rc = bind_text(s, 2, entry.debit_account_id); rc != SQLITE_OK)
            return std::unexpected(to_db_error(rc));
        if (const int rc = bind_text(s, 3, entry.credit_account_id); rc != SQLITE_OK)
            return std::unexpected(to_db_error(rc));
        if (const int rc = sqlite3_bind_int64(s, 4, entry.amount_cents); rc != SQLITE_OK)
            return std::unexpected(to_db_error(rc));
        if (const int rc = bind_text(s, 5, entry.timestamp); rc != SQLITE_OK)
            return std::unexpected(to_db_error(rc));
        if (const int rc = bind_text(s, 6, entry.operation_type); rc != SQLITE_OK)
            return std::unexpected(to_db_error(rc));
        if (const int rc =
                sqlite3_bind_int64(s, 7, static_cast<std::int64_t>(entry.nonce));
            rc != SQLITE_OK)
            return std::unexpected(to_db_error(rc));
        if (const int rc = bind_text(s, 8, entry.prev_hash); rc != SQLITE_OK)
            return std::unexpected(to_db_error(rc));
        if (const int rc = bind_text(s, 9, entry.curr_hash); rc != SQLITE_OK)
            return std::unexpected(to_db_error(rc));
        if (const int rc = bind_text(s, 10, entry.hmac_signature); rc != SQLITE_OK)
            return std::unexpected(to_db_error(rc));

        if (const int rc = sqlite3_step(s); rc != SQLITE_DONE)
            return std::unexpected(to_db_error(rc));
        return {};
    }

    std::expected<std::vector<LedgerEntry>, DbError> load_ledger_entries(sqlite3* db)
    {
        auto stmt = prepare(db, sql::kSelectLedgerEntries);
        if (!stmt) return std::unexpected(stmt.error());

        std::vector<LedgerEntry> loaded;
        int rc = SQLITE_ROW;
        while ((rc = sqlite3_step(stmt->get())) == SQLITE_ROW)
        {
            loaded.push_back(read_ledger_entry(stmt->get()));
        }
        if (rc != SQLITE_DONE) return std::unexpected(to_db_error(rc));

        return loaded;
    }
} // namespace storage
