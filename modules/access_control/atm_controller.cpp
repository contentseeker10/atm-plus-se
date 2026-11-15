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

#include "accounting/audit.hpp"
#include "accounting/ledger_service.hpp"

#include "accounts/checking_account.hpp"
#include "accounts/savings_account.hpp"

#include "cash/cash_vault.hpp"

#include "security/anti_replay.hpp"

#include "storage/operations.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

static_assert(std::is_nothrow_move_assignable_v<CashVault>);
static_assert(std::is_copy_constructible_v<CashVault>);

namespace
{
    constexpr AtmError to_atm_error(VaultError err) noexcept
    {
        switch (err)
        {
        case VaultError::DoorOpen: return AtmError::VaultDoorOpen;
        case VaultError::DoorClosed: return AtmError::VaultAccessDenied;
        case VaultError::CassetteFull:
        case VaultError::MissingCassette: return AtmError::CassetteFull;
        case VaultError::InvalidAmount:
        case VaultError::InvalidCount: return AtmError::InvalidAmount;
        case VaultError::NoExactCombination:
        case VaultError::InsufficientCash:
        case VaultError::InsufficientBills: return AtmError::InsufficientCashInAtm;
        }
        return AtmError::InternalError;
    }

    constexpr AtmError to_atm_error(LedgerError err) noexcept
    {
        switch (err)
        {
        case LedgerError::InvalidAmount: return AtmError::InvalidAmount;
        case LedgerError::InsufficientFunds:
        case LedgerError::NoOverdraftCoverage: return AtmError::InsufficientFunds;
        case LedgerError::ReplayDetected: return AtmError::ReplayAttackDetected;
        case LedgerError::IntegrityCompromised: return AtmError::CryptoVerificationFailed;
        case LedgerError::UnbalancedEntry:
        case LedgerError::AuditFailure:
        case LedgerError::InvalidRule: return AtmError::InternalError;
        }
        return AtmError::InternalError;
    }

    constexpr AtmError to_atm_error(DbError err) noexcept
    {
        return err == DbError::RecordNotFound ? AtmError::CardNotFound
                                              : AtmError::DatabaseError;
    }

    struct CustomerAccounts
    {
        std::unique_ptr<CheckingAccount> checking;
        std::unique_ptr<SavingsAccount> savings;
    };

    template <typename Derived>
    std::unique_ptr<Derived> downcast(std::unique_ptr<Account> account) noexcept
    {
        if (dynamic_cast<Derived*>(account.get()) == nullptr) return nullptr;
        return std::unique_ptr<Derived>{static_cast<Derived*>(account.release())};
    }

    std::expected<CustomerAccounts, AtmError> load_customer_accounts(
        DatabaseManager& db, std::string_view card_number)
    {
        CustomerAccounts accounts;
        std::optional<AtmError> failure;

        const auto tx = db.execute_transaction(
            [&](sqlite3* raw)
            {
                const auto ids = storage::load_card_accounts(raw, card_number);
                if (!ids)
                {
                    failure = to_atm_error(ids.error());
                    return false;
                }
                if (!ids->savings_id)
                {
                    failure = AtmError::DatabaseError;
                    return false;
                }

                auto checking = storage::load_account(raw, ids->checking_id);
                auto savings = storage::load_account(raw, *ids->savings_id);
                if (!checking || !savings)
                {
                    failure = AtmError::DatabaseError;
                    return false;
                }

                accounts.checking = downcast<CheckingAccount>(std::move(*checking));
                accounts.savings = downcast<SavingsAccount>(std::move(*savings));
                if (!accounts.checking || !accounts.savings)
                {
                    failure = AtmError::DatabaseError;
                    return false;
                }
                return true;
            });

        if (!tx) return std::unexpected(failure.value_or(AtmError::DatabaseError));
        return accounts;
    }

