/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: main.cpp
 * Program's entry point
 *
 */

#include "db.hpp"
#include "enums.hpp"
#include "http_server.hpp"

#include "access_control/atm_controller.hpp"
#include "access_control/auth_service.hpp"

#include "accounting/audit.hpp"
#include "accounting/ledger_service.hpp"

#include "cash/cash_vault.hpp"

#include "security/anti_replay.hpp"

#include "storage/operations.hpp"
#include "storage/seed.hpp"

#include <cstdlib>
#include <map>
#include <memory>
#include <print>
#include <string>
#include <string_view>

constexpr std::string_view DB_PATH = "./atm.db";
constexpr std::string HMAC_SECRET = "bebra";

void log(const char* msg, bool err = false)
{
    if (!err) { std::println("[ATM+SE] {}", msg); }
    else
    {
        std::println("\033[31m[ATM+SE] [ERROR] {}\033[0m", msg);
    }
}

int main()
{
    try
    {
        log("Starting server...");

        log("Initializing Database Manager...");
        auto db = std::make_shared<DatabaseManager>(DB_PATH);

        const char* env_hmac = std::getenv("ATM_HMAC_SECRET");
        const std::string hmac_secret = (env_hmac != nullptr && *env_hmac != '\0')
                                            ? std::string{env_hmac}
                                            : HMAC_SECRET;

        log("Seeding Database and Loading Cash Vault...");
        std::map<Denomination, Cassette> cassettes;
        const auto seeded = db->execute_transaction(
            [&](sqlite3* raw)
            {
                if (!storage::seed_demo_data(raw, hmac_secret)) return false;
                auto loaded = storage::load_cassettes(raw);
                if (!loaded) return false;
                cassettes = std::move(*loaded);
                return true;
            });
        if (!seeded)
        {
            log("Failed to seed or load database.", true);
            return 1;
        }
        auto vault = std::make_unique<CashVault>(std::move(cassettes));

        log("Initializing Audit Log...");
        auto audit = std::make_shared<AuditLog>(db, hmac_secret);
        if (!audit->load_from_database())
        {
            log("Failed to load audit log from database.", true);
            return 1;
        }

        log("Initializing Auth Service...");
        auto auth = std::make_unique<AuthService>(db, audit);

        log("Initializing Anti-Replay Manager...");
        auto anti_replay = std::make_shared<AntiReplayManager>();

        log("Initializing Ledger Service...");
        auto ledger = std::make_shared<LedgerService>(audit, anti_replay);

        log("Initializing ATM Controller...");
        auto controller = std::make_shared<AtmController>(
            std::move(vault), db, audit, std::move(auth), ledger, anti_replay);

        log("Initializing HTTP Server Adapter...");
        HttpServerAdapter http(controller);

        log("Server successfully started.");
        http.start();
    }
    catch (const std::exception& e)
    {
        log(e.what(), true);
    }

    return 0;
}