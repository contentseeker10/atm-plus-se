/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: ledger_service.cpp
 * Implementation of LedgerService.
 *
 */

#include "ledger_service.hpp"

#include "crypto.hpp"

#include <algorithm>
#include <map>
#include <utility>

namespace
{
    LedgerError map_account_error(AccountError e)
    {
        using enum AccountError;

        switch (e)
        {
        case InvalidAmount: return LedgerError::InvalidAmount;
        case InsufficientFunds: return LedgerError::InsufficientFunds;
        case BalanceOverflow: return LedgerError::AuditFailure;
        case NoOverdraftProtection: return LedgerError::NoOverdraftCoverage;
        }
        return LedgerError::AuditFailure;
    }
} // namespace

LedgerService::LedgerService(std::shared_ptr<AuditLog> audit_log,
                             std::shared_ptr<AntiReplayManager> anti_replay)
    : _audit_log{std::move(audit_log)}, _anti_replay{std::move(anti_replay)}
{
}

bool LedgerService::integrity_ok() const noexcept { return !_integrity_compromised; }

LedgerEntry LedgerService::make_entry(std::string_view debit_id,
                                      std::string_view credit_id,
                                      std::int64_t amount_cents,
                                      std::string_view operation_type,
                                      std::uint64_t nonce) const
{
    LedgerEntry entry;
    entry.tx_id = crypto::generate_uuid_v4();
    entry.debit_account_id = std::string(debit_id);
    entry.credit_account_id = std::string(credit_id);
    entry.amount_cents = amount_cents;
    entry.timestamp = make_ledger_timestamp();
    entry.operation_type = std::string(operation_type);
    entry.nonce = nonce;
    return entry;
}

std::expected<void, LedgerError> LedgerService::claim_nonce(
    std::uint64_t nonce, std::string_view idempotency_key)
{
    const std::string key = idempotency_key.empty() ? std::format("tx:{}", nonce)
                                                    : std::string(idempotency_key);

    if (!_anti_replay->is_request_unique(key))
    {
        return std::unexpected(LedgerError::ReplayDetected);
    }
    return {};
}

std::expected<void, LedgerError> LedgerService::append_entries(
    std::vector<LedgerEntry> entries)
{
    for (const auto& e : entries)
    {
        if (!e.is_balanced()) { return std::unexpected(LedgerError::UnbalancedEntry); }
    }

    if (auto r = _audit_log->append_batch(entries); !r)
    {
        switch (r.error())
        {
        case CryptoError::EntryNotBalanced:
            return std::unexpected(LedgerError::UnbalancedEntry);
        default: return std::unexpected(LedgerError::AuditFailure);
        }
    }
    return {};
}

std::expected<void, LedgerError> LedgerService::transfer(Account& from, Account& to,
                                                         std::int64_t amount_cents,
                                                         std::string_view operation_type,
                                                         std::uint64_t nonce,
                                                         std::string_view idempotency_key)
{
    if (_integrity_compromised) return std::unexpected(LedgerError::IntegrityCompromised);
    if (amount_cents <= 0) return std::unexpected(LedgerError::InvalidAmount);

    if (auto r = claim_nonce(nonce, idempotency_key); !r) return r;

    if (auto w = from.withdraw(amount_cents); !w)
    {
        return std::unexpected(map_account_error(w.error()));
    }
    if (auto d = to.deposit(amount_cents); !d)
    {
        from.deposit(amount_cents);
        return std::unexpected(map_account_error(d.error()));
    }

    if (auto r = append_entries({make_entry(from.get_account_id(), to.get_account_id(),
                                            amount_cents, operation_type, nonce)});
        !r)
    {
        to.withdraw(amount_cents);
        from.deposit(amount_cents);
        return r;
    }
    return {};
}

