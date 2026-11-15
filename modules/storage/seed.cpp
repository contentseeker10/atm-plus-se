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

#include "operations.hpp"
#include "sql.hpp"
#include "sqlite_utils.hpp"

#include "accounting/ledger.hpp"
#include "accounting/ledger_service.hpp"
#include "crypto.hpp"

#include <sqlite3.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
        std::expected<bool, DbError> insert(sqlite3* db, std::string_view query,
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
            return sqlite3_changes64(db) > 0;
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

    std::expected<void, DbError> seed_demo_data(sqlite3* db, std::string_view hmac_secret)
    {
        const auto existing_entries = load_ledger_entries(db);
        if (!existing_entries) return std::unexpected(existing_entries.error());

        const bool record_genesis = existing_entries->empty();
        std::vector<LedgerEntry> genesis;
        std::uint64_t nonce = 0;

        for (const auto& s : kSavings)
        {
            const auto inserted =
                insert(db, sql::kInsertSavingsSeed, s.account_id, s.client_id,
                       s.balance_cents, s.annual_interest_rate, s.minimum_balance_cents);
            if (!inserted) return std::unexpected(inserted.error());
            if (record_genesis && *inserted && s.balance_cents > 0)
            {
                LedgerEntry entry;
                entry.debit_account_id = std::string(BANK_TREASURY_ACCOUNT_ID);
                entry.credit_account_id = std::string(s.account_id);
                entry.amount_cents = s.balance_cents;
                entry.operation_type = "OPENING_BALANCE";
                entry.nonce = ++nonce;
                genesis.push_back(std::move(entry));
            }
        }

        for (const auto& c : kChecking)
        {
            const auto inserted = insert(db, sql::kInsertCheckingSeed, c.account_id,
                                         c.client_id, c.balance_cents, c.client_min_cents,
                                         c.overflow_max_cents, c.linked_savings_id);
            if (!inserted) return std::unexpected(inserted.error());
            if (record_genesis && *inserted && c.balance_cents > 0)
            {
                LedgerEntry entry;
                entry.debit_account_id = std::string(BANK_TREASURY_ACCOUNT_ID);
                entry.credit_account_id = std::string(c.account_id);
                entry.amount_cents = c.balance_cents;
                entry.operation_type = "OPENING_BALANCE";
                entry.nonce = ++nonce;
                genesis.push_back(std::move(entry));
            }
        }

        for (const auto& card : kCards)
        {
            const std::string salt = crypto::generate_uuid_v4();
            const std::string pin_hash = crypto::sha256_hex(card.pin, salt);
            const auto inserted =
                insert(db, sql::kInsertCardSeed, card.card_number, card.card_holder,
                       std::string_view{pin_hash}, std::string_view{salt},
                       role_code(card.role), card.is_blocked, card.is_expired,
                       card.failed_attempts, card.checking_account_id);
            if (!inserted) return std::unexpected(inserted.error());
        }

        for (const auto& cassette : kCassettes)
        {
            const auto inserted = insert(db, sql::kInsertCassetteSeed,
                                         static_cast<std::int64_t>(cassette.denomination),
                                         cassette.bill_count, cassette.capacity);
            if (!inserted) return std::unexpected(inserted.error());
            if (record_genesis && *inserted && cassette.bill_count > 0)
            {
                LedgerEntry entry;
                entry.debit_account_id = std::string(ATM_VAULT_ACCOUNT_ID);
                entry.credit_account_id = std::string(BANK_TREASURY_ACCOUNT_ID);
                entry.amount_cents = static_cast<std::int64_t>(cassette.denomination) *
                                     cassette.bill_count;
                entry.operation_type = "REPLENISHMENT";
                entry.nonce = ++nonce;
                genesis.push_back(std::move(entry));
            }
        }

        std::string prev_hash(64, '0');
        for (auto& entry : genesis)
        {
            entry.tx_id = crypto::generate_uuid_v4();
            entry.timestamp = make_ledger_timestamp();
            entry.prev_hash = prev_hash;
            entry.curr_hash =
                crypto::sha256_hex(entry.prev_hash + entry.to_payload_string());
            entry.hmac_signature = crypto::hmac_sha256_hex(entry.curr_hash, hmac_secret);
            if (auto r = insert_ledger_entry(db, entry); !r) return r;
            prev_hash = entry.curr_hash;
        }

        return {};
    }
} // namespace storage
