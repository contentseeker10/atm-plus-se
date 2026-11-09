/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: crypto.cpp
 * Implementation of crypto utilities via OpenSSL.
 *
 */

#include "crypto.hpp"

#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <array>
#include <format>
#include <stdexcept>

namespace crypto
{
    namespace
    {
        std::string to_hex(const uint8_t* data, size_t length) {
            std::string hex_str;
            hex_str.reserve(length * 2);
            for (std::size_t i = 0; i < length; ++i) {
                hex_str += std::format("{:02x}", data[i]);
            }
            return hex_str;
        }
    } // namespace
    
    std::vector<uint8_t> random_bytes(std::size_t size) {
        std::vector<uint8_t> bytes(size);
        if (RAND_bytes(bytes.data(), static_cast<int>(size)) != 1) {
            throw std::runtime_error("OpenSSL RAND_bytes failed to generate entropy");
        }
        return bytes;
    }

    std::string generate_uuid_v4() {
        std::array<uint8_t, 16> bytes{};
        if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) {
            throw std::runtime_error("RAND_bytes failed for UUIDv4 generation");
        }
        // RFC 4122 v4 compliance
        bytes[6] = (bytes[6] & 0x0F) | 0x40; // Version 4
        bytes[8] = (bytes[8] & 0x3F) | 0x80; // Variant 10xx
        return std::format(
            "{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
            bytes[0], bytes[1], bytes[2], bytes[3],
            bytes[4], bytes[5],
            bytes[6], bytes[7],
            bytes[8], bytes[9],
            bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]
        );
    }

    std::string sha256_hex(std::string_view data, std::string_view salt) {
        std::array<uint8_t, 32> hash{};
        size_t out_len = 0;
        std::string salted_payload;
        salted_payload.reserve(salt.size() + data.size());
        salted_payload.append(salt);
        salted_payload.append(data);
        if (EVP_Q_digest(nullptr, "SHA256", nullptr, 
                        salted_payload.data(), salted_payload.size(), 
                        hash.data(), &out_len) != 1 || out_len != hash.size()) {
            throw std::runtime_error("EVP_Q_digest failed for SHA256");
        }
        return to_hex(hash.data(), hash.size());
    }

    std::string hmac_sha256_hex(std::string_view data, std::string_view key) {
        std::array<uint8_t, 32> mac{};
        size_t out_len = 0;
        if (EVP_Q_mac(nullptr, "HMAC", nullptr, "SHA256", nullptr,
                    key.data(), key.size(),
                    reinterpret_cast<const uint8_t*>(data.data()), data.size(),
                    mac.data(), mac.size(), &out_len) == nullptr || out_len != mac.size()) {
            throw std::runtime_error("EVP_Q_mac failed for HMAC-SHA256");
        }
        return to_hex(mac.data(), mac.size());
    }

    bool constant_time_compare(std::string_view a, std::string_view b) noexcept {
        if (a.size() != b.size()) {
            return false;
        }
        return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
    }
} // namespace crypto
