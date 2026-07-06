// ---------------------------------------------------------------------------
// glyph_selector_random.cpp — Permutation + pseudo-random helpers
// extracted from glyph_selector.cpp (FASE 8 Step 1)
// ---------------------------------------------------------------------------

#include "glyph_selector_random.hpp"
#include <mutex>
#include <numeric>
#include <unordered_map>

namespace chronon3d::detail {

// ── hash_combine (local, avoids dependency on render_graph_hashing.hpp) ──

[[nodiscard]] inline u64 hash_combine_local(u64 seed, u64 value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

// ── hash_to_unit_float ────────────────────────────────────────────────────

f32 hash_to_unit_float(u64 seed, u64 unit_index) {
    u64 x = hash_combine_local(seed, unit_index);
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x = x ^ (x >> 31);
    constexpr f64 kInvU64Max = 1.0 / static_cast<f64>(UINT64_MAX);
    return static_cast<f32>(static_cast<f64>(x) * kInvU64Max);
}

// ── Permutation cache (Fisher-Yates) ──────────────────────────────────────

struct PermutationKey {
    u64 seed;
    u32 total_units;
    bool operator==(const PermutationKey& o) const noexcept {
        return seed == o.seed && total_units == o.total_units;
    }
};

struct PermutationKeyHash {
    size_t operator()(const PermutationKey& k) const noexcept {
        size_t h = static_cast<size_t>(k.seed);
        h ^= static_cast<size_t>(k.total_units) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        return h;
    }
};

const std::vector<u32>& get_or_build_permutation(u64 seed, u32 total_units) {
    static std::unordered_map<PermutationKey, std::vector<u32>, PermutationKeyHash> cache;
    static std::mutex cache_mutex;
    PermutationKey key{seed, total_units};
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        auto it = cache.find(key);
        if (it != cache.end()) {
            return it->second;
        }
    }

    std::vector<u32> perm(total_units);
    std::iota(perm.begin(), perm.end(), 0u);
    for (u32 i = total_units; i > 1; --i) {
        const u32 u = i - 1;
        const f32 r = hash_to_unit_float(seed, static_cast<u64>(u));
        const u32 raw_j = static_cast<u32>(static_cast<f32>(i) * r);
        const u32 j = (raw_j < i) ? raw_j : u;
        std::swap(perm[u], perm[j]);
    }
    std::lock_guard<std::mutex> lock(cache_mutex);
    auto inserted = cache.emplace(key, std::move(perm));
    return inserted.first->second;
}

} // namespace chronon3d::detail
