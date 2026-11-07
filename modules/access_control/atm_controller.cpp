/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: atm_controller.cpp
 * Implementation of ATM's controller unit.
 *
 */

#include "atm_controller.hpp"

#include "cash/cash_vault.hpp"

AtmController::AtmController(
    std::unique_ptr<CashVault> vault, 
    std::shared_ptr<DatabaseManager> db, 
    std::shared_ptr<AuditLog> log, 
    AuthService auth
) : _vault        {std::move(vault)},
    _db           {std::move(db)},
    _audit_log    {std::move(log)},
    _auth_service {std::move(auth)} 
{}
AtmController::~AtmController() = default;

void AtmController::transition_to(std::unique_ptr<IAtmState> new_state) {
    _current_state = std::move(new_state);
}