/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: http_server.cpp
 * Implementation of HTTP server adapter.
 *
 */

#include "http_server.hpp"

#include "crypto.hpp"
#include "enums.hpp"

#include "access_control/atm_controller.hpp"
#include "accounting/ledger.hpp"
#include "cash/cash_vault.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace
{
    using json = nlohmann::json;

    void send_json(httplib::Response& res, int status, const json& payload)
    {
        res.status = status;
        res.set_content(payload.dump(), "application/json");
    }

    constexpr std::string_view error_code_string(AtmError err) noexcept
    {
        switch (err)
        {
        case AtmError::InvalidStateTransition: return "InvalidStateTransition";
        case AtmError::NoActiveSession: return "NoActiveSession";
        case AtmError::SessionExpired: return "SessionExpired";
        case AtmError::InvalidSessionToken: return "InvalidSessionToken";
        case AtmError::ReplayAttackDetected: return "ReplayAttackDetected";
        case AtmError::Unauthorized: return "Unauthorized";
        case AtmError::VaultAccessDenied: return "VaultAccessDenied";
        case AtmError::HardwareFailure: return "HardwareFailure";
        case AtmError::InternalError: return "InternalError";
        case AtmError::InsufficientCashInAtm: return "InsufficientCashInAtm";
        case AtmError::InvalidAmount: return "InvalidAmount";
        case AtmError::CassetteFull: return "CassetteFull";
        case AtmError::VaultDoorOpen: return "VaultDoorOpen";
        case AtmError::CardNotFound: return "CardNotFound";
        case AtmError::CardBlocked: return "CardBlocked";
        case AtmError::InvalidPin: return "InvalidPin";
        case AtmError::MaxAttemptsExceeded: return "MaxAttemptsExceeded";
        case AtmError::CardExpired: return "CardExpired";
        case AtmError::CryptoVerificationFailed: return "CryptoVerificationFailed";
        case AtmError::DatabaseError: return "DatabaseError";
        case AtmError::InsufficientFunds: return "InsufficientFunds";
        }
        return "InternalError";
    }

    constexpr int error_http_status(AtmError err) noexcept
    {
        switch (err)
        {
        case AtmError::CardNotFound: return 404;
        case AtmError::InvalidPin:
        case AtmError::NoActiveSession:
        case AtmError::SessionExpired:
        case AtmError::InvalidSessionToken: return 401;
        case AtmError::CardBlocked:
        case AtmError::MaxAttemptsExceeded:
        case AtmError::CardExpired:
        case AtmError::Unauthorized:
        case AtmError::VaultAccessDenied: return 403;
        case AtmError::InvalidAmount:
        case AtmError::InsufficientFunds: return 400;
        case AtmError::InsufficientCashInAtm:
        case AtmError::CassetteFull:
        case AtmError::ReplayAttackDetected:
        case AtmError::CryptoVerificationFailed:
        case AtmError::InvalidStateTransition: return 409;
        case AtmError::VaultDoorOpen: return 503;
        case AtmError::DatabaseError:
        case AtmError::HardwareFailure:
        case AtmError::InternalError: return 500;
        }
        return 500;
    }

    constexpr std::string_view error_message(AtmError err) noexcept
    {
        switch (err)
        {
        case AtmError::InvalidStateTransition: return "Operation not allowed in current ATM state.";
        case AtmError::NoActiveSession: return "No active ATM session.";
        case AtmError::SessionExpired: return "Session expired due to inactivity.";
        case AtmError::InvalidSessionToken: return "Session expired or invalid token.";
        case AtmError::ReplayAttackDetected: return "Replay attack or duplicate nonce detected.";
        case AtmError::Unauthorized: return "Insufficient privileges for this operation.";
        case AtmError::VaultAccessDenied: return "Vault access denied.";
        case AtmError::HardwareFailure: return "ATM hardware subsystem failure.";
        case AtmError::InternalError: return "Internal ATM controller error.";
        case AtmError::InsufficientCashInAtm: return "ATM cannot dispense the exact requested amount.";
        case AtmError::InvalidAmount: return "Invalid amount or unsupported denomination.";
        case AtmError::CassetteFull: return "Cassette capacity exceeded.";
        case AtmError::VaultDoorOpen: return "Vault door is open. Customer operations are locked.";
        case AtmError::CardNotFound: return "Unrecognized card number.";
        case AtmError::CardBlocked: return "Card is blocked due to security policy.";
        case AtmError::InvalidPin: return "Incorrect PIN code.";
        case AtmError::MaxAttemptsExceeded: return "Maximum PIN attempts exceeded. Card has been blocked.";
        case AtmError::CardExpired: return "This card has expired and cannot be used.";
        case AtmError::CryptoVerificationFailed: return "Cryptographic integrity check failed.";
        case AtmError::DatabaseError: return "Database operation failed.";
        case AtmError::InsufficientFunds: return "Insufficient funds in account.";
        }
        return "Internal ATM controller error.";
    }

    void send_atm_error(httplib::Response& res, AtmError err,
                        const json& extra = json::object())
    {
        json payload{
            {"errorCode", error_code_string(err)},
            {"message", error_message(err)}
        };
        if (extra.is_object())
        {
            for (const auto& [k, v] : extra.items())
            {
                payload[k] = v;
            }
        }
        send_json(res, error_http_status(err), payload);
    }

    std::optional<json> parse_json_body(const httplib::Request& req, httplib::Response& res)
    {
        if (req.body.empty()) return json::object();
        auto parsed = json::parse(req.body, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object())
        {
            send_json(res, 400, {
                {"errorCode", "InvalidAmount"},
                {"message", "Invalid JSON request payload."}
            });
            return std::nullopt;
        }
        return parsed;
    }

    std::optional<std::string> read_string(const json& obj, std::string_view key,
                                           std::string_view fallback_key = "")
    {
        if (const auto it = obj.find(std::string{key});
            it != obj.end() && it->is_string())
        {
            return it->get<std::string>();
        }
        if (!fallback_key.empty())
        {
            if (const auto it = obj.find(std::string{fallback_key});
                it != obj.end() && it->is_string())
            {
                return it->get<std::string>();
            }
        }
        return std::nullopt;
    }

    std::optional<std::int64_t> read_int64(const json& obj, std::string_view key,
                                           std::string_view fallback_key = "")
    {
        if (const auto it = obj.find(std::string{key});
            it != obj.end() && it->is_number_integer())
        {
            return it->get<std::int64_t>();
        }
        if (!fallback_key.empty())
        {
            if (const auto it = obj.find(std::string{fallback_key});
                it != obj.end() && it->is_number_integer())
            {
                return it->get<std::int64_t>();
            }
        }
        return std::nullopt;
    }

    std::optional<std::int64_t> parse_int64_text(std::string_view text) noexcept
    {
        std::int64_t val = 0;
        const auto* begin = text.data();
        const auto* end = text.data() + text.size();
        const auto [ptr, ec] = std::from_chars(begin, end, val);
        if (ec != std::errc{} || ptr != end) return std::nullopt;
        return val;
    }

    std::optional<Denomination> to_denomination(std::int64_t cents) noexcept
    {
        switch (cents)
        {
        case 1000: return Denomination::D_10;
        case 2000: return Denomination::D_20;
        case 5000: return Denomination::D_50;
        case 10000: return Denomination::D_100;
        case 20000: return Denomination::D_200;
        case 50000: return Denomination::D_500;
        default: return std::nullopt;
        }
    }

    std::optional<Denomination> to_denomination_by_index(std::int64_t index) noexcept
    {
        constexpr std::array kOrder{
            Denomination::D_10, Denomination::D_20,  Denomination::D_50,
            Denomination::D_100, Denomination::D_200, Denomination::D_500,
        };
        if (index < 0 || static_cast<std::size_t>(index) >= kOrder.size())
            return std::nullopt;
        return kOrder[static_cast<std::size_t>(index)];
    }

    std::string mask_pan(std::string_view pan)
    {
        if (pan.size() <= 4) return std::string{pan};
        return "****-****-****-" + std::string{pan.substr(pan.size() - 4)};
    }

    std::string upper_prefix(std::string value, std::size_t count)
    {
        if (value.size() > count) value.resize(count);
        for (char& ch : value)
        {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
        return value;
    }

    std::optional<std::string> extract_bearer_token(const httplib::Request& req)
    {
        const std::string header = req.get_header_value("Authorization");
        constexpr std::string_view kPrefix = "Bearer ";
        if (header.size() <= kPrefix.size() ||
            header.compare(0, kPrefix.size(), kPrefix) != 0)
        {
            return std::nullopt;
        }
        return header.substr(kPrefix.size());
    }

    bool verify_bearer_token(AtmController& controller, const httplib::Request& req,
                             httplib::Response& res)
    {
        if (const auto valid = controller.validate_session(); !valid)
        {
            send_atm_error(res, valid.error());
            return false;
        }
        const auto active_token = controller.get_active_session_id();
        const auto provided = extract_bearer_token(req);
        if (!active_token || !provided || *provided != *active_token)
        {
            send_atm_error(res, AtmError::InvalidSessionToken);
            return false;
        }
        return true;
    }

    bool verify_engineer_headers(AtmController& controller, const httplib::Request& req,
                                 httplib::Response& res)
    {
        if (!verify_bearer_token(controller, req, res)) return false;
        if (controller.get_active_role() != UserRole::Engineer)
        {
            send_atm_error(res, AtmError::Unauthorized);
            return false;
        }
        const std::string master_key = req.get_header_value("X-Master-Key");
        if (!controller.verify_engineer_key(master_key))
        {
            send_atm_error(res, AtmError::VaultAccessDenied);
            return false;
        }
        return true;
    }

    json build_accounts_json(const CustomerAccountSnapshot& snap)
    {
        return json::array({
            {
                {"id", snap.checking_id},
                {"type", "Checking"},
                {"balance", snap.checking_balance_cents},
                {"clientMin", snap.client_min_cents},
                {"overflowMax", snap.overflow_max_cents}
            },
            {
                {"id", snap.savings_id},
                {"type", "Savings"},
                {"balance", snap.savings_balance_cents}
            }
        });
    }

    json build_vault_json(const CashVault& vault, int dispense_cycles, int deposit_cycles)
    {
        json cassettes_arr = json::array();
        for (const auto& [denom, cassette] : vault.get_cassettes())
        {
            cassettes_arr.push_back({
                {"denom", static_cast<std::int64_t>(denom)},
                {"count", cassette.get_count()},
                {"capacity", cassette.get_max_capacity()}
            });
        }

        return {
            {"totalCash", vault.get_total_cash_cents()},
            {"doorOpen", !vault.is_door_closed()},
            {"hardwareCounters", {
                {"dispenseCycles", dispense_cycles},
                {"depositCycles", deposit_cycles},
                {"jamEvents", 0}
            }},
            {"cassettes", cassettes_arr}
        };
    }

    json build_receipt(std::string_view card_number, std::string_view type,
                       std::string_view account, std::int64_t amount_cents,
                       std::int64_t new_balance_cents,
                       std::int64_t overdraft_covered = 0,
                       std::int64_t overflow_swept = 0)
    {
        const std::string tx_uuid = crypto::generate_uuid_v4();
        const std::string timestamp = make_ledger_timestamp();
        const std::string payload = std::string{tx_uuid} + "|" + std::string{type} + "|" +
                                    std::string{account} + "|" + std::to_string(amount_cents);

        json receipt{
            {"txId", "TX-" + upper_prefix(tx_uuid, 8)},
            {"timestamp", timestamp},
            {"card", mask_pan(card_number)},
            {"type", type},
            {"account", account},
            {"amountCents", amount_cents},
            {"newBalanceCents", new_balance_cents},
            {"authHash", upper_prefix(crypto::sha256_hex(payload), 16)}
        };
        if (overdraft_covered > 0) receipt["overdraftCoveredCents"] = overdraft_covered;
        if (overflow_swept > 0) receipt["overflowSweptCents"] = overflow_swept;
        return receipt;
    }

    std::optional<std::vector<Denomination>> parse_bills_payload(const json& body)
    {
        const auto it = body.find("bills");
        if (it == body.end()) return std::nullopt;

        std::vector<Denomination> bills;
        if (it->is_array())
        {
            bills.reserve(it->size());
            for (const auto& item : *it)
            {
                if (!item.is_number_integer()) return std::nullopt;
                const auto denom = to_denomination(item.get<std::int64_t>());
                if (!denom) return std::nullopt;
                bills.push_back(*denom);
            }
            return bills.empty() ? std::nullopt : std::optional{std::move(bills)};
        }

        if (it->is_object())
        {
            for (const auto& [key, val] : it->items())
            {
                if (!val.is_number_integer()) return std::nullopt;
                const std::int64_t count = val.get<std::int64_t>();
                if (count <= 0 || count > 1000) return std::nullopt;

                const auto raw_key = parse_int64_text(key);
                if (!raw_key) return std::nullopt;

                auto denom = to_denomination(*raw_key);
                if (!denom) denom = to_denomination(*raw_key * 100);
                if (!denom) return std::nullopt;

                for (std::int64_t i = 0; i < count; ++i)
                {
                    bills.push_back(*denom);
                }
            }
            return bills.empty() ? std::nullopt : std::optional{std::move(bills)};
        }

        return std::nullopt;
    }
}

void HttpServerAdapter::_mount_frontend()
{
    std::filesystem::path chosen_root;
    for (const char* candidate : {VIEW_PATH, "../../view", "../view", "./view"})
    {
        if (std::filesystem::exists(candidate) && std::filesystem::is_directory(candidate))
        {
            chosen_root = candidate;
            break;
        }
    }

    if (chosen_root.empty())
    {
        std::println("frontend directory {} not found", VIEW_PATH);
        return;
    }
    if (!_server.set_mount_point("/", chosen_root.string()))
    {
        std::println("could not mount frontend");
    }
}

void HttpServerAdapter::_register_routes()
{
    _server.set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"},
        {"Access-Control-Allow-Headers", "Content-Type, Authorization, X-Master-Key"}
    });

    _server.Options(R"(/api/.*)", [](const httplib::Request&, httplib::Response& res)
    {
        res.status = 204;
    });

    _server.Post("/api/card/insert", [this](const httplib::Request& req, httplib::Response& res)
    {
        const auto body = parse_json_body(req, res);
        if (!body) return;

        const auto card_number = read_string(*body, "cardNumber", "pan");
        if (!card_number || card_number->empty())
        {
            return send_atm_error(res, AtmError::CardNotFound);
        }

        if (_atm_controller->get_current_state_name() != "Idle")
        {
            std::ignore = _atm_controller->process_eject_card();
        }

        if (const auto r = _atm_controller->process_insert_card(*card_number); !r)
        {
            return send_atm_error(res, r.error());
        }

        const auto card = _atm_controller->get_active_card();
        const std::string holder =
            (card && !card->holder.empty()) ? card->holder : "CARDHOLDER";
        const int remaining = _atm_controller->get_remaining_pin_attempts(*card_number);

        send_json(res, 200, {
            {"status", "ok"},
            {"requiresPin", true},
            {"cardHolder", holder},
            {"maskedPan", mask_pan(*card_number)},
            {"remainingAttempts", remaining}
        });
    });

    _server.Post("/api/auth/pin", [this](const httplib::Request& req, httplib::Response& res)
    {
        const auto body = parse_json_body(req, res);
        if (!body) return;

        const auto active_before = _atm_controller->get_active_card();
        const auto expected_pan = read_string(*body, "cardNumber", "pan");
        if (expected_pan && active_before && active_before->number != *expected_pan)
        {
            return send_atm_error(res, AtmError::InvalidStateTransition);
        }

        const std::string target_pan =
            active_before ? active_before->number : expected_pan.value_or("");

        const auto pin = read_string(*body, "pin");
        if (!pin || pin->empty())
        {
            return send_atm_error(
                res, AtmError::InvalidPin,
                {{"remainingAttempts", _atm_controller->get_remaining_pin_attempts(target_pan)}});
        }

        const auto r = _atm_controller->process_enter_pin(*pin);
        if (!r)
        {
            if (r.error() == AtmError::MaxAttemptsExceeded ||
                r.error() == AtmError::CardBlocked)
            {
                return send_atm_error(res, r.error(),
                                      {{"remainingAttempts", 0}, {"cardBlocked", true}});
            }
            if (r.error() == AtmError::InvalidPin)
            {
                return send_atm_error(
                    res, r.error(),
                    {{"remainingAttempts", _atm_controller->get_remaining_pin_attempts(target_pan)}});
            }
            return send_atm_error(res, r.error());
        }

        const std::string token = _atm_controller->get_active_session_id().value_or("");
        const auto card = _atm_controller->get_active_card();
        const std::string role =
            (card && card->role == UserRole::Engineer) ? "Engineer" : "Customer";
        const std::string holder =
            (card && !card->holder.empty()) ? card->holder : "CARDHOLDER";
        const int remaining =
            _atm_controller->get_remaining_pin_attempts(card ? card->number : target_pan);

        send_json(res, 200, {
            {"status", "ok"},
            {"token", token},
            {"role", role},
            {"cardHolder", holder},
            {"remainingAttempts", remaining}
        });
    });

    _server.Post("/api/card/eject", [this](const httplib::Request&, httplib::Response& res)
    {
        std::ignore = _atm_controller->process_eject_card();
        send_json(res, 200, {{"status", "ok"}});
    });

    _server.Get("/api/customer/accounts", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!verify_bearer_token(*_atm_controller, req, res)) return;

        const auto snap = _atm_controller->get_customer_accounts();
        if (!snap) return send_atm_error(res, snap.error());

        send_json(res, 200, {{"accounts", build_accounts_json(*snap)}});
    });

    _server.Post("/api/customer/withdraw", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!verify_bearer_token(*_atm_controller, req, res)) return;

        const auto body = parse_json_body(req, res);
        if (!body) return;

        const auto amount = read_int64(*body, "amount");
        if (!amount) return send_atm_error(res, AtmError::InvalidAmount);

        const std::string account_type =
            read_string(*body, "accountType", "account").value_or("Checking");
        const auto card = _atm_controller->get_active_card();

        const auto wd = _atm_controller->process_withdraw(*amount, account_type);
        if (!wd)
        {
            return send_atm_error(res, wd.error());
        }
        ++_dispense_cycles;

        json dispensed_bills = json::array();
        for (const auto& [denom, count] : wd->dispensed_bills)
        {
            if (count > 0)
            {
                dispensed_bills.push_back({
                    {"denomination", static_cast<std::int64_t>(denom)},
                    {"count", count}
                });
            }
        }

        const bool is_savings = (account_type == "Savings" || account_type == "savings" ||
                                 account_type == wd->accounts.savings_id);
        const std::string& receipt_account =
            is_savings ? wd->accounts.savings_id : wd->accounts.checking_id;

        const std::string pan = card ? card->number : "";
        const json receipt = build_receipt(pan, "WITHDRAWAL", receipt_account, *amount,
                                           wd->target_new_balance_cents,
                                           wd->overdraft_covered_cents, 0);

        send_json(res, 200, {
            {"status", "ok"},
            {"dispensedBills", dispensed_bills},
            {"newBalance", wd->target_new_balance_cents},
            {"overdraftCoveredCents", wd->overdraft_covered_cents},
            {"accounts", build_accounts_json(wd->accounts)},
            {"receipt", receipt}
        });
    });

    _server.Post("/api/customer/deposit", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!verify_bearer_token(*_atm_controller, req, res)) return;

        const auto body = parse_json_body(req, res);
        if (!body) return;

        const auto bills = parse_bills_payload(*body);
        if (!bills) return send_atm_error(res, AtmError::InvalidAmount);

        const std::string account_type =
            read_string(*body, "accountType", "account").value_or("Checking");
        const auto card = _atm_controller->get_active_card();

        const auto dep = _atm_controller->process_deposit(*bills, account_type);
        if (!dep)
        {
            return send_atm_error(res, dep.error());
        }
        ++_deposit_cycles;

        const bool is_savings = (account_type == "Savings" || account_type == "savings" ||
                                 account_type == dep->accounts.savings_id);
        const std::string& receipt_account =
            is_savings ? dep->accounts.savings_id : dep->accounts.checking_id;

        const std::string pan = card ? card->number : "";
        const json receipt = build_receipt(pan, "DEPOSIT", receipt_account,
                                           dep->total_deposited_cents,
                                           dep->target_new_balance_cents, 0,
                                           dep->overflow_swept_cents);

        send_json(res, 200, {
            {"status", "ok"},
            {"totalDeposited", dep->total_deposited_cents},
            {"overflowSweptCents", dep->overflow_swept_cents},
            {"newBalance", dep->target_new_balance_cents},
            {"accounts", build_accounts_json(dep->accounts)},
            {"receipt", receipt}
        });
    });

    _server.Post("/api/customer/transfer", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!verify_bearer_token(*_atm_controller, req, res)) return;

        const auto body = parse_json_body(req, res);
        if (!body) return;

        const auto from = read_string(*body, "from", "from_account_id");
        const auto to = read_string(*body, "to", "to_account_id");
        const auto amount = read_int64(*body, "amount");
        if (!from || !to || from->empty() || to->empty() || !amount)
        {
            return send_atm_error(res, AtmError::InvalidAmount);
        }

        const std::string nonce =
            read_string(*body, "nonce", "idempotencyKey").value_or("");
        const auto card = _atm_controller->get_active_card();

        const auto tr = _atm_controller->process_transfer(*from, *to, *amount, nonce);
        if (!tr)
        {
            return send_atm_error(res, tr.error());
        }

        std::string tx_type = "TRANSFER TO " + *to;
        for (char& ch : tx_type)
        {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }

        const std::string pan = card ? card->number : "";
        const json receipt =
            build_receipt(pan, tx_type, *from, *amount, tr->source_new_balance_cents);

        send_json(res, 200, {
            {"status", "ok"},
            {"newBalance", tr->source_new_balance_cents},
            {"accounts", build_accounts_json(tr->accounts)},
            {"receipt", receipt}
        });
    });

    _server.Get("/api/engineer/vault", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!verify_engineer_headers(*_atm_controller, req, res)) return;

        send_json(res, 200,
                  build_vault_json(_atm_controller->get_vault(),
                                   _dispense_cycles.load(), _deposit_cycles.load()));
    });

    _server.Post("/api/engineer/vault-door", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!verify_engineer_headers(*_atm_controller, req, res)) return;

        const auto body = parse_json_body(req, res);
        if (!body) return;

        bool open = _atm_controller->get_vault().is_door_closed();
        if (const auto it = body->find("open"); it != body->end() && it->is_boolean())
        {
            open = it->get<bool>();
        }

        const auto r = open ? _atm_controller->open_vault_door()
                            : _atm_controller->close_vault_door();
        if (!r) return send_atm_error(res, r.error());

        send_json(res, 200, {
            {"status", "ok"},
            {"doorOpen", !_atm_controller->get_vault().is_door_closed()}
        });
    });

    _server.Post("/api/engineer/replenish", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!verify_engineer_headers(*_atm_controller, req, res)) return;

        const auto body = parse_json_body(req, res);
        if (!body) return;

        if (const auto it = body->find("resetCounters");
            it != body->end() && it->is_boolean() && it->get<bool>())
        {
            _dispense_cycles = 0;
            _deposit_cycles = 0;
            json payload = build_vault_json(_atm_controller->get_vault(), 0, 0);
            payload["status"] = "ok";
            return send_json(res, 200, payload);
        }

        std::optional<Denomination> denom;
        if (const auto idx = read_int64(*body, "cassetteIndex"))
        {
            denom = to_denomination_by_index(*idx);
        }
        else if (const auto d = read_int64(*body, "denom"))
        {
            denom = to_denomination(*d);
            if (!denom) denom = to_denomination(*d * 100);
        }

        const auto count = read_int64(*body, "addCount", "delta");
        if (!denom || !count)
        {
            return send_atm_error(res, AtmError::InvalidAmount);
        }

        if (const auto r =
                _atm_controller->process_replenish(*denom, static_cast<int>(*count));
            !r)
        {
            return send_atm_error(res, r.error());
        }

        json payload = build_vault_json(_atm_controller->get_vault(),
                                        _dispense_cycles.load(), _deposit_cycles.load());
        payload["status"] = "ok";
        const auto& cassettes = _atm_controller->get_vault().get_cassettes();
        if (const auto it = cassettes.find(*denom); it != cassettes.end())
        {
            payload["cassette"] = {
                {"denom", static_cast<std::int64_t>(*denom)},
                {"count", it->second.get_count()},
                {"capacity", it->second.get_max_capacity()}
            };
        }
        send_json(res, 200, payload);
    });

    _server.Get("/api/engineer/audit", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!verify_engineer_headers(*_atm_controller, req, res)) return;

        const auto entries = _atm_controller->get_audit_entries();
        if (!entries) return send_atm_error(res, entries.error());

        json records = json::array();
        for (std::size_t i = entries->size(); i > 0; --i)
        {
            const auto& e = (*entries)[i - 1];
            records.push_back({
                {"id", i},
                {"timestamp", e.timestamp},
                {"event", e.operation_type},
                {"detail", e.debit_account_id + "->" + e.credit_account_id},
                {"amountCents", e.amount_cents},
                {"prevHash", e.prev_hash},
                {"hash", e.curr_hash},
                {"tampered", false}
            });
        }
        send_json(res, 200, {{"records", records}});
    });

    _server.Post("/api/engineer/verify-integrity", [this](const httplib::Request& req, httplib::Response& res)
    {
        if (!verify_engineer_headers(*_atm_controller, req, res)) return;

        const auto verification = _atm_controller->verify_audit_integrity();
        if (!verification) return send_atm_error(res, verification.error());

        const auto entries = _atm_controller->get_audit_entries();
        if (!entries) return send_atm_error(res, entries.error());

        const std::string detail = verification->ok
            ? "SHA-256 + HMAC chain verified (" + std::to_string(entries->size()) + " records intact)."
            : verification->detail;

        send_json(res, 200, {
            {"valid", verification->ok},
            {"checkedRecords", entries->size()},
            {"brokenRecordId", verification->ok ? json(nullptr) : json(verification->first_broken_index + 1)},
            {"detail", detail}
        });
    });
}