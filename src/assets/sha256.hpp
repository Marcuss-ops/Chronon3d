#pragma once

#include <chronon3d/assets/prepared_asset_manifest.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace chronon3d::assets::detail {

/// Small dependency-free SHA-256 state used by prepared asset hashing.
class Sha256 final {
public:
    Sha256() : state_{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                      0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U} {}

    void update(const void* data, std::size_t size) {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        while (size != 0) {
            const auto count = std::min(size, block_.size() - block_size_);
            std::copy_n(bytes, count, block_.begin() + block_size_);
            block_size_ += count;
            bytes += count;
            size -= count;
            total_bytes_ += count;
            if (block_size_ == block_.size()) {
                transform(block_.data());
                block_size_ = 0;
            }
        }
    }

    [[nodiscard]] ContentDigest finish() const {
        Sha256 copy = *this;
        const std::uint64_t bit_count = copy.total_bytes_ * 8U;
        const std::uint8_t one = 0x80U;
        copy.update(&one, 1);
        const std::uint8_t zero = 0;
        while (copy.block_size_ != 56U) copy.update(&zero, 1);
        std::array<std::uint8_t, 8> length{};
        for (std::size_t i = 0; i < length.size(); ++i) {
            length[length.size() - 1U - i] =
                static_cast<std::uint8_t>(bit_count >> (i * 8U));
        }
        copy.update(length.data(), length.size());

        ContentDigest digest;
        for (std::size_t i = 0; i < copy.state_.size(); ++i) {
            digest.bytes[i * 4U] = static_cast<std::byte>(copy.state_[i] >> 24U);
            digest.bytes[i * 4U + 1U] = static_cast<std::byte>(copy.state_[i] >> 16U);
            digest.bytes[i * 4U + 2U] = static_cast<std::byte>(copy.state_[i] >> 8U);
            digest.bytes[i * 4U + 3U] = static_cast<std::byte>(copy.state_[i]);
        }
        return digest;
    }

private:
    static constexpr std::array<std::uint32_t, 64> kRoundConstants{
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
        0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
        0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
        0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

    static constexpr std::uint32_t rotate_right(
        std::uint32_t value, unsigned count) noexcept {
        return (value >> count) | (value << (32U - count));
    }

    void transform(const std::uint8_t* block) noexcept {
        std::array<std::uint32_t, 64> schedule{};
        for (std::size_t i = 0; i < 16U; ++i) {
            schedule[i] = (static_cast<std::uint32_t>(block[i * 4U]) << 24U) |
                          (static_cast<std::uint32_t>(block[i * 4U + 1U]) << 16U) |
                          (static_cast<std::uint32_t>(block[i * 4U + 2U]) << 8U) |
                          static_cast<std::uint32_t>(block[i * 4U + 3U]);
        }
        for (std::size_t i = 16U; i < schedule.size(); ++i) {
            const auto s0 = rotate_right(schedule[i - 15U], 7U) ^
                            rotate_right(schedule[i - 15U], 18U) ^
                            (schedule[i - 15U] >> 3U);
            const auto s1 = rotate_right(schedule[i - 2U], 17U) ^
                            rotate_right(schedule[i - 2U], 19U) ^
                            (schedule[i - 2U] >> 10U);
            schedule[i] = schedule[i - 16U] + s0 + schedule[i - 7U] + s1;
        }
        auto working = state_;
        for (std::size_t i = 0; i < schedule.size(); ++i) {
            const auto s1 = rotate_right(working[4], 6U) ^
                            rotate_right(working[4], 11U) ^
                            rotate_right(working[4], 25U);
            const auto choose = (working[4] & working[5]) ^
                                ((~working[4]) & working[6]);
            const auto temp1 = working[7] + s1 + choose +
                               kRoundConstants[i] + schedule[i];
            const auto s0 = rotate_right(working[0], 2U) ^
                            rotate_right(working[0], 13U) ^
                            rotate_right(working[0], 22U);
            const auto majority = (working[0] & working[1]) ^
                                  (working[0] & working[2]) ^
                                  (working[1] & working[2]);
            const auto temp2 = s0 + majority;
            working[7] = working[6]; working[6] = working[5];
            working[5] = working[4]; working[4] = working[3] + temp1;
            working[3] = working[2]; working[2] = working[1];
            working[1] = working[0]; working[0] = temp1 + temp2;
        }
        for (std::size_t i = 0; i < state_.size(); ++i) state_[i] += working[i];
    }

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> block_{};
    std::size_t block_size_{0};
    std::uint64_t total_bytes_{0};
};

} // namespace chronon3d::assets::detail
