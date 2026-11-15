/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: test_cash_dispense.cpp
 * Dynamic programming dispense: exact combinations, fewest bills and cassette limits.
 *
 */

#include "test_framework.hpp"

#include "cash/cash_vault.hpp"

#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <utility>

namespace
{
    CashVault make_vault(std::initializer_list<std::pair<Denomination, int>> counts)
    {
        std::map<Denomination, Cassette> cassettes;
        for (const auto& [denomination, count] : counts)
            cassettes.emplace(denomination, Cassette{denomination, count, 2000});
        return CashVault{std::move(cassettes)};
    }

    int total_bills(const std::map<Denomination, int>& plan)
    {
        int total = 0;
        for (const auto& [denomination, bills] : plan) total += bills;
        return total;
    }

    std::int64_t plan_cents(const std::map<Denomination, int>& plan)
    {
        std::int64_t total = 0;
        for (const auto& [denomination, bills] : plan)
            total += static_cast<std::int64_t>(denomination) * bills;
        return total;
    }
} // namespace

TEST(dp_dispense_finds_combination_greedy_misses)
{
    begin("dispense: $60 from $50 and $20 cassettes gives 3x$20");

    const auto vault = make_vault({{Denomination::D_20, 10}, {Denomination::D_50, 10}});
    const auto plan = vault.calculate_dispense(6000);
    CHECK(plan.has_value());
    const std::map<Denomination, int> expected{{Denomination::D_20, 3}};
    CHECK(plan && *plan == expected);

    done();
}

TEST(dp_dispense_tc02_combination)
{
    begin("dispense: TC-02 $250 from $100 and $50 gives 2x$100 + 1x$50");

    const auto vault = make_vault({{Denomination::D_50, 10}, {Denomination::D_100, 10}});
    const auto plan = vault.calculate_dispense(25000);
    const std::map<Denomination, int> expected{{Denomination::D_50, 1},
                                               {Denomination::D_100, 2}};
    CHECK(plan && *plan == expected);

    done();
}

TEST(dp_dispense_uses_fewest_bills)
{
    begin("dispense: $400 uses 3 bills when a single $200 is left");

    const auto vault = make_vault(
        {{Denomination::D_50, 20}, {Denomination::D_100, 2}, {Denomination::D_200, 1}});
    const auto plan = vault.calculate_dispense(40000);
    CHECK(plan && total_bills(*plan) == 3);
    CHECK(plan && plan_cents(*plan) == 40000);

    done();
}

TEST(dp_dispense_respects_cassette_counts)
{
    begin("dispense: $60 with two $20 bills falls back to $10 bills");

    const auto vault = make_vault({{Denomination::D_10, 5}, {Denomination::D_20, 2}});
    const auto plan = vault.calculate_dispense(6000);
    const std::map<Denomination, int> expected{{Denomination::D_10, 2},
                                               {Denomination::D_20, 2}};
    CHECK(plan && *plan == expected);

    done();
}

TEST(dp_dispense_reports_impossible_amount)
{
    begin("dispense: $30 from $20 bills only has no exact combination");

    auto vault = make_vault({{Denomination::D_20, 10}});
    const auto plan = vault.calculate_dispense(3000);
    CHECK(!plan && plan.error() == VaultError::NoExactCombination);

    const auto before = vault.get_cassettes().at(Denomination::D_20).get_count();
    CHECK(before == 10);

    done();
}

TEST(dp_dispense_full_vault_is_fast_and_exact)
{
    begin("dispense: $100000 from a full vault stays exact and fast");

    const auto vault = make_vault({{Denomination::D_10, 2000},
                                   {Denomination::D_20, 2000},
                                   {Denomination::D_50, 2000},
                                   {Denomination::D_100, 2000},
                                   {Denomination::D_200, 2000},
                                   {Denomination::D_500, 2000}});

    const auto start = std::chrono::steady_clock::now();
    const auto plan = vault.calculate_dispense(10000000);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK(plan && plan_cents(*plan) == 10000000);
    CHECK(plan && total_bills(*plan) == 200);
    CHECK(elapsed < std::chrono::seconds{2});

    done();
}