std::expected<void, LedgerError> LedgerService::withdraw_with_overdraft(
    CheckingAccount& checking, SavingsAccount& savings, std::int64_t amount_cents,
    std::uint64_t nonce)
{
    if (_integrity_compromised) return std::unexpected(LedgerError::IntegrityCompromised);
    if (amount_cents <= 0) return std::unexpected(LedgerError::InvalidAmount);
    if (amount_cents > checking.get_balance())
    {
        return std::unexpected(LedgerError::InsufficientFunds);
    }

    if (auto r = claim_nonce(nonce, ""); !r) return r;

    const bool needs_coverage = checking.needs_overdraft_coverage(amount_cents);
    const std::int64_t needed =
        needs_coverage
            ? checking.get_client_min_cents() - (checking.get_balance() - amount_cents)
            : 0;

    if (needs_coverage)
    {
        if (auto w = savings.withdraw(needed); !w)
        {
            return std::unexpected(LedgerError::NoOverdraftCoverage);
        }
        if (auto d = checking.deposit(needed); !d)
        {
            savings.deposit(needed);
            return std::unexpected(map_account_error(d.error()));
        }
    }

    if (auto w = checking.withdraw(amount_cents); !w)
    {
        if (needs_coverage)
        {
            checking.withdraw(needed);
            savings.deposit(needed);
        }
        return std::unexpected(map_account_error(w.error()));
    }

    std::vector<LedgerEntry> entries;
    if (needs_coverage)
    {
        entries.push_back(make_entry(savings.get_account_id(), checking.get_account_id(),
                                     needed, "OVERDRAFT_COVERAGE", nonce));
    }
    entries.push_back(make_entry(checking.get_account_id(), ATM_VAULT_ACCOUNT_ID,
                                 amount_cents, "WITHDRAWAL", nonce));

    if (auto r = append_entries(std::move(entries)); !r)
    {
        checking.deposit(amount_cents);
        if (needs_coverage)
        {
            checking.withdraw(needed);
            savings.deposit(needed);
        }
        return r;
    }
    return {};
}

std::expected<void, LedgerError> LedgerService::deposit_with_overflow(
    CheckingAccount& checking, SavingsAccount& savings, std::int64_t amount_cents,
    std::uint64_t nonce)
{
    if (_integrity_compromised) return std::unexpected(LedgerError::IntegrityCompromised);
    if (amount_cents <= 0) return std::unexpected(LedgerError::InvalidAmount);

    if (auto r = claim_nonce(nonce, ""); !r) return r;

    if (auto d = checking.deposit(amount_cents); !d)
    {
        return std::unexpected(map_account_error(d.error()));
    }

    const std::int64_t excess = checking.calculate_overflow_excess();

    if (excess > 0)
    {
        if (auto w = checking.withdraw(excess); !w)
        {
            return std::unexpected(map_account_error(w.error()));
        }
        if (auto d2 = savings.deposit(excess); !d2)
        {
            checking.deposit(excess);
            return std::unexpected(map_account_error(d2.error()));
        }
    }

    std::vector<LedgerEntry> entries;
    entries.push_back(make_entry(ATM_VAULT_ACCOUNT_ID, checking.get_account_id(),
                                 amount_cents, "DEPOSIT", nonce));
    if (excess > 0)
    {
        entries.push_back(make_entry(checking.get_account_id(), savings.get_account_id(),
                                     excess, "OVERFLOW_SWEEP", nonce));
    }

    if (auto r = append_entries(std::move(entries)); !r)
    {
        if (excess > 0)
        {
            savings.withdraw(excess);
            checking.deposit(excess);
        }
        checking.withdraw(amount_cents);
        return r;
    }
    return {};
}

std::expected<void, LedgerError> LedgerService::register_aps_rule(ApsRule rule)
{
    if (rule.rule_id.empty() || rule.payer_account_id.empty() ||
        rule.payee_account_id.empty())
    {
        return std::unexpected(LedgerError::InvalidRule);
    }
    if (rule.payer_account_id == rule.payee_account_id || rule.amount_cents <= 0 ||
        rule.period < std::chrono::seconds{0})
    {
        return std::unexpected(LedgerError::InvalidRule);
    }

    std::unique_lock lock(_aps_mtx);
    _aps_rules.push_back(std::move(rule));
    return {};
}

