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

#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <print>
#include <string_view>

constexpr std::string_view DB_PATH     = "./atm.db";
constexpr std::string      HMAC_SECRET = "bebra";

void log(const char* msg, bool err = false) { 
    if (!err) 
    {
        std::println("[ATM+SE] {}", msg); 
    }
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
    
        log("Initializing Cash Vault...");
        std::map<Denomination, Cassette> cassettes = {
            { Denomination::D_10, Cassette(Denomination::D_10, 10, 100) },
            { Denomination::D_20, Cassette(Denomination::D_20, 10, 100) },
            { Denomination::D_50, Cassette(Denomination::D_50, 10, 100) },
            { Denomination::D_100, Cassette(Denomination::D_100, 10, 100) },
            { Denomination::D_200, Cassette(Denomination::D_200, 10, 100) },
            { Denomination::D_500, Cassette(Denomination::D_500, 10, 100) },
        };
        auto vault = std::make_unique<CashVault>(cassettes);

        log("Initializing Database Manager...");
        auto db = std::make_shared<DatabaseManager>(DB_PATH);

        log("Initializing Audit Log...");
        auto audit = std::make_shared<AuditLog>(db, HMAC_SECRET);

        log("Initializing Auth Service...");
        auto auth = std::make_unique<AuthService>(db, audit);

        log("Initializing Anti-Replay Manager...");
        auto anti_replay = std::make_shared<AntiReplayManager>();

        log("Initializing Ledger Service...");
        auto ledger = std::make_shared<LedgerService>(audit, anti_replay);

        log("Initializing ATM Controller...");
        auto controller = std::make_shared<AtmController>(std::move(vault), db, audit, std::move(auth), ledger);

        log("Initializing HTTP Server Adapter...");
        HttpServerAdapter http(controller);

        log("Starting HTTP Server...");
        auto t = std::async([&http]() { http.start(); });

        log("Server successfully started.");
    }
    catch(const std::exception& e) { log(e.what(), true); }

    return 0;
}