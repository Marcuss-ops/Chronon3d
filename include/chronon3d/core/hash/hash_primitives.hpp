#pragma once

#include <cstdint>

namespace chronon3d::core::hash {

/// Standard boost::hash_combine mixing formula.
[[nodiscard]] inline constexpr std::uint64_t hash_combine(
    std::uint64_t seed, std::uint64_t value) noexcept {
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
}

/// SplitMix64 avalanche finalizer for an already-mixed 64-bit value.
[[nodiscard]] inline constexpr std::uint64_t splitmix64_finalize(
    std::uint64_t value) noexcept {
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

/// SplitMix64 sequence step: advance by its Weyl constant, then avalanche.
[[nodiscard]] inline constexpr std::uint64_t splitmix64(
    std::uint64_t value) noexcept {
    return splitmix64_finalize(value + 0x9e3779b97f4a7c15ULL);
}

} // namespace chronon3d::core::hash
