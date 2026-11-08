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

#include "db.hpp"

#include "atm_state.hpp"
#include "auth_service.hpp"
#include "session_context.hpp"

#include <memory>
#include <optional>
#include <vector>
#include <mutex>

using namespace std::literals::chrono_literals;

class CashVault;
class AuditLog;

class AtmController {
public:
    AtmController(
        std::unique_ptr<CashVault> vault, 
        std::shared_ptr<DatabaseManager> db, 
        std::shared_ptr<AuditLog> log, 
        AuthService auth
    );
    ~AtmController();

    AtmController(const AtmController&)            = delete;
    AtmController& operator=(const AtmController&) = delete;

    AtmController(AtmController&&)                 = delete;
    AtmController& operator=(AtmController&&)      = delete;

    void transition_to(std::unique_ptr<IAtmState> new_state);
    void start_session(std::string_view card, UserRole role);
    void end_session() noexcept;

    std::expected<void, AtmError> process_insert_card(std::string_view card);
    std::expected<void, AtmError> process_enter_pin(std::string_view pin);
    std::expected<void, AtmError> process_withdraw(int64_t amount_cents);
    std::expected<void, AtmError> process_deposit(const std::vector<Denomination>& bills);

    std::expected<void, AtmError> validate_active_session(); 

    std::optional<std::string_view> get_active_session_id() const;

    std::string_view get_current_state_name() const { return _current_state->get_state_name(); }

private:
    std::optional<SessionContext>    _active_session{std::nullopt};

    std::unique_ptr<IAtmState>       _current_state{std::make_unique<IdleState>()};
    std::unique_ptr<CashVault>       _vault;

    std::shared_ptr<DatabaseManager> _db;
    std::shared_ptr<AuditLog>        _audit_log;

    AuthService                      _auth_service;

    mutable std::mutex               _mtx;
};