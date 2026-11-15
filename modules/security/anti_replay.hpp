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

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

class AntiReplayManager
{
    using clock = std::chrono::steady_clock;

public:
    inline static constexpr std::size_t kAutoPurgeThreshold = 4096;
    inline static constexpr std::chrono::seconds kDefaultKeyTtl{3600};

    AntiReplayManager() = default;
    ~AntiReplayManager() = default;

    AntiReplayManager(const AntiReplayManager&) = delete;
    AntiReplayManager& operator=(const AntiReplayManager&) = delete;
    AntiReplayManager(AntiReplayManager&&) = delete;
    AntiReplayManager& operator=(AntiReplayManager&&) = delete;

    bool validate_and_consume(std::string_view session_id, uint64_t incoming_nonce);
    bool is_request_unique(std::string_view idemp_key);

    void purge_expired_keys(std::chrono::seconds max_age);
    void reset_session(std::string_view session_id);
    void register_session(std::string_view session_id, uint64_t initial_nonce = 0);

private:
    struct StringHash
    {
        using is_transparent = void;
        std::size_t operator()(std::string_view sv) const noexcept
        {
            return std::hash<std::string_view>{}(sv);
        }
    };

    void _purge_expired_keys_unlocked(clock::time_point now, std::chrono::seconds max_age);

    std::unordered_map<std::string, clock::time_point, StringHash, std::equal_to<>>
        _seen_idemp_keys;
    std::unordered_map<std::string, uint64_t, StringHash, std::equal_to<>>
        _session_nonces;

    mutable std::mutex _mtx;
};