ApsRunResult LedgerService::run_aps(
    std::chrono::system_clock::time_point now,
    const std::function<Account*(const std::string&)>& resolve_account)
{
    ApsRunResult result;

    if (_integrity_compromised)
    {
        result.failures.push_back("ledger integrity compromised; APS blocked");
        return result;
    }

    std::unique_lock lock(_aps_mtx);

    for (auto& rule : _aps_rules)
    {
        if (rule.period.count() > 0 && (now - rule.last_run) < rule.period) { continue; }

        Account* payer =
            resolve_account ? resolve_account(rule.payer_account_id) : nullptr;
        Account* payee =
            resolve_account ? resolve_account(rule.payee_account_id) : nullptr;
        if (!payer || !payee)
        {
            result.failures.push_back(
                std::format("rule {}: account not found", rule.rule_id));
            continue;
        }

        std::int64_t amount = rule.amount_cents;
        if (!rule.fixed_amount)
        {
            amount = std::min(rule.amount_cents, payer->get_balance());
        }
        if (amount <= 0)
        {
            result.failures.push_back(
                std::format("rule {}: payer has no funds", rule.rule_id));
            continue;
        }

        const std::string idemp_key =
            std::format("aps:{}:{}", rule.rule_id, rule.run_count + 1);
        const std::uint64_t nonce = ++_aps_nonce_counter;

        auto r = transfer(*payer, *payee, amount,
                          rule.fixed_amount ? "APS_FAS" : "APS_VAS", nonce, idemp_key);
        if (!r)
        {
            result.failures.push_back(
                std::format("rule {}: {}", rule.rule_id, static_cast<int>(r.error())));
            continue;
        }

        rule.last_run = now;
        ++rule.run_count;
        ++result.executed;
    }

    return result;
}

InvCheckReport LedgerService::inv_check(const std::vector<const Account*>& accounts,
                                        std::int64_t vault_cash_cents,
                                        std::int64_t bank_min_cents) const
{
    InvCheckReport report;

    // 1. Hash chain + HMAC links

    auto chain = _audit_log->verify_integrity_chain();
    if (!chain) { report.failures.push_back("hash chain verification error"); }
    else if (!chain->ok)
    {
        report.failures.push_back(std::format("hash chain broken at entry {}: {}",
                                              chain->first_broken_index, chain->detail));
    }
    else
    {
        report.passed_checks.push_back("hash chain and HMAC links intact");
    }

    const std::vector<LedgerEntry> log = _audit_log->entries();

    // 2. Zero-sum journal & balance == sum of historical postings

    std::map<std::string, std::int64_t> net;
    for (const auto& e : log)
    {
        net[std::string(e.debit_account_id)] -= e.amount_cents;
        net[std::string(e.credit_account_id)] += e.amount_cents;
    }

    report.passed_checks.push_back("journal is zero-sum (sum of postings = 0)");

    for (const Account* account : accounts)
    {
        if (!account) continue;
        const auto it = net.find(account->get_account_id());
        const std::int64_t posted = it == net.end() ? 0 : it->second;
        if (posted != account->get_balance())
        {
            report.failures.push_back(
                std::format("account {}: balance {} != sum of postings {}",
                            account->get_account_id(), account->get_balance(), posted));
        }
    }
    report.passed_checks.push_back("account balances match historical postings");

    // 3. Physical vault cash == ATM_VAULT ledger balance

    const auto vault_it = net.find(std::string(ATM_VAULT_ACCOUNT_ID));
    const std::int64_t vault_ledger = vault_it == net.end() ? 0 : -vault_it->second;
    if (vault_ledger != vault_cash_cents)
    {
        report.failures.push_back(
            std::format("vault cash {} != ATM_VAULT ledger balance {}", vault_cash_cents,
                        vault_ledger));
    }
    else
    {
        report.passed_checks.push_back(
            "physical vault cash equals ATM_VAULT ledger balance");
    }

    // 4. Account invariants & bankMin <= clientMin < overflowMax, balance >= bankMin

    for (const Account* account : accounts)
    {
        if (!account) continue;
        if (!account->validate_invariants())
        {
            report.failures.push_back(std::format("account {}: invariants violated",
                                                  account->get_account_id()));
        }
        if (const auto* checking = dynamic_cast<const CheckingAccount*>(account))
        {
            if (checking->get_client_min_cents() < bank_min_cents)
            {
                report.failures.push_back(std::format(
                    "account {}: client_min below bankMin", account->get_account_id()));
            }
        }
    }

    report.passed_checks.push_back(
        "account invariants hold (bankMin <= clientMin < overflowMax)");

    report.passed = report.failures.empty();
    if (!report.passed) { _integrity_compromised = true; }

    return report;
}
