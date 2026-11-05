/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: atm_state.hpp
 * Interface for ATM's FSM.
 *
 */

#pragma once

#include "enums.hpp"

#include <string_view>
#include <expected>

class AtmError;
class AtmController;

class IAtmState {
public:
    virtual ~IAtmState() = default;

    std::string_view get_state_name(this auto& self) noexcept {
        return self._get_state_name();
    }
    
    std::expected<void, AtmError> insert_card(AtmController& ctx, std::string_view card_number);
    std::expected<void, AtmError> enter_pin(AtmController& ctx, std::string_view pin);
    std::expected<void, AtmError> select_service(AtmController& ctx, ServiceType service);
    std::expected<void, AtmError> eject_card(AtmController& ctx);
    std::expected<void, AtmError> open_vault(AtmController& ctx, std::string_view engineer_key);
};

class IdleState : public IAtmState {
public:
    std::expected<void, AtmError> insert_card(AtmController& ctx, std::string_view card_number);
private:
    std::string_view _get_state_name() noexcept;
};

class CardInsertedState : public IAtmState {
public:
    std::expected<void, AtmError> enter_pin(AtmController& ctx, std::string_view pin);
    std::expected<void, AtmError> eject_card(AtmController& ctx);
private:
    std::string_view _get_state_name() noexcept;
};

class CustomerSessionState : public IAtmState {
public:
    std::expected<void, AtmError> select_service(AtmController& ctx, ServiceType service);
    std::expected<void, AtmError> eject_card(AtmController& ctx);
private:
    std::string_view _get_state_name() noexcept;
};

class EngineerSessionState : public IAtmState {
public:
    std::expected<void, AtmError> open_vault(AtmController& ctx, std::string_view engineer_key);
    std::expected<void, AtmError> eject_card(AtmController& ctx);
private:
    std::string_view _get_state_name() noexcept;
};