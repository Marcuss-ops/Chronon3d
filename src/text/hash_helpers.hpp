#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// hash_helpers.hpp — shared XXH64-based hash combiners
//
// Internal header (src/text/) — NOT part of the public API.
// Extracted from text_document.cpp and timed_text_document.cpp (TICKET-011)
// to eliminate the duplicate anonymous-namespace definitions that caused
// ODR violations in unity builds.
//
// Determinism: pure, no threads, no time, no PRNG.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/hash/hash_builder.hpp>

#include <xxhash.h>
#include <string_view>
#include <cstddef>
#include <cstdint>

namespace chronon3d {
namespace text {
namespace detail {

/// Combine two u64 hashes.  Cat-3: delegates to the canonical digest
/// primitive (core/hash/hash_builder.hpp) shared with the render-graph and
/// cache-key hashing; the formula is unchanged, so digest values are
/// bit-identical to the historical inline versions.
[[nodiscard]] inline u64 hcombine(u64 seed, u64 value) {
    return core::hash::hash_combine(seed, value);
}

/// Hash raw bytes via XXH64.
[[nodiscard]] inline u64 hbytes(const void* data, size_t size) {
    return core::hash::hash_bytes(data, size);
}

/// Hash a string_view via XXH64.
[[nodiscard]] inline u64 hstring(std::string_view sv) {
    return core::hash::hash_string(sv);
}

/// Hash a trivially-copyable value via its byte representation.
template <typename T>
[[nodiscard]] u64 hval(const T& v) {
    return hbytes(&v, sizeof(v));
}

}  // namespace detail
}  // namespace text
}  // namespace chronon3d
