/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: atm_state.cpp
 * Implementation of ATM's FSM.
 *
 */

#include "atm_state.hpp"
#include "atm_controller.hpp"

std::expected<void, AtmError> IdleState::insert_card(AtmController& ctx,
                                                     std::string_view card_number)
{
    auto record = ctx.validate_card(card_number);
    if (!record) {
        return std::unexpected(record.error());
    }
    ctx.start_session(record.value());
    ctx.transition_to(std::make_unique<CardInsertedState>());
    return {};
}

std::expected<void, AtmError> CardInsertedState::enter_pin(AtmController& ctx,
                                                           std::string_view pin)
{
    auto record = ctx.authenticate_card(pin);
    if (!record)
    {
        if (record.error() == AtmError::CardBlocked || record.error() == AtmError::MaxAttemptsExceeded) {
            ctx.end_session();
            ctx.transition_to(std::make_unique<IdleState>());
        }
        return std::unexpected(record.error());
    }

    ctx.update_session(record.value());

    auto role = ctx.get_active_role();
    if (role == UserRole::Customer)
    {
        ctx.transition_to(std::make_unique<CustomerSessionState>());
    }
    else if (role == UserRole::Engineer)
    {
        ctx.transition_to(std::make_unique<EngineerSessionState>());
    }
    else
    {
        return std::unexpected(AtmError::InternalError);
    }
    return {};
}

std::expected<void, AtmError> CardInsertedState::eject_card(AtmController& /*ctx*/)
{ return {}; }

std::expected<void, AtmError> CustomerSessionState::select_service(
    AtmController& /*ctx*/, ServiceType /*service*/)
{ return {}; }

std::expected<void, AtmError> CustomerSessionState::eject_card(AtmController& /*ctx*/)
{ return {}; }

std::expected<void, AtmError> EngineerSessionState::open_vault(
    AtmController& /*ctx*/, std::string_view /*engineer_key*/)
{ return {}; }

std::expected<void, AtmError> EngineerSessionState::eject_card(AtmController& /*ctx*/)
{ return {}; }