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

#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

inline constexpr std::string_view BANK_TREASURY_ACCOUNT_ID = "BANK_TREASURY";

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

    std::expected<void, CryptoError> reverse_entries(AuditLog& log, std::uint64_t nonce,
                                                     std::size_t audit_mark)
    {
        const std::vector<LedgerEntry> entries = log.entries();

        std::vector<LedgerEntry> reversal;
        for (std::size_t i = entries.size(); i > audit_mark; --i)
        {
            const LedgerEntry& original = entries[i - 1];
            if (original.nonce != nonce) continue;

            LedgerEntry entry;
            entry.debit_account_id = original.credit_account_id;
            entry.credit_account_id = original.debit_account_id;
            entry.amount_cents = original.amount_cents;
            entry.operation_type = original.operation_type + "_REVERSAL";
            entry.nonce = nonce;
            reversal.push_back(std::move(entry));
        }

        return log.append_batch(reversal);
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
} // namespace

AtmController::AtmController(std::unique_ptr<CashVault> vault,
                             std::shared_ptr<DatabaseManager> db,
                             std::shared_ptr<AuditLog> log,
                             std::unique_ptr<AuthService> auth,
                             std::shared_ptr<LedgerService> ledger)
    : _db{std::move(db)}, _audit_log{std::move(log)}, _ledger{std::move(ledger)},
      _anti_replay{std::make_shared<AntiReplayManager>()},
      _vault{std::move(vault)}, _auth_service{std::move(auth)}
{
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

void AtmController::start_session(const CardRecord& card)
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
        transition_to(std::make_unique<IdleState>());
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

std::expected<void, AtmError> AtmController::process_withdraw(int64_t amount_cents)
{
    std::unique_lock lock(_mtx);

    if (auto session = require_role(UserRole::Customer); !session) return session;
    if (!_ledger || !_audit_log) return std::unexpected(AtmError::InternalError);

    const auto plan = _vault->calculate_dispense(amount_cents);
    if (!plan) return std::unexpected(to_atm_error(plan.error()));

    CashVault next_vault{*_vault};
    if (auto dispensed = next_vault.execute_dispense(*plan); !dispensed)
    {
        return std::unexpected(to_atm_error(dispensed.error()));
    }

    auto accounts = load_customer_accounts(*_db, _active_session->record.number);
    if (!accounts) return std::unexpected(accounts.error());

    if (auto replayed = consume_session_nonce(); !replayed) return replayed;

    const std::uint64_t nonce = ++_tx_nonce;
    const std::size_t audit_mark = _audit_log->size();

    if (auto posted = _ledger->withdraw_with_overdraft(
            *accounts->checking, *accounts->savings, amount_cents, nonce);
        !posted)
    {
        return std::unexpected(to_atm_error(posted.error()));
    }

    return commit_cash_operation(std::move(next_vault),
                                 {accounts->checking.get(), accounts->savings.get()},
                                 nonce, audit_mark);
}

std::expected<void, AtmError> AtmController::process_deposit(
    const std::vector<Denomination>& bills)
{
    std::unique_lock lock(_mtx);

    if (auto session = require_role(UserRole::Customer); !session) return session;
    if (!_ledger || !_audit_log) return std::unexpected(AtmError::InternalError);

    CashVault next_vault{*_vault};
    if (auto accepted = next_vault.deposit_bills(bills); !accepted)
    {
        return std::unexpected(to_atm_error(accepted.error()));
    }

    auto accounts = load_customer_accounts(*_db, _active_session->record.number);
    if (!accounts) return std::unexpected(accounts.error());

    if (auto replayed = consume_session_nonce(); !replayed) return replayed;

    const std::uint64_t nonce = ++_tx_nonce;
    const std::size_t audit_mark = _audit_log->size();

    if (auto posted = _ledger->deposit_with_overflow(
            *accounts->checking, *accounts->savings, sum_of(bills), nonce);
        !posted)
    {
        return std::unexpected(to_atm_error(posted.error()));
    }

    return commit_cash_operation(std::move(next_vault),
                                 {accounts->checking.get(), accounts->savings.get()},
                                 nonce, audit_mark);
}

std::expected<void, AtmError> AtmController::process_transfer(
    std::string_view from_account, std::string_view to_account,
    std::int64_t amount_cents, std::string_view idempotency_key)
{
    std::unique_lock lock(_mtx);

    if (auto session = require_role(UserRole::Customer); !session) return session;
    if (amount_cents <= 0) return std::unexpected(AtmError::InvalidAmount);
    if (!_ledger || !_audit_log) return std::unexpected(AtmError::InternalError);

    auto accounts = load_customer_accounts(*_db, _active_session->record.number);
    if (!accounts) return std::unexpected(accounts.error());

    Account* from = resolve_own_account(from_account, *accounts);
    if (!from) return std::unexpected(AtmError::Unauthorized);

    std::unique_ptr<Account> external_to;
    Account* to = resolve_own_account(to_account, *accounts);
    if (!to)
    {
        if (auto loaded = _db->load_account(to_account); loaded)
        {
            external_to = std::move(*loaded);
            to = external_to.get();
        }
        else
        {
            auto card = _db->load_card(to_account, false);
            if (!card) return std::unexpected(to_atm_error(card.error()));
            if (!card->account_id) return std::unexpected(AtmError::CardNotFound);

            auto card_account = _db->load_account(*card->account_id);
            if (!card_account) return std::unexpected(to_atm_error(card_account.error()));

            external_to = std::move(*card_account);
            to = external_to.get();
        }
    }

    if (from->get_account_id() == to->get_account_id())
    {
        return std::unexpected(AtmError::InvalidAmount);
    }

    if (const auto* chk_from = dynamic_cast<const CheckingAccount*>(from))
    {
        if (chk_from->needs_overdraft_coverage(amount_cents))
        {
            return std::unexpected(AtmError::InsufficientFunds);
        }
    }

    if (const auto* chk_to = dynamic_cast<const CheckingAccount*>(to))
    {
        if (amount_cents > chk_to->get_overflow_max_cents() - chk_to->get_balance())
        {
            return std::unexpected(AtmError::InvalidAmount);
        }
    }

    if (auto replayed = consume_session_nonce(); !replayed) return replayed;

    const std::uint64_t nonce = ++_tx_nonce;
    const std::size_t audit_mark = _audit_log->size();

    if (auto posted = _ledger->transfer(*from, *to, amount_cents, "TRANSFER", nonce,
                                        idempotency_key);
        !posted)
    {
        return std::unexpected(to_atm_error(posted.error()));
    }

    const auto saved = _db->execute_transaction(
        [&](sqlite3* raw)
        {
            return storage::save_account(raw, *from).has_value() &&
                   storage::save_account(raw, *to).has_value();
        });

    if (!saved)
    {
        if (!reverse_entries(*_audit_log, nonce, audit_mark))
            return std::unexpected(AtmError::InternalError);
        return std::unexpected(AtmError::DatabaseError);
    }

    return {};
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

    return commit_cash_operation(std::move(next_vault), {}, nonce, audit_mark);
}

std::expected<CustomerAccountSnapshot, AtmError> AtmController::get_customer_accounts()
{
    std::unique_lock lock(_mtx);

    if (auto session = require_role(UserRole::Customer); !session)
        return std::unexpected(session.error());

    auto accounts = load_customer_accounts(*_db, _active_session->record.number);
    if (!accounts) return std::unexpected(accounts.error());

    return CustomerAccountSnapshot{accounts->checking->get_account_id(),
                                   accounts->checking->get_balance(),
                                   accounts->checking->get_client_min_cents(),
                                   accounts->checking->get_overflow_max_cents(),
                                   accounts->savings->get_account_id(),
                                   accounts->savings->get_balance()};
}

std::expected<std::vector<LedgerEntry>, AtmError> AtmController::get_audit_entries()
{
    std::unique_lock lock(_mtx);

    if (auto session = require_role(UserRole::Engineer); !session)
        return std::unexpected(session.error());
    if (!_audit_log) return std::unexpected(AtmError::InternalError);

    return _audit_log->entries();
}

std::expected<ChainVerification, AtmError> AtmController::verify_audit_integrity()
{
    std::unique_lock lock(_mtx);

    if (auto session = require_role(UserRole::Engineer); !session)
        return std::unexpected(session.error());
    if (!_audit_log) return std::unexpected(AtmError::InternalError);

    auto result = _audit_log->verify_integrity_chain();
    if (!result) return std::unexpected(AtmError::CryptoVerificationFailed);
    return *result;
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
    return crypto::constant_time_compare(engineer_key, kDefaultEngineerKey);
}

std::expected<void, AtmError> AtmController::require_role(UserRole role)
{
    if (auto session = validate_session(); !session) return session;
    if (_active_session->record.role != role) return std::unexpected(AtmError::Unauthorized);
    return {};
}

std::expected<void, AtmError> AtmController::consume_session_nonce()
{
    if (!_active_session.has_value()) return std::unexpected(AtmError::NoActiveSession);
    const std::uint64_t next = _active_session->next_nonce();
    if (_anti_replay &&
        !_anti_replay->validate_and_consume(_active_session->session_id, next))
    {
        return std::unexpected(AtmError::ReplayAttackDetected);
    }
    return {};
}

std::expected<void, AtmError> AtmController::commit_cash_operation(
    CashVault next_vault, const std::vector<const Account*>& accounts,
    std::uint64_t nonce, std::size_t audit_mark)
{
    const auto inventory = inventory_of(next_vault);

    const auto saved = _db->execute_transaction(
        [&](sqlite3* raw)
        {
            for (auto it = accounts.cbegin(); it != accounts.cend(); ++it)
            {
                if (!storage::save_account(raw, **it)) return false;
            }
            return storage::save_vault_inventory(raw, inventory).has_value();
        });

    if (!saved)
    {
        if (!reverse_entries(*_audit_log, nonce, audit_mark))
            return std::unexpected(AtmError::InternalError);
        return std::unexpected(AtmError::DatabaseError);
    }

    *_vault = std::move(next_vault);
    return {};
}
