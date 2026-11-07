/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: atm_controller.hpp
 * Interface for ATM's controller unit.
 *
 */

#pragma once

#include "atm_state.hpp"
#include "auth_service.hpp"
#include "session_context.hpp"

#include <memory>
#include <optional>
#include <vector>

class CashVault;
class DatabaseManager;
class AuditLog;

class AtmController {
public:
    void transition_to(std::unique_ptr<IAtmState> new_state);
    
    std::expected<void, AtmError> process_insert_card(std::string_view card);
    std::expected<void, AtmError> process_enter_pin(std::string_view pin);
    std::expected<void, AtmError> process_withdraw(int64_t amount_cents);
    std::expected<void, AtmError> process_deposit(const std::vector<Denomination>& bills);

    void end_session() noexcept;
private:
    std::optional<SessionContext>    _active_session;

    std::unique_ptr<IAtmState>       _current_state;
    std::unique_ptr<CashVault>       _vault;

    std::shared_ptr<DatabaseManager> _db;
    std::shared_ptr<AuditLog>        _audit_log;

    AuthService                      _auth_service;
};