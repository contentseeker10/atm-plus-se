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
    // TODO: somehow select user role Customer/Engineer
    auto role = UserRole::Customer;

    ctx.start_session(card_number, role);
    ctx.transition_to(std::make_unique<CardInsertedState>());
    return {};
}

std::expected<void, AtmError> CardInsertedState::enter_pin(AtmController& /*ctx*/, std::string_view /*pin*/) {
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