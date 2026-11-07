/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: anti_replay.hpp
 * Interface for Anti-Replay protection service.
 *
 */

#pragma once

#include <unordered_set>
#include <unordered_map>
#include <string>
#include <chrono>
#include <mutex>

class AntiReplayManager {
    using clock = std::chrono::steady_clock;
public:
    AntiReplayManager()  = default;
    ~AntiReplayManager() = default;

    AntiReplayManager(const AntiReplayManager&)            = delete;
    AntiReplayManager& operator=(const AntiReplayManager&) = delete;
    AntiReplayManager(AntiReplayManager&&)                 = delete;
    AntiReplayManager& operator=(AntiReplayManager&&)      = delete;

    bool validate_and_consume(std::string_view session_id, uint64_t incoming_nonce);
    bool is_request_unique(std::string_view idemp_key);
    
    void purge_expired_keys(std::chrono::seconds max_age);
    void reset_session(std::string_view session_id);
    void register_session(std::string_view session_id, uint64_t initial_nonce = 0);
private:
    std::unordered_map<std::string, clock::time_point> _seen_idemp_keys;
    std::unordered_map<std::string, uint64_t>          _session_nonces;

    mutable std::mutex _mtx;
};