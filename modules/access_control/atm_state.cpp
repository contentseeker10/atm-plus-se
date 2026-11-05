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

std::string_view IdleState::_get_state_name()            noexcept { return "Idle"; }
std::string_view CardInsertedState::_get_state_name()    noexcept { return "CardInserted"; }
std::string_view CustomerSessionState::_get_state_name() noexcept { return "CustomerSession"; }
std::string_view EngineerSessionState::_get_state_name() noexcept { return "EngineerSession"; }

