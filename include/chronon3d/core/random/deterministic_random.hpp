#pragma once

// Stable, stateless random primitives shared by animation, text, effects and
// procedural content. The integer mixing and XXH64 seed contract are kept
// identical to the former animation-local implementation.

#include <chronon3d/core/types/types.hpp>

#include <cstdint>
#include <utility>
#include <string_view>
#include <vector>

#include <xxhash.h>

namespace chronon3d::random {

struct Seed {
    u64 value;
};

[[nodiscard]] inline u64 stable_seed(std::string_view value) noexcept {
    return XXH64(value.data(), value.size(), 0);
}

[[nodiscard]] inline f32 unit(Seed seed, u64 index = 0) noexcept {
    u64 x = seed.value;
    x ^= index + 0x9e3779b97f4a7c15ULL + (x << 6) + (x >> 2);
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x ^= (x >> 31);
    return static_cast<f32>(static_cast<u32>(x >> 40))
         / static_cast<f32>(1ULL << 24);
}

[[nodiscard]] inline f32 unit(u64 seed, u64 index = 0) noexcept {
    return unit(Seed{seed}, index);
}

[[nodiscard]] inline f32 unit(std::string_view seed, u64 index = 0) noexcept {
    return unit(Seed{stable_seed(seed)}, index);
}

[[nodiscard]] inline f32 range(Seed seed, u64 index,
                               f32 minimum, f32 maximum) noexcept {
    return minimum + (maximum - minimum) * unit(seed, index);
}

[[nodiscard]] inline f32 range(u64 seed, u64 index,
                               f32 minimum, f32 maximum) noexcept {
    return range(Seed{seed}, index, minimum, maximum);
}

[[nodiscard]] inline std::vector<u32> permutation(Seed seed,
                                                   u32 total_units) {
    std::vector<u32> result(total_units);
    for (u32 i = 0; i < total_units; ++i) result[i] = i;
    for (u32 i = total_units; i > 1; --i) {
        const u32 last = i - 1;
        const u32 candidate = static_cast<u32>(static_cast<f32>(i)
                                               * unit(seed, last));
        const u32 swap_index = candidate < i ? candidate : last;
        std::swap(result[last], result[swap_index]);
    }
    return result;
}

} // namespace chronon3d::random
