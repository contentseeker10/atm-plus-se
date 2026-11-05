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

std::string_view IdleState::get_state_name()            noexcept { return "Idle"; }
std::string_view CardInsertedState::get_state_name()    noexcept { return "CardInserted"; }
std::string_view CustomerSessionState::get_state_name() noexcept { return "CustomerSession"; }
std::string_view EngineerSessionState::get_state_name() noexcept { return "EngineerSession"; }

