/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: seed.hpp
 * Demo data seeder for accounts, cards and vault cassettes.
 *
 */

#pragma once

#include "enums.hpp"

#include <expected>
#include <string_view>

struct sqlite3;

namespace storage
{
    std::expected<void, DbError> seed_demo_data(sqlite3* db,
                                                std::string_view hmac_secret = "bebra");
}
