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
constexpr size_t MAX_ATTEMPTS = 3;

AtmController::AtmController(std::unique_ptr<CashVault> vault,
                             std::shared_ptr<DatabaseManager> db,
                             std::shared_ptr<AuditLog> log, AuthService auth)
    : _vault{std::move(vault)}, _db{std::move(db)}, _audit_log{std::move(log)},
      _auth_service{std::move(auth)}
{ }
AtmController::~AtmController() = default;

void AtmController::transition_to(std::unique_ptr<IAtmState> new_state)
{
    // std::unique_lock lock(_mtx); // assumed locked externally
    _current_state = std::move(new_state);
}

void AtmController::start_session(std::string_view card)
{
    std::unique_lock lock(_mtx);

    // TODO: generate session id?
    std::string session_id("example");

    SessionContext session(session_id, std::string(card));
    _active_session.emplace(session);
}

void AtmController::end_session() noexcept
{
    std::unique_lock lock(_mtx);
    _active_session.reset();
}

void AtmController::update_role(UserRole new_role) { _active_session->role = new_role; }

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

std::expected<void, AtmError> AtmController::validate_active_session()
{
    std::unique_lock lock(_mtx);

    if (!_active_session.has_value())
    {
        return std::unexpected(AtmError::NoActiveSession);
    }
    if (_active_session->is_expired(TIMEOUT))
    {
        end_session();
        return std::unexpected(AtmError::SessionExpired);
    }
    _active_session->touch();
    return {};
}

std::expected<UserRole, AtmError> AtmController::authenticate_current_card(
    std::string_view pin)
{
    auto auth_result = _auth_service.authenticate_card(_active_session->card_number, pin);
    if (!auth_result.has_value())
    {
        return std::unexpected(to_atm_error(auth_result.error()));
    }
    return auth_result.value();
}

std::optional<std::string_view> AtmController::get_active_session_id() const
{
    std::unique_lock lock(_mtx);

    if (!_active_session.has_value()) { return std::nullopt; }
    if (_active_session->is_expired(TIMEOUT)) { return std::nullopt; }
    return _active_session->session_id;
}