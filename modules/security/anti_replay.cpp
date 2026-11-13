/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: anti_replay.cpp
 * Implementation of Anti-Replay protection service.
 *
 */

#include "anti_replay.hpp"

bool AntiReplayManager::validate_and_consume(std::string_view session_id,
                                             uint64_t incoming_nonce)
{
    std::unique_lock lock(_mtx);

    auto it = _session_nonces.find(std::string(session_id));
    if (it == _session_nonces.end()) { return false; }

    if (incoming_nonce <= it->second) { return false; }

    it->second = incoming_nonce;
    return true;
}

bool AntiReplayManager::is_request_unique(std::string_view idemp_key)
{
    if (idemp_key.empty()) { return false; }

    std::unique_lock lock(_mtx);
    const auto now = clock::now();
    const auto [it, inserted] = _seen_idemp_keys.try_emplace(std::string(idemp_key), now);

    return inserted;
}

void AntiReplayManager::purge_expired_keys(std::chrono::seconds max_age)
{
    std::unique_lock lock(_mtx);
    const auto now = clock::now();
    std::erase_if(_seen_idemp_keys, [now, max_age](const auto& pair)
                  { return (now - pair.second) > max_age; });
}

void AntiReplayManager::reset_session(std::string_view session_id)
{
    std::unique_lock lock(_mtx);
    _session_nonces.erase(std::string(session_id));
}

void AntiReplayManager::register_session(std::string_view session_id,
                                         uint64_t initial_nonce)
{
    std::unique_lock lock(_mtx);
    _session_nonces[std::string(session_id)] = initial_nonce;
}