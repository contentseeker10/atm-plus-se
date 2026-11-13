/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: sql.hpp
 * SQL schema and queries used by the database manager.
 *
 */

#pragma once

#include <string_view>

namespace storage::sql
{
    inline constexpr char kSchema[] = R"sql(
BEGIN;

CREATE TABLE IF NOT EXISTS accounts (
    account_id            TEXT    NOT NULL PRIMARY KEY CHECK (length(account_id) > 0),
    client_id             TEXT    NOT NULL CHECK (length(client_id) > 0),
    type                  TEXT    NOT NULL CHECK (type IN ('checking', 'savings')),
    balance_cents         INTEGER NOT NULL,
    client_min_cents      INTEGER,
    overflow_max_cents    INTEGER,
    linked_savings_id     TEXT    REFERENCES accounts (account_id),
    annual_interest_rate  REAL,
    minimum_balance_cents INTEGER,
    CHECK (type <> 'checking' OR (
        client_min_cents IS NOT NULL AND overflow_max_cents IS NOT NULL
        AND annual_interest_rate IS NULL AND minimum_balance_cents IS NULL
        AND balance_cents >= client_min_cents)),
    CHECK (type <> 'savings' OR (
        annual_interest_rate IS NOT NULL AND minimum_balance_cents IS NOT NULL
        AND client_min_cents IS NULL AND overflow_max_cents IS NULL AND linked_savings_id IS NULL
        AND balance_cents >= minimum_balance_cents))
) STRICT;

CREATE TABLE IF NOT EXISTS vault_cassettes (
    denomination_cents INTEGER NOT NULL PRIMARY KEY
        CHECK (denomination_cents IN (1000, 2000, 5000, 10000, 20000, 50000)),
    bill_count         INTEGER NOT NULL,
    capacity           INTEGER NOT NULL CHECK (capacity > 0),
    CHECK (bill_count BETWEEN 0 AND capacity)
) STRICT;

COMMIT;
)sql";

    inline constexpr std::string_view kSelectAccount = R"sql(
SELECT type, account_id, client_id, balance_cents,
       client_min_cents, overflow_max_cents, linked_savings_id,
       annual_interest_rate, minimum_balance_cents
FROM accounts
WHERE account_id = ?1
)sql";

    inline constexpr std::string_view kUpdateBalance =
        "UPDATE accounts SET balance_cents = ?1 WHERE account_id = ?2";

    inline constexpr std::string_view kSelectInventory =
        "SELECT denomination_cents, bill_count FROM vault_cassettes";
} // namespace storage::sql
