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
#include "card.hpp"
#include "session_context.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

using namespace std::literals::chrono_literals;

class Account;
class CashVault;
class AuditLog;
class LedgerService;

class AtmController
{
public:
    AtmController(std::unique_ptr<CashVault> vault, std::shared_ptr<DatabaseManager> db,
                  std::shared_ptr<AuditLog> log, std::unique_ptr<AuthService> auth,
                  std::shared_ptr<LedgerService> ledger = nullptr);
    ~AtmController();

    AtmController(const AtmController&) = delete;
    AtmController& operator=(const AtmController&) = delete;

    AtmController(AtmController&&) = delete;
    AtmController& operator=(AtmController&&) = delete;

    void transition_to(std::unique_ptr<IAtmState> new_state);
    void start_session(const CardRecord& card);
    void update_session(const CardRecord& card);
    void end_session() noexcept;

    std::expected<void, AtmError> process_insert_card(std::string_view card);
    std::expected<void, AtmError> process_enter_pin(std::string_view pin);
    std::expected<void, AtmError> process_select_service(ServiceType service);
    std::expected<void, AtmError> process_eject_card();
    std::expected<void, AtmError> process_open_vault(std::string_view engineer_key);
    std::expected<void, AtmError> process_withdraw(int64_t amount_cents);
    std::expected<void, AtmError> process_deposit(const std::vector<Denomination>& bills);
    std::expected<void, AtmError> process_replenish(Denomination denomination, int count);

    std::expected<void, AtmError> open_vault_door();
    std::expected<void, AtmError> close_vault_door();
    bool verify_engineer_key(std::string_view engineer_key) const noexcept;

    const CashVault& get_vault() const noexcept { return *_vault; }

    std::expected<CardRecord, AtmError> validate_card(std::string_view card);
    std::expected<CardRecord, AtmError> authenticate_card(std::string_view pin);
    std::expected<void, AtmError>       validate_session();

    std::optional<std::string> get_active_session_id() const;
    UserRole                   get_active_role()       const;

    std::string_view get_current_state_name() const
    { return _current_state->get_state_name(); }

private:
    inline static constexpr std::chrono::seconds kTimeout = 60s;
    inline static constexpr std::string_view kDefaultEngineerKey = "ENG-MASTER-KEY-2026";

    std::expected<void, AtmError> require_role(UserRole role);
    std::expected<void, AtmError> commit_cash_operation(
        CashVault next_vault, const std::vector<const Account*>& accounts,
        std::uint64_t nonce, std::size_t audit_mark);

    std::shared_ptr<DatabaseManager> _db;
    std::shared_ptr<AuditLog>        _audit_log;
    std::shared_ptr<LedgerService>   _ledger;

    std::optional<SessionContext> _active_session{std::nullopt};

    std::unique_ptr<IAtmState>   _current_state{std::make_unique<IdleState>()};
    std::unique_ptr<CashVault>   _vault;
    std::unique_ptr<AuthService> _auth_service;

    std::uint64_t _tx_nonce{0};

    mutable std::recursive_mutex _mtx;
};