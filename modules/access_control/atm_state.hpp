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

class AtmController;

class IAtmState {
public:
    virtual ~IAtmState() = default;

    [[nodiscard]] virtual std::string_view get_state_name() const noexcept = 0;
    
    virtual std::expected<void, AtmError> insert_card(AtmController& ctx, std::string_view card_number) {
        return std::unexpected(AtmError::InvalidStateTransition);
    }
    virtual std::expected<void, AtmError> enter_pin(AtmController& ctx, std::string_view pin) {
        return std::unexpected(AtmError::InvalidStateTransition);
    }
    virtual std::expected<void, AtmError> select_service(AtmController& ctx, ServiceType service) {
        return std::unexpected(AtmError::InvalidStateTransition);
    }
    virtual std::expected<void, AtmError> eject_card(AtmController& ctx) {
        return std::unexpected(AtmError::InvalidStateTransition);
    }
    virtual std::expected<void, AtmError> open_vault(AtmController& ctx, std::string_view engineer_key) {
        return std::unexpected(AtmError::InvalidStateTransition);
    }
};

class IdleState : public IAtmState {
public:
    [[nodiscard]] std::string_view get_state_name() const noexcept override { return "Idle"; };
    
    std::expected<void, AtmError> insert_card(AtmController& ctx, std::string_view card_number) override;
};

class CardInsertedState : public IAtmState {
public:
    [[nodiscard]] std::string_view get_state_name() const noexcept override { return "CardInserted"; };
    
    std::expected<void, AtmError> enter_pin(AtmController& ctx, std::string_view pin) override;
    std::expected<void, AtmError> eject_card(AtmController& ctx) override;
};

class CustomerSessionState : public IAtmState {
public:
    [[nodiscard]] std::string_view get_state_name() const noexcept override { return "CustomerSession"; };
    
    std::expected<void, AtmError> select_service(AtmController& ctx, ServiceType service) override;
    std::expected<void, AtmError> eject_card(AtmController& ctx) override;
};

class EngineerSessionState : public IAtmState {
public:
    [[nodiscard]] std::string_view get_state_name() const noexcept override { return "EngineerSession"; };
    
    std::expected<void, AtmError> open_vault(AtmController& ctx, std::string_view engineer_key) override;
    std::expected<void, AtmError> eject_card(AtmController& ctx) override;
};