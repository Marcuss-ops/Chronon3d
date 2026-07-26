#pragma once

// Compatibility facade for the canonical core random primitives.
// New cross-domain code should include core/random/deterministic_random.hpp
// and use chronon3d::random::{stable_seed, unit, range, permutation}.

#include <chronon3d/core/random/deterministic_random.hpp>

namespace chronon3d {

using RandomSeed = random::Seed;

[[nodiscard]] inline f32 deterministic_random(RandomSeed seed,
                                              u64 index = 0) noexcept {
    return random::unit(seed, index);
}

[[nodiscard]] inline f32 deterministic_random(std::string_view seed,
                                              u64 index = 0) noexcept {
    return random::unit(seed, index);
}

[[nodiscard]] inline std::vector<u32> build_random_permutation(
    RandomSeed seed, u32 total_units) {
    return random::permutation(seed, total_units);
}

} // namespace chronon3d
