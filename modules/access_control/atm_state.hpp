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

#include <string_view>
#include <expected>

class AtmError;
class AtmController;

enum class ServiceType;

class IAtmState {
public:
    virtual ~IAtmState() = default;

    virtual std::string_view get_state_name() const noexcept = 0;
    
    std::expected<void, AtmError> insert_card(AtmController& ctx, std::string_view card_number);
    std::expected<void, AtmError> enter_pin(AtmController& ctx, std::string_view pin);
    std::expected<void, AtmError> select_service(AtmController& ctx, ServiceType service);
    std::expected<void, AtmError> eject_card(AtmController& ctx);
    std::expected<void, AtmError> open_vault(AtmController& ctx, std::string_view engineer_key);
};

class IdleState : public IAtmState {
    std::expected<void, AtmError> insert_card(AtmController& ctx, std::string_view card_number);
};

class CardInsertedState : public IAtmState {
    std::expected<void, AtmError> enter_pin(AtmController& ctx, std::string_view pin);
    std::expected<void, AtmError> eject_card(AtmController& ctx);
};

class CustomerSessionState : public IAtmState {
    std::expected<void, AtmError> select_service(AtmController& ctx, ServiceType service);
    std::expected<void, AtmError> eject_card(AtmController& ctx);
};

class EngineerSessionState : public IAtmState {
    std::expected<void, AtmError> open_vault(AtmController& ctx, std::string_view engineer_key);
    std::expected<void, AtmError> eject_card(AtmController& ctx);
};