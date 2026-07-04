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

#include <xxhash.h>
#include <string_view>
#include <cstddef>
#include <cstdint>

namespace chronon3d {
namespace text {
namespace detail {

/// Combine two u64 hashes (boost::hash_combine-style, matching the
/// codebase convention from render_graph_hashing.hpp).
[[nodiscard]] inline u64 hcombine(u64 seed, u64 value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

/// Hash raw bytes via XXH64.
[[nodiscard]] inline u64 hbytes(const void* data, size_t size) {
    return XXH64(data, size, 0);
}

/// Hash a string_view via XXH64.
[[nodiscard]] inline u64 hstring(std::string_view sv) {
    return XXH64(sv.data(), sv.size(), 0);
}

/// Hash a trivially-copyable value via its byte representation.
template <typename T>
[[nodiscard]] u64 hval(const T& v) {
    return hbytes(&v, sizeof(v));
}

}  // namespace detail
}  // namespace text
}  // namespace chronon3d