    std::map<Denomination, int> inventory_of(const CashVault& vault)
    {
        std::map<Denomination, int> inventory;
        const auto& cassettes = vault.get_cassettes();
        for (auto it = cassettes.cbegin(); it != cassettes.cend(); ++it)
        {
            inventory.emplace(it->first, it->second.get_count());
        }
        return inventory;
    }

    std::int64_t sum_of(const std::vector<Denomination>& bills) noexcept
    {
        std::int64_t total = 0;
        for (auto it = bills.cbegin(); it != bills.cend(); ++it)
        {
            total += static_cast<std::int64_t>(*it);
        }
        return total;
    }

    Account* resolve_own_account(std::string_view key,
                                 CustomerAccounts& accounts) noexcept
    {
        if (key == "Checking" || key == "checking" ||
            key == accounts.checking->get_account_id())
        {
            return accounts.checking.get();
        }
        if (key == "Savings" || key == "savings" ||
            key == accounts.savings->get_account_id())
        {
            return accounts.savings.get();
        }
        return nullptr;
    }

    CustomerAccountSnapshot make_account_snapshot(const CustomerAccounts& accounts)
    {
        return CustomerAccountSnapshot{accounts.checking->get_account_id(),
                                       accounts.checking->get_balance(),
                                       accounts.checking->get_client_min_cents(),
                                       accounts.checking->get_overflow_max_cents(),
                                       accounts.savings->get_account_id(),
                                       accounts.savings->get_balance()};
    }

    std::expected<Account*, AtmError> resolve_target_account(
        DatabaseManager& db, CustomerAccounts& accounts, std::string_view target,
        std::unique_ptr<Account>& external_owner)
    {
        if (Account* own = resolve_own_account(target, accounts))
        {
            return own;
        }
        if (auto loaded = db.load_account(target); loaded)
        {
            external_owner = std::move(*loaded);
            return external_owner.get();
        }
        auto card = db.load_card(target, false);
        if (!card) return std::unexpected(to_atm_error(card.error()));
        if (!card->account_id) return std::unexpected(AtmError::CardNotFound);

        auto card_account = db.load_account(*card->account_id);
        if (!card_account) return std::unexpected(to_atm_error(card_account.error()));

        external_owner = std::move(*card_account);
        return external_owner.get();
    }
}

AtmController::AtmController(std::unique_ptr<CashVault> vault,
                             std::shared_ptr<DatabaseManager> db,
                             std::shared_ptr<AuditLog> log,
                             std::unique_ptr<AuthService> auth,
                             std::shared_ptr<LedgerService> ledger,
                             std::shared_ptr<AntiReplayManager> anti_replay)
    : _db{std::move(db)}, _audit_log{std::move(log)}, _ledger{std::move(ledger)},
      _anti_replay{std::move(anti_replay)},
      _vault{std::move(vault)}, _auth_service{std::move(auth)}
{
    if (!_anti_replay && _ledger)
    {
        _anti_replay = _ledger->get_anti_replay();
    }
    if (!_anti_replay)
    {
        _anti_replay = std::make_shared<AntiReplayManager>();
    }
    if (!_ledger && _audit_log)
    {
        _ledger = std::make_shared<LedgerService>(_audit_log, _anti_replay);
    }
}
AtmController::~AtmController() = default;

void AtmController::transition_to(std::unique_ptr<IAtmState> new_state)
{
    _current_state = std::move(new_state);
}

