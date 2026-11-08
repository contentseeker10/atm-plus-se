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

constexpr std::chrono::seconds TIMEOUT = 60s; 

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

std::expected<void, AtmError> AtmController::validate_active_session() {
    if(!_active_session.has_value()) {
        return std::unexpected(AtmError::NoActiveSession);
    }
    if (_active_session->is_expired(TIMEOUT)) {
        end_session();
        return std::unexpected(AtmError::SessionExpired);
    }
    _active_session->touch();
    return {};
}

std::optional<std::string> AtmController::get_active_session_id() const {
    if (!_active_session.has_value() ) {
        return std::nullopt;
    }
    if (_active_session->is_expired(TIMEOUT)) {
        return std::nullopt;
    }
    return _active_session->session_id;
}

