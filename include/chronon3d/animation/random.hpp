#pragma once

// ──────────────────────────────────────────────────────────────────────────
// animation/random.hpp — Canonical deterministic random API
//
// Single source of truth for hash-driven deterministic pseudo-randomness
// in the Chronon3D animation module (TICKET-RANDOM-UNIFY, Cat-3 anti-dup).
//
// API:
//   deterministic_random(RandomSeed seed, u64 index = 0)
//       Pure hash → [0, 1) bit-stable across libcs. NO accumulator state.
//   deterministic_random(std::string_view seed_str, u64 index = 0)
//       Hash the seed_str via XXH64 then funnel into the u64 pipeline.
//   build_random_permutation(RandomSeed seed, u32 total_units)
//       Fisher-Yates bijection on [0, total_units). Recomputed per call
//       (no thread_local cache) — zero global/thread-local state.
//
// Cross-OS bit-identity: the final result uses
//   static_cast<f32>(x >> 40) / static_cast<f32>(1ULL << 24)
// with 1ULL << 24 = 16777216 exactly representable in IEEE-754 binary32.
// The integer shift + u32-f32 cast + IEEE-754 division are all libc-stable
// (no rounding-mode / f64 narrowing-cast surprises).
//
// Determinism note: pure functions of (seed, index) only. NO time, NO
// PRNG state, NO threads, NO file-system inputs.
// ──────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <string_view>
#include <vector>

#include <chronon3d/core/types/types.hpp>  // u32, u64, f32 typedefs (canonical)
#include <xxhash.h>

namespace chronon3d {

/// RandomSeed — explicit strong type to prevent ambiguous u64 → RandomSeed
/// conversions at the API surface. Construct with brace-init only.
struct RandomSeed {
    u64 value;
};

/// Deterministic pseudo-random float in [0, 1) for (seed, index).
///
/// Cross-platform bit-stable (see header math note). Output range exclusive
/// at 1.0 (corresponds to bits [0, 2^24) of the hash).
[[nodiscard]] inline f32 deterministic_random(
    RandomSeed seed, u64 index = 0) noexcept
{
    u64 x = seed.value;
    // Boost-style hash_combine: mix index into the u64 hash state.
    x ^= index + 0x9e3779b97f4a7c15ULL + (x << 6) + (x >> 2);
    // Splitmix64-style finalizer — 2 rounds + xor-shift for avalanche.
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x ^= (x >> 31);
    // Bit-stable f32 normalization. 1ULL << 24 = 16777216 is exactly
    // representable in binary32; the integer shift + u32→f32 cast +
    // IEEE-754 division give bit-identical output across libcs (Linux /
    // macOS / Windows / glibc / musl). No f64 intermediate round.
    return static_cast<f32>(static_cast<u32>(x >> 40))
         / static_cast<f32>(1ULL << 24);
}

/// Deterministic pseudo-random float in [0, 1) for (string_view seed, index).
///
/// Hashes the seed_str via XXH64 (canonical deterministic hash for byte
/// sequences), then dispatches into the u64 pipeline. The XXH64 → u64
/// fold is bit-stable across platforms (XXH64 specifies bit-exact output).
[[nodiscard]] inline f32 deterministic_random(
    std::string_view seed_str, u64 index = 0) noexcept
{
    return deterministic_random(
        RandomSeed{XXH64(seed_str.data(), seed_str.size(), 0)},
        index);
}

/// Fisher-Yates permutation on [0, total_units) keyed by RandomSeed.
///
/// Returns a by-value `std::vector<u32>` containing a BIJECTION (each
/// integer in [0, total_units) appears exactly once) — callers store
/// this directly without further wrapping. Recomputed per call (no
/// thread_local cache) so zero global/thread-local state escapes this
/// header.
[[nodiscard]] inline std::vector<u32> build_random_permutation(
    RandomSeed seed, u32 total_units)
{
    std::vector<u32> perm(total_units);
    for (u32 i = 0; i < total_units; ++i) perm[i] = i;
    for (u32 i = total_units; i > 1; --i) {
        const u32 u = i - 1;
        // Use the canonical hash pipeline (seed, u64-index) to ensure
        // bit-identical Fisher-Yates across libcs.
        const f32 r = deterministic_random(seed, static_cast<u64>(u));
        const u32 raw_j = static_cast<u32>(static_cast<f32>(i) * r);
        const u32 j = (raw_j < i) ? raw_j : u;
        std::swap(perm[u], perm[j]);
    }
    return perm;
}

} // namespace chronon3d
