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

std::expected<void, AtmError> IdleState::insert_card(AtmController& ctx, std::string_view card_number) {
    ctx.start_session(card_number);
    ctx.transition_to(std::make_unique<CardInsertedState>());
    return {};
}

std::expected<void, AtmError> CardInsertedState::enter_pin(AtmController& ctx, std::string_view pin) {
    auto role = ctx.authenticate_current_card(pin);
    if (!role.has_value()) {
        return std::unexpected(role.error());
    }
    if (role.value() == UserRole::Customer) {
        ctx.update_role(UserRole::Customer);
        ctx.transition_to(std::make_unique<CustomerSessionState>());
    } else if (role.value() == UserRole::Engineer) {
        ctx.update_role(UserRole::Engineer);
        ctx.transition_to(std::make_unique<EngineerSessionState>());
    } else {
        return std::unexpected(AtmError::InternalError);
    }
    return {};
}

std::expected<void, AtmError> CardInsertedState::eject_card(AtmController& /*ctx*/) {
    return {};
}

std::expected<void, AtmError> CustomerSessionState::select_service(AtmController& /*ctx*/, ServiceType /*service*/) {
    return {};
}

std::expected<void, AtmError> CustomerSessionState::eject_card(AtmController& /*ctx*/) {
    return {};
}

std::expected<void, AtmError> EngineerSessionState::open_vault(AtmController& /*ctx*/, std::string_view /*engineer_key*/) {
    return {};
}

std::expected<void, AtmError> EngineerSessionState::eject_card(AtmController& /*ctx*/) {
    return {};
}