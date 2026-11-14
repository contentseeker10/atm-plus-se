/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: row_mapping.cpp
 * Implementation of SQLite3 row conversion.
 *
 */

#include "row_mapping.hpp"

#include "sqlite_utils.hpp"

#include "access_control/card.hpp"

#include "accounts/checking_account.hpp"
#include "accounts/savings_account.hpp"

#include <sqlite3.h>

#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace storage
{
    std::expected<std::unique_ptr<Account>, DbError> read_account(sqlite3_stmt* row)
    {
        const std::string type = column_text(row, 0);
        try
        {
            if (type == "checking")
            {
                return std::make_unique<CheckingAccount>(
                    column_text(row, 1), column_text(row, 2),
                    sqlite3_column_int64(row, 3), sqlite3_column_int64(row, 4),
                    sqlite3_column_int64(row, 5), column_optional_text(row, 6));
            }
            if (type == "savings")
            {
                return std::make_unique<SavingsAccount>(
                    column_text(row, 1), column_text(row, 2),
                    sqlite3_column_int64(row, 3), sqlite3_column_double(row, 7),
                    sqlite3_column_int64(row, 8));
            }
        }
        catch (const std::invalid_argument&)
        {
            return std::unexpected(DbError::ConstraintViolation);
        }
        return std::unexpected(DbError::ConstraintViolation);
    }

    std::optional<Denomination> to_denomination(std::int64_t cents) noexcept
    {
        if (!std::in_range<std::underlying_type_t<Denomination>>(cents))
            return std::nullopt;

        switch (const auto denomination = static_cast<Denomination>(cents))
        {
        case Denomination::D_10:
        case Denomination::D_20:
        case Denomination::D_50:
        case Denomination::D_100:
        case Denomination::D_200:
        case Denomination::D_500: return denomination;
        }
        return std::nullopt;
    }

    std::expected<CardRecord, DbError> read_card(sqlite3_stmt* row) {
        auto card_number = column_text(row, 0);
        auto card_holder = column_text(row, 1);
        auto pin = column_text(row, 2);
        auto sal = column_text(row, 3);
        auto account = column_optional_text(row, 4);

        auto role = UserRole::None;
        switch (const int role_int = static_cast<int>(sqlite3_column_int64(row, 5))) {
            case 0: role = UserRole::Customer; break;
            case 1: role = UserRole::Engineer; break;
            default: return std::unexpected(DbError::ConstraintViolation);
        }

        auto blocked = sqlite3_column_int64(row, 6) != 0;
        auto expired = sqlite3_column_int64(row, 7) != 0;
        auto failed_atmps = sqlite3_column_int64(row, 8);

        CardRecord record(card_number, card_holder, pin, sal, account, role, blocked, expired, failed_atmps);
        return record;
    }

    std::expected<CardRecord, DbError> read_card_limited(sqlite3_stmt* row) {
        auto card_number = column_text(row, 0);
        auto blocked = sqlite3_column_int64(row, 1) != 0;
        auto expired = sqlite3_column_int64(row, 2) != 0;
        CardRecord record(card_number, blocked, expired);
        return record;
    }
} // namespace storage
