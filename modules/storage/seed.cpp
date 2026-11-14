/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: seed.cpp
 * Demo data seeder for accounts, cards and vault cassettes.
 *
 */

#include "seed.hpp"

#include "sql.hpp"
#include "sqlite_utils.hpp"

#include "crypto.hpp"

#include <sqlite3.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace storage
{
    namespace
    {
        struct SavingsSeed
        {
            std::string_view account_id;
            std::string_view client_id;
            std::int64_t balance_cents;
            double annual_interest_rate;
            std::int64_t minimum_balance_cents;
        };

        struct CheckingSeed
        {
            std::string_view account_id;
            std::string_view client_id;
            std::int64_t balance_cents;
            std::int64_t client_min_cents;
            std::int64_t overflow_max_cents;
            std::string_view linked_savings_id;
        };

        struct CardSeed
        {
            std::string_view card_number;
            std::string_view card_holder;
            std::string_view pin;
            UserRole role;
            bool is_blocked;
            bool is_expired;
            int failed_attempts;
            std::optional<std::string_view> checking_account_id;
        };

        struct CassetteSeed
        {
            Denomination denomination;
            int bill_count;
            int capacity;
        };

        constexpr std::array kSavings{
            SavingsSeed{"SAV-4444", "ivan.petrenko", 450000, 0.025, 0},
            SavingsSeed{"SAV-9999", "blocked.user", 120000, 0.025, 0},
            SavingsSeed{"SAV-7777", "olena.kovalenko", 0, 0.025, 0},
        };

        constexpr std::array kChecking{
            CheckingSeed{"CHK-4444", "ivan.petrenko", 154000, 10000, 500000, "SAV-4444"},
            CheckingSeed{"CHK-9999", "blocked.user", 82000, 10000, 500000, "SAV-9999"},
            CheckingSeed{"CHK-7777", "olena.kovalenko", 0, 0, 500000, "SAV-7777"},
        };

        constexpr std::array kCards{
            CardSeed{"4111-2222-3333-4444", "IVAN PETRENKO", "1234", UserRole::Customer,
                     false, false, 0, "CHK-4444"},
            CardSeed{"4111-0000-0000-9999", "BLOCKED USER", "9999", UserRole::Customer,
                     true, false, 3, "CHK-9999"},
            CardSeed{"4111-5555-6666-7777", "OLENA KOVALENKO", "4321", UserRole::Customer,
                     false, false, 0, "CHK-7777"},
            CardSeed{"9999-8888-7777-0001", "SERVICE ENGINEER #42", "0000",
                     UserRole::Engineer, false, false, 0, std::nullopt},
        };

        constexpr std::array kCassettes{
            CassetteSeed{Denomination::D_10, 100, 2000},
            CassetteSeed{Denomination::D_20, 200, 2000},
            CassetteSeed{Denomination::D_50, 200, 2000},
            CassetteSeed{Denomination::D_100, 300, 2000},
            CassetteSeed{Denomination::D_200, 150, 2000},
            CassetteSeed{Denomination::D_500, 100, 2000},
        };

        int bind(sqlite3_stmt* stmt, int index, std::string_view value) noexcept
        { return bind_text(stmt, index, value); }

        int bind(sqlite3_stmt* stmt, int index, std::int64_t value) noexcept
        { return sqlite3_bind_int64(stmt, index, value); }

        int bind(sqlite3_stmt* stmt, int index, int value) noexcept
        { return sqlite3_bind_int64(stmt, index, value); }

        int bind(sqlite3_stmt* stmt, int index, bool value) noexcept
        { return sqlite3_bind_int64(stmt, index, value ? 1 : 0); }

        int bind(sqlite3_stmt* stmt, int index, double value) noexcept
        { return sqlite3_bind_double(stmt, index, value); }

        int bind(sqlite3_stmt* stmt, int index,
                 std::optional<std::string_view> value) noexcept
        {
            return value ? bind_text(stmt, index, *value)
                         : sqlite3_bind_null(stmt, index);
        }

        template <typename... Args>
        std::expected<void, DbError> insert(sqlite3* db, std::string_view query,
                                            const Args&... args)
        {
            auto stmt = prepare(db, query);
            if (!stmt) return std::unexpected(stmt.error());

            int index = 0;
            int rc = SQLITE_OK;
            ((rc = rc == SQLITE_OK ? bind(stmt->get(), ++index, args) : rc), ...);
            if (rc != SQLITE_OK) return std::unexpected(to_db_error(rc));

            if (rc = sqlite3_step(stmt->get()); rc != SQLITE_DONE)
                return std::unexpected(to_db_error(rc));
            return {};
        }

        constexpr int role_code(UserRole role) noexcept
        {
            switch (role)
            {
            case UserRole::Customer: return 0;
            case UserRole::Engineer: return 1;
            case UserRole::None: break;
            }
            return 2;
        }
    } // namespace

    std::expected<void, DbError> seed_demo_data(sqlite3* db)
    {
        for (const auto& s : kSavings)
        {
            if (auto r = insert(db, sql::kInsertSavingsSeed, s.account_id, s.client_id,
                                s.balance_cents, s.annual_interest_rate,
                                s.minimum_balance_cents);
                !r)
                return r;
        }

        for (const auto& c : kChecking)
        {
            if (auto r = insert(db, sql::kInsertCheckingSeed, c.account_id, c.client_id,
                                c.balance_cents, c.client_min_cents, c.overflow_max_cents,
                                c.linked_savings_id);
                !r)
                return r;
        }

        for (const auto& card : kCards)
        {
            const std::string salt = crypto::generate_uuid_v4();
            const std::string pin_hash = crypto::sha256_hex(card.pin, salt);
            if (auto r =
                    insert(db, sql::kInsertCardSeed, card.card_number, card.card_holder,
                           std::string_view{pin_hash}, std::string_view{salt},
                           role_code(card.role), card.is_blocked, card.is_expired,
                           card.failed_attempts, card.checking_account_id);
                !r)
                return r;
        }

        for (const auto& cassette : kCassettes)
        {
            if (auto r = insert(db, sql::kInsertCassetteSeed,
                                static_cast<std::int64_t>(cassette.denomination),
                                cassette.bill_count, cassette.capacity);
                !r)
                return r;
        }

        return {};
    }
} // namespace storage
