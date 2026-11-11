/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: crypto.hpp
 * Interface for crypto utilities via OpenSSL.
 *
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace crypto
{
    std::vector<uint8_t> random_bytes(std::size_t size);
    std::string generate_uuid_v4();
    std::string sha256_hex(std::string_view data, std::string_view salt = "");
    std::string hmac_sha256_hex(std::string_view data, std::string_view key);
    bool constant_time_compare(std::string_view a, std::string_view b) noexcept;
} // namespace crypto
