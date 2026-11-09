/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: test_framework.hpp
 * Test framework.
 *
 * USAGE:
 *
 *     #include "test_framework.hpp"
 *
 *     TEST(my_feature) {
 *         begin("TC-xx my feature
 *         CHECK(1 + 1 == 2);
 *         CHECK_MSG(vec.size() == 3, "vector should have 3 items");
 *         done();
 *     }
 *
 *     TEST_MAIN()
 *
 */

#pragma once

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace testfw
{
    struct TestCase
    {
        std::string name;
        std::function<void()> fn;
    };

    inline std::vector<TestCase>& registry()
    {
        static std::vector<TestCase> r;
        return r;
    }

    inline int& failure_count()
    {
        static int count = 0;
        return count;
    }

    inline void begin(const std::string& name) { std::cout << "[TEST] " << name << '\n'; }

    inline void done() { std::cout.flush(); }

    inline bool register_test(std::string name, std::function<void()> fn)
    {
        registry().push_back({std::move(name), std::move(fn)});
        return true;
    }

    inline int run_all()
    {
        for (const TestCase& tc : registry()) { tc.fn(); }
        const int failed = failure_count();
        if (failed == 0)
        {
            std::cout << "ALL TESTS PASSED\n";
            return 0;
        }
        std::cout << failed << " CHECK(S) FAILED\n";
        return 1;
    }
} // namespace testfw

using testfw::begin;
using testfw::done;

#define CHECK(cond)                                                                      \
    do                                                                                   \
    {                                                                                    \
        if (!(cond))                                                                     \
        {                                                                                \
            ++testfw::failure_count();                                                   \
            std::cout << "    FAIL: " << #cond << " (line " << __LINE__ << ")\n";        \
        }                                                                                \
    } while (0)

#define CHECK_MSG(cond, msg)                                                             \
    do                                                                                   \
    {                                                                                    \
        if (!(cond))                                                                     \
        {                                                                                \
            ++testfw::failure_count();                                                   \
            std::cout << "    FAIL: " << #cond << " (line " << __LINE__ << ") - " << msg \
                      << "\n";                                                           \
        }                                                                                \
    } while (0)

#define TEST(name)                                                                       \
    static void testfw_test_fn_##name();                                                 \
    static const bool testfw_registered_##name =                                         \
        ::testfw::register_test(#name, &testfw_test_fn_##name);                          \
    static void testfw_test_fn_##name()

#define TEST_MAIN()                                                                      \
    int main() { return ::testfw::run_all(); }
