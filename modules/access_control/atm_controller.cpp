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

#include "crypto.hpp"

#include "cash/cash_vault.hpp"

AtmController::AtmController(std::unique_ptr<CashVault> vault,
                             std::shared_ptr<DatabaseManager> db,
                             std::shared_ptr<AuditLog> log, std::unique_ptr<AuthService> auth)
    : _vault{std::move(vault)}, _db{std::move(db)}, _audit_log{std::move(log)},
      _auth_service{std::move(auth)}
{ }
AtmController::~AtmController() = default;

void AtmController::transition_to(std::unique_ptr<IAtmState> new_state)
{
    _current_state = std::move(new_state);
}

void AtmController::start_session(const CardRecord& card)
{
    std::unique_lock lock(_mtx);
    auto session_id = crypto::generate_uuid_v4();
    SessionContext session(session_id, card);
    _active_session.emplace(session);
}

void AtmController::update_session(const CardRecord& card) {
    std::unique_lock lock(_mtx);
    if (_active_session.has_value()) {
        _active_session->record = card;
    }
}

void AtmController::end_session() noexcept
{
    std::unique_lock lock(_mtx);
    _active_session.reset();
}

std::expected<void, AtmError> AtmController::process_insert_card(std::string_view card)
{
    std::unique_lock lock(_mtx);
    return _current_state->insert_card(*this, card);
}

std::expected<void, AtmError> AtmController::process_enter_pin(std::string_view pin)
{
    std::unique_lock lock(_mtx);
    auto result = _current_state->enter_pin(*this, pin);
    return result;
}

std::expected<void, AtmError> AtmController::process_eject_card() {
    std::unique_lock lock(_mtx);
    return _current_state->eject_card(*this);
}

std::expected<CardRecord, AtmError> AtmController::validate_card(std::string_view card) {
    std::unique_lock lock(_mtx);

    auto result = _auth_service->validate_card(card);
    if (!result) {
        return std::unexpected(to_atm_error(result.error()));
    }

    return result.value();
}

std::expected<void, AtmError> AtmController::validate_session()
{
    std::unique_lock lock(_mtx);

    if (!_active_session.has_value())
    {
        return std::unexpected(AtmError::NoActiveSession);
    }
    if (_active_session->is_expired(kTimeout))
    {
        end_session();
        transition_to(std::make_unique<IdleState>());
        return std::unexpected(AtmError::SessionExpired);
    }
    _active_session->touch();
    return {};
}

std::expected<CardRecord, AtmError> AtmController::authenticate_card(
    std::string_view pin)
{
    std::unique_lock lock(_mtx);

    auto session = validate_session();
    if (!session) {
        return std::unexpected(session.error());
    }

    auto auth_result = _auth_service->authenticate_card(_active_session->record.number, pin);
    if (!auth_result.has_value())
    {
        return std::unexpected(to_atm_error(auth_result.error()));
    }
    return auth_result.value();
}

std::optional<std::string> AtmController::get_active_session_id() const
{
    std::unique_lock lock(_mtx);
    
    if (!_active_session.has_value()) { return std::nullopt; }
    if (_active_session->is_expired(kTimeout)) { return std::nullopt; }
    return _active_session->session_id;
}

UserRole AtmController::get_active_role() const {
    std::unique_lock lock(_mtx);
    if (!_active_session.has_value() || _active_session->is_expired(kTimeout)) { 
        return UserRole::None; 
    }
    return _active_session->record.role;
}