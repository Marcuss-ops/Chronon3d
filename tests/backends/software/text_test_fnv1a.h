// ═══════════════════════════════════════════════════════════════════════════
// tests/backends/software/text_test_fnv1a.h
// ═══════════════════════════════════════════════════════════════════════════
//
// Shared FNV-1a 64-bit hash utility for text backend golden raster and
// bench hash-equality tests.  Declared `inline` so it can be included by
// multiple translation units without redefinition errors (critical for
// unity builds where .cpp files are batched together).
//
// Pattern precedent: tests/scene/camera/_golden/ FNV-1a conventions.

#pragma once

#include <cstddef>
#include <cstdint>

inline std::uint64_t fnv1a_64(const std::uint8_t* data, std::size_t n) noexcept {
    constexpr std::uint64_t kOffset = 0xcbf29ce484222325ULL;
    constexpr std::uint64_t kPrime  = 0x100000001b3ULL;
    std::uint64_t h = kOffset;
    for (std::size_t i = 0; i < n; ++i) {
        h ^= static_cast<std::uint64_t>(data[i]);
        h *= kPrime;
    }
    return h;
}