void AtmController::start_session(const CardRecord& card, bool authenticated)
{
    std::unique_lock lock(_mtx);
    if (_active_session.has_value() && _anti_replay)
    {
        _anti_replay->reset_session(_active_session->session_id);
    }
    auto session_id = crypto::generate_uuid_v4();
    SessionContext session(session_id, card);
    _active_session.emplace(session);
    if (_anti_replay)
    {
        _anti_replay->register_session(session_id, 0);
    }
    if (authenticated)
    {
        if (card.role == UserRole::Customer)
        {
            transition_to(std::make_unique<CustomerSessionState>());
        }
        else if (card.role == UserRole::Engineer)
        {
            transition_to(std::make_unique<EngineerSessionState>());
        }
        else
        {
            transition_to(std::make_unique<CardInsertedState>());
        }
    }
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
    if (_active_session.has_value() && _anti_replay)
    {
        _anti_replay->reset_session(_active_session->session_id);
    }
    _active_session.reset();
    transition_to(std::make_unique<IdleState>());
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

std::expected<void, AtmError> AtmController::process_select_service(ServiceType service)
{
    std::unique_lock lock(_mtx);
    return _current_state->select_service(*this, service);
}

std::expected<void, AtmError> AtmController::process_eject_card() {
    std::unique_lock lock(_mtx);
    return _current_state->eject_card(*this);
}

std::expected<void, AtmError> AtmController::process_open_vault(
    std::string_view engineer_key)
{
    std::unique_lock lock(_mtx);
    return _current_state->open_vault(*this, engineer_key);
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

std::optional<CardRecord> AtmController::get_active_card() const
{
    std::unique_lock lock(_mtx);
    if (!_active_session.has_value() || _active_session->is_expired(kTimeout))
    {
        return std::nullopt;
    }
    return _active_session->record;
}

UserRole AtmController::get_active_role() const {
    std::unique_lock lock(_mtx);
    if (!_active_session.has_value() || _active_session->is_expired(kTimeout)) {
        return UserRole::None;
    }
    return _active_session->record.role;
}

int AtmController::get_remaining_pin_attempts(std::string_view card) const
{
    std::unique_lock lock(_mtx);
    if (!_auth_service) return 0;
    return _auth_service->get_remaining_attempts(card);
}

std::expected<WithdrawResult, AtmError> AtmController::process_withdraw(
    int64_t amount_cents, std::string_view account_type,
    std::string_view idempotency_key, std::optional<std::uint64_t> incoming_nonce)
{
    std::unique_lock lock(_mtx);

    if (auto session = require_role(UserRole::Customer); !session)
        return std::unexpected(session.error());
    if (!_ledger || !_audit_log) return std::unexpected(AtmError::InternalError);
    if (auto guard = check_customer_replay_guard(idempotency_key, incoming_nonce); !guard)
        return std::unexpected(guard.error());

    auto plan = _vault->calculate_dispense(amount_cents);
    if (!plan) return std::unexpected(to_atm_error(plan.error()));

    CashVault next_vault{*_vault};
    if (auto dispensed = next_vault.execute_dispense(*plan); !dispensed)
    {
        return std::unexpected(to_atm_error(dispensed.error()));
    }

    auto accounts = load_customer_accounts(*_db, _active_session->record.number);
    if (!accounts) return std::unexpected(accounts.error());

    Account* target = resolve_own_account(
        account_type.empty() ? std::string_view{"Checking"} : account_type, *accounts);
    if (!target) return std::unexpected(AtmError::Unauthorized);

    const bool is_checking = (target == accounts->checking.get());
    const std::int64_t savings_before = accounts->savings->get_balance();
    const std::uint64_t prev_session_nonce = _active_session->current_nonce;

    if (auto replayed = consume_session_nonce(incoming_nonce); !replayed)
        return std::unexpected(replayed.error());

    const std::uint64_t nonce = ++_tx_nonce;
    const std::size_t audit_mark = _audit_log->size();

    const auto posted =
        is_checking ? _ledger->withdraw_with_overdraft(
                          *accounts->checking, *accounts->savings, amount_cents, nonce,
                          idempotency_key)
                    : _ledger->withdraw_from_account(*accounts->savings, amount_cents, nonce,
                                                     idempotency_key);
    if (!posted)
    {
        rollback_session_nonce(prev_session_nonce);
        return std::unexpected(to_atm_error(posted.error()));
    }

    const std::vector<const Account*> updated_accounts =
        is_checking
            ? std::vector<const Account*>{accounts->checking.get(), accounts->savings.get()}
            : std::vector<const Account*>{accounts->savings.get()};

    if (auto committed =
            commit_operation(std::move(next_vault), updated_accounts, nonce, audit_mark);
        !committed)
    {
        rollback_session_nonce(prev_session_nonce);
        return std::unexpected(committed.error());
    }

    const std::int64_t overdraft_covered =
        is_checking
            ? std::max<std::int64_t>(0, savings_before - accounts->savings->get_balance())
            : 0;
    return WithdrawResult{std::move(*plan), overdraft_covered,
                          make_account_snapshot(*accounts), target->get_balance()};
}

std::expected<DepositResult, AtmError> AtmController::process_deposit(
    const std::vector<Denomination>& bills, std::string_view account_type,
    std::string_view idempotency_key, std::optional<std::uint64_t> incoming_nonce)
{
    std::unique_lock lock(_mtx);

    if (auto session = require_role(UserRole::Customer); !session)
        return std::unexpected(session.error());
    if (!_ledger || !_audit_log) return std::unexpected(AtmError::InternalError);
    if (auto guard = check_customer_replay_guard(idempotency_key, incoming_nonce); !guard)
        return std::unexpected(guard.error());

    CashVault next_vault{*_vault};
    if (auto accepted = next_vault.deposit_bills(bills); !accepted)
    {
        return std::unexpected(to_atm_error(accepted.error()));
    }

    auto accounts = load_customer_accounts(*_db, _active_session->record.number);
    if (!accounts) return std::unexpected(accounts.error());

    Account* target = resolve_own_account(
        account_type.empty() ? std::string_view{"Checking"} : account_type, *accounts);
    if (!target) return std::unexpected(AtmError::Unauthorized);

    const bool is_checking = (target == accounts->checking.get());
    const std::int64_t total_deposited = sum_of(bills);
    const std::int64_t savings_before = accounts->savings->get_balance();
    const std::uint64_t prev_session_nonce = _active_session->current_nonce;

    if (auto replayed = consume_session_nonce(incoming_nonce); !replayed)
        return std::unexpected(replayed.error());

    const std::uint64_t nonce = ++_tx_nonce;
    const std::size_t audit_mark = _audit_log->size();

    const auto posted =
        is_checking ? _ledger->deposit_with_overflow(
                          *accounts->checking, *accounts->savings, total_deposited, nonce,
                          idempotency_key)
                    : _ledger->deposit_to_account(*accounts->savings, total_deposited, nonce,
                                                  idempotency_key);
    if (!posted)
    {
        rollback_session_nonce(prev_session_nonce);
        return std::unexpected(to_atm_error(posted.error()));
    }

    const std::vector<const Account*> updated_accounts =
        is_checking
            ? std::vector<const Account*>{accounts->checking.get(), accounts->savings.get()}
            : std::vector<const Account*>{accounts->savings.get()};

    if (auto committed =
            commit_operation(std::move(next_vault), updated_accounts, nonce, audit_mark);
        !committed)
    {
        rollback_session_nonce(prev_session_nonce);
        return std::unexpected(committed.error());
    }

    const std::int64_t overflow_swept =
        is_checking
            ? std::max<std::int64_t>(0, accounts->savings->get_balance() - savings_before)
            : 0;
    return DepositResult{total_deposited, overflow_swept, make_account_snapshot(*accounts),
                         target->get_balance()};
}

std::expected<TransferResult, AtmError> AtmController::process_transfer(
    std::string_view from_account, std::string_view to_account,
    std::int64_t amount_cents, std::string_view idempotency_key,
    std::optional<std::uint64_t> incoming_nonce)
{
    std::unique_lock lock(_mtx);

    if (auto session = require_role(UserRole::Customer); !session)
        return std::unexpected(session.error());
    if (!_ledger || !_audit_log) return std::unexpected(AtmError::InternalError);
    if (auto guard = check_customer_replay_guard(idempotency_key, incoming_nonce); !guard)
        return std::unexpected(guard.error());
    if (amount_cents <= 0) return std::unexpected(AtmError::InvalidAmount);

    auto accounts = load_customer_accounts(*_db, _active_session->record.number);
    if (!accounts) return std::unexpected(accounts.error());

    Account* from = resolve_own_account(from_account, *accounts);
    if (!from) return std::unexpected(AtmError::Unauthorized);

    std::unique_ptr<Account> external_to;
    auto target_res =
        resolve_target_account(*_db, *accounts, to_account, external_to);
    if (!target_res) return std::unexpected(target_res.error());
    Account* to = *target_res;

    if (from->get_account_id() == to->get_account_id())
    {
        return std::unexpected(AtmError::InvalidAmount);
    }

    SavingsAccount* from_linked_savings =
        (from == accounts->checking.get() && to != accounts->savings.get())
            ? accounts->savings.get()
            : nullptr;

    std::unique_ptr<SavingsAccount> external_to_savings;
    SavingsAccount* to_linked_savings = nullptr;
    if (to == accounts->checking.get())
    {
        to_linked_savings = accounts->savings.get();
    }
    else if (const auto* chk_to = dynamic_cast<const CheckingAccount*>(to);
             chk_to && chk_to->get_linked_savings_id())
    {
        if (auto loaded_sav = _db->load_account(*chk_to->get_linked_savings_id()))
        {
            external_to_savings = downcast<SavingsAccount>(std::move(*loaded_sav));
            to_linked_savings = external_to_savings.get();
        }
    }

    const std::uint64_t prev_session_nonce = _active_session->current_nonce;
    if (auto replayed = consume_session_nonce(incoming_nonce); !replayed)
        return std::unexpected(replayed.error());

    const std::uint64_t nonce = ++_tx_nonce;
    const std::size_t audit_mark = _audit_log->size();

    const auto outcome = _ledger->transfer_with_protection(
        *from, *to, amount_cents, from_linked_savings, to_linked_savings, nonce,
        idempotency_key);
    if (!outcome)
    {
        rollback_session_nonce(prev_session_nonce);
        return std::unexpected(to_atm_error(outcome.error()));
    }

    std::vector<const Account*> updated_accounts{from, to};
    auto add_unique_account = [&updated_accounts](const Account* acc)
    {
        if (acc &&
            std::find(updated_accounts.begin(), updated_accounts.end(), acc) ==
                updated_accounts.end())
        {
            updated_accounts.push_back(acc);
        }
    };
    if (outcome->overdraft_covered_cents > 0) add_unique_account(from_linked_savings);
    if (outcome->overflow_swept_cents > 0) add_unique_account(to_linked_savings);

    if (auto committed = commit_operation(std::nullopt, updated_accounts, nonce, audit_mark);
        !committed)
    {
        rollback_session_nonce(prev_session_nonce);
        return std::unexpected(committed.error());
    }

    return TransferResult{from->get_balance(), make_account_snapshot(*accounts),
                          outcome->overdraft_covered_cents,
                          outcome->overflow_swept_cents};
}

std::expected<void, AtmError> AtmController::process_replenish(Denomination denomination,
                                                               int count)
{
    std::unique_lock lock(_mtx);

    if (auto session = require_role(UserRole::Engineer); !session) return session;
    if (!_ledger || !_audit_log) return std::unexpected(AtmError::InternalError);
    if (!_ledger->integrity_ok())
        return std::unexpected(AtmError::CryptoVerificationFailed);

    CashVault next_vault{*_vault};
    if (auto loaded = next_vault.replenish(denomination, count); !loaded)
    {
        return std::unexpected(to_atm_error(loaded.error()));
    }

    if (auto replayed = consume_session_nonce(); !replayed) return replayed;

    const std::uint64_t nonce = ++_tx_nonce;
    const std::size_t audit_mark = _audit_log->size();

    LedgerEntry entry;
    entry.debit_account_id = std::string(ATM_VAULT_ACCOUNT_ID);
    entry.credit_account_id = std::string(BANK_TREASURY_ACCOUNT_ID);
    entry.amount_cents = static_cast<std::int64_t>(denomination) * count;
    entry.operation_type = "REPLENISHMENT";
    entry.nonce = nonce;

    if (auto posted = _audit_log->append_record(entry); !posted)
    {
        return std::unexpected(posted.error() == CryptoError::DatabaseError
                                   ? AtmError::DatabaseError
                                   : AtmError::InternalError);
    }

    return commit_operation(std::move(next_vault), {}, nonce, audit_mark);
}

std::expected<CustomerAccountSnapshot, AtmError> AtmController::get_customer_accounts()
{
    std::unique_lock lock(_mtx);

    if (auto session = require_role(UserRole::Customer); !session)
        return std::unexpected(session.error());

    auto accounts = load_customer_accounts(*_db, _active_session->record.number);
    if (!accounts) return std::unexpected(accounts.error());

    return make_account_snapshot(*accounts);
}

std::expected<std::vector<ApsRule>, AtmError> AtmController::get_customer_aps_rules()
{
    std::unique_lock lock(_mtx);

    if (auto session = require_role(UserRole::Customer); !session)
        return std::unexpected(session.error());
    if (!_ledger) return std::unexpected(AtmError::InternalError);

    auto accounts = load_customer_accounts(*_db, _active_session->record.number);
    if (!accounts) return std::unexpected(accounts.error());

    const std::string& checking_id = accounts->checking->get_account_id();
    const std::string& savings_id = accounts->savings->get_account_id();

    std::vector<ApsRule> rules;
    for (auto& rule : _ledger->get_aps_rules())
    {
        if (rule.payer_account_id == checking_id || rule.payer_account_id == savings_id)
        {
            rules.push_back(std::move(rule));
        }
    }
    return rules;
}

std::expected<ApsRule, AtmError> AtmController::create_customer_aps_rule(
    std::string_view from_account, std::string_view to_account,
    std::int64_t amount_cents, bool fixed_amount, std::int64_t period_seconds)
{
    std::unique_lock lock(_mtx);

    if (auto session = require_role(UserRole::Customer); !session)
        return std::unexpected(session.error());
    if (!_ledger) return std::unexpected(AtmError::InternalError);
    if (!_ledger->integrity_ok())
        return std::unexpected(AtmError::CryptoVerificationFailed);
    if (from_account.empty() || to_account.empty() || amount_cents <= 0 ||
        period_seconds < 0)
    {
        return std::unexpected(AtmError::InvalidAmount);
    }

    auto accounts = load_customer_accounts(*_db, _active_session->record.number);
    if (!accounts) return std::unexpected(accounts.error());

    Account* from = resolve_own_account(from_account, *accounts);
    if (!from) return std::unexpected(AtmError::Unauthorized);

    std::unique_ptr<Account> external_to;
    auto target_res = resolve_target_account(*_db, *accounts, to_account, external_to);
    if (!target_res) return std::unexpected(target_res.error());
    Account* to = *target_res;

    if (from->get_account_id() == to->get_account_id())
    {
        return std::unexpected(AtmError::InvalidAmount);
    }

    ApsRule rule;
    rule.rule_id = "APS-" + crypto::generate_uuid_v4().substr(0, 8);
    rule.payer_account_id = from->get_account_id();
    rule.payee_account_id = to->get_account_id();
    rule.amount_cents = amount_cents;
    rule.fixed_amount = fixed_amount;
    rule.period = std::chrono::seconds{period_seconds};

    if (auto reg = _ledger->register_aps_rule(rule); !reg)
    {
        return std::unexpected(to_atm_error(reg.error()));
    }
    return rule;
}

std::expected<CustomerApsRunResult, AtmError> AtmController::run_customer_aps()
{
    std::unique_lock lock(_mtx);

    if (auto session = require_role(UserRole::Customer); !session)
        return std::unexpected(session.error());
    if (!_ledger) return std::unexpected(AtmError::InternalError);
    if (!_ledger->integrity_ok())
        return std::unexpected(AtmError::CryptoVerificationFailed);

    ApsRunResult run = _ledger->run_aps(std::chrono::system_clock::now(), *_db);

    auto accounts = load_customer_accounts(*_db, _active_session->record.number);
    if (!accounts) return std::unexpected(accounts.error());

    return CustomerApsRunResult{std::move(run), make_account_snapshot(*accounts)};
}

std::expected<void, AtmError> AtmController::delete_customer_aps_rule(
    std::string_view rule_id)
{
    std::unique_lock lock(_mtx);

    if (auto session = require_role(UserRole::Customer); !session)
        return std::unexpected(session.error());
    if (!_ledger) return std::unexpected(AtmError::InternalError);
    if (rule_id.empty()) return std::unexpected(AtmError::InvalidAmount);

    auto accounts = load_customer_accounts(*_db, _active_session->record.number);
    if (!accounts) return std::unexpected(accounts.error());

    const std::string& checking_id = accounts->checking->get_account_id();
    const std::string& savings_id = accounts->savings->get_account_id();

    bool found = false;
    bool owned = false;
    for (const auto& rule : _ledger->get_aps_rules())
    {
        if (rule.rule_id == rule_id)
        {
            found = true;
            owned = (rule.payer_account_id == checking_id ||
                     rule.payer_account_id == savings_id);
            break;
        }
    }

    if (!found) return std::unexpected(AtmError::InvalidAmount);
    if (!owned) return std::unexpected(AtmError::Unauthorized);
    if (!_ledger->remove_aps_rule(rule_id))
        return std::unexpected(AtmError::InternalError);
    return {};
}

std::expected<std::vector<LedgerEntry>, AtmError> AtmController::get_audit_entries(
    std::size_t offset, std::size_t limit)
{
    std::unique_lock lock(_mtx);

    if (auto session = require_role(UserRole::Engineer); !session)
        return std::unexpected(session.error());
    if (!_audit_log) return std::unexpected(AtmError::InternalError);

    if (offset == 0 && limit == 0)
    {
        return _audit_log->entries();
    }
    return _audit_log->entries_page(offset, limit);
}

std::expected<ChainVerification, AtmError> AtmController::verify_audit_integrity()
{
    std::unique_lock lock(_mtx);

    if (auto session = require_role(UserRole::Engineer); !session)
        return std::unexpected(session.error());
    if (!_audit_log) return std::unexpected(AtmError::InternalError);

    auto result = _audit_log->verify_integrity_chain();
    if (!result) return std::unexpected(AtmError::CryptoVerificationFailed);
    if (!result->ok && _ledger)
    {
        _ledger->set_integrity_compromised(true);
    }
    return *result;
}

bool AtmController::is_audit_entry_tampered(const LedgerEntry& entry) const
{
    return _audit_log ? _audit_log->is_entry_tampered(entry) : false;
}

std::expected<void, AtmError> AtmController::simulate_audit_tamper(bool restore)
{
    std::unique_lock lock(_mtx);

    if (auto session = require_role(UserRole::Engineer); !session)
        return std::unexpected(session.error());
    if (!_audit_log || !_ledger) return std::unexpected(AtmError::InternalError);

    if (restore)
    {
        _audit_log->restore_tampered_entries();
        _ledger->set_integrity_compromised(false);
        return {};
    }

    const auto entries = _audit_log->entries();
    if (entries.empty()) return std::unexpected(AtmError::InvalidAmount);

    const std::size_t target_index = entries.size() - 1;
    const std::int64_t tampered_amount =
        entries[target_index].amount_cents == 99999900 ? 88888800 : 99999900;
    _audit_log->debug_tamper_amount(target_index, tampered_amount);
    return {};
}

std::expected<void, AtmError> AtmController::open_vault_door()
{
    std::unique_lock lock(_mtx);

    if (auto session = require_role(UserRole::Engineer); !session) return session;

    _vault->open_door();
    return {};
}

std::expected<void, AtmError> AtmController::close_vault_door()
{
    std::unique_lock lock(_mtx);

    if (auto session = require_role(UserRole::Engineer); !session) return session;

    _vault->close_door();
    return {};
}

bool AtmController::verify_engineer_key(std::string_view engineer_key) const noexcept
{
    const char* env_key = std::getenv("ATM_ENGINEER_MASTER_KEY");
    const std::string_view expected =
        (env_key != nullptr && *env_key != '\0') ? std::string_view{env_key} : kDefaultEngineerKey;
    return crypto::constant_time_compare(engineer_key, expected);
}

std::expected<void, AtmError> AtmController::require_role(UserRole role)
{
    if (auto session = validate_session(); !session) return session;
    switch (role)
    {
    case UserRole::Customer: return _current_state->verify_customer_access(*this);
    case UserRole::Engineer: return _current_state->verify_engineer_access(*this);
    case UserRole::None: break;
    }
    return std::unexpected(AtmError::Unauthorized);
}

std::expected<void, AtmError> AtmController::check_customer_replay_guard(
    std::string_view idempotency_key, std::optional<std::uint64_t> incoming_nonce) const
{
    if (!_ledger->integrity_ok())
    {
        return std::unexpected(AtmError::CryptoVerificationFailed);
    }
    if (incoming_nonce.has_value() && *incoming_nonce <= _active_session->current_nonce)
    {
        return std::unexpected(AtmError::ReplayAttackDetected);
    }
    if (!idempotency_key.empty() && _anti_replay &&
        _anti_replay->is_key_seen(idempotency_key))
    {
        return std::unexpected(AtmError::ReplayAttackDetected);
    }
    return {};
}

std::expected<void, AtmError> AtmController::consume_session_nonce(
    std::optional<std::uint64_t> incoming_nonce)
{
    if (!_active_session.has_value()) return std::unexpected(AtmError::NoActiveSession);
    if (incoming_nonce.has_value())
    {
        if (_anti_replay)
        {
            if (!_anti_replay->validate_and_consume(_active_session->session_id,
                                                    *incoming_nonce))
            {
                return std::unexpected(AtmError::ReplayAttackDetected);
            }
        }
        else if (*incoming_nonce <= _active_session->current_nonce)
        {
            return std::unexpected(AtmError::ReplayAttackDetected);
        }
        _active_session->current_nonce = *incoming_nonce;
        return {};
    }
    const std::uint64_t next = _active_session->next_nonce();
    if (_anti_replay &&
        !_anti_replay->validate_and_consume(_active_session->session_id, next))
    {
        --_active_session->current_nonce;
        return std::unexpected(AtmError::ReplayAttackDetected);
    }
    return {};
}

void AtmController::rollback_session_nonce(std::uint64_t previous_nonce)
{
    if (!_active_session.has_value()) return;
    _active_session->current_nonce = previous_nonce;
    if (_anti_replay)
    {
        _anti_replay->register_session(_active_session->session_id, previous_nonce);
    }
}

std::expected<void, AtmError> AtmController::commit_operation(
    std::optional<CashVault> next_vault, const std::vector<const Account*>& accounts,
    std::uint64_t nonce, std::size_t audit_mark)
{
    const auto inventory = next_vault ? std::optional{inventory_of(*next_vault)} : std::nullopt;

    const auto saved = _db->execute_transaction(
        [&](sqlite3* raw)
        {
            for (const Account* acc : accounts)
            {
                if (!storage::save_account(raw, *acc)) return false;
            }
            if (inventory && !storage::save_vault_inventory(raw, *inventory)) return false;
            return true;
        });

    if (!saved)
    {
        if (!_ledger || !_ledger->reverse_entries(nonce, audit_mark))
            return std::unexpected(AtmError::InternalError);
        return std::unexpected(AtmError::DatabaseError);
    }

    if (next_vault)
    {
        *_vault = std::move(*next_vault);
    }
    return {};
}
