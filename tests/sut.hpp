/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: sut.hpp
 * System under test fixture for the accounting test suites.
 *
 */

#pragma once

#include "accounting/ledger_service.hpp"

#include <memory>

struct Sut
{
    std::shared_ptr<AuditLog> audit;
    std::shared_ptr<AntiReplayManager> replay;
    LedgerService ledger;

    Sut()
        : audit{std::make_shared<AuditLog>(nullptr, "test-hmac-secret")},
          replay{std::make_shared<AntiReplayManager>()}, ledger{audit, replay}
    {
    }
};
