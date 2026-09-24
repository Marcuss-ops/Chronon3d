#pragma once

// ── hash_builder.hpp — Unified, dependency-free hash builder ───────────────
//
// Provides a fluent API for building u64 digests from cache-key fields.
// Replaces the duplicate hash_combine / hash_string / hash_value boilerplate
// that was scattered across frame_cache_detail, node_cache_detail,
// video_frame_cache, and converted_frame_cache.
//
// Uses XXH64 for strings (deterministic, same as render_graph) and the
// standard boost::hash_combine for mixing.  All digests are deterministic
// across runs — required for persistent disk caches keyed by digest.

#include <cstddef>
#include <cstdint>
#include <concepts>
#include <cstring>
#include <string_view>
#include <type_traits>
#include <xxhash.h>

#include <chronon3d/core/types/frame.hpp>

namespace chronon3d::core::hash {

// ── Canonical digest primitives (Cat-3 single seat) ──────────────────────
// The mixing formula and the XXH64 byte/string digests are defined ONCE,
// here.  Domain spellings (graph::hash_combine/hash_bytes/hash_string in
// render_graph_hashing.hpp, text::detail::hcombine/hbytes/hstring in
// src/text/hash_helpers.hpp) delegate to these, so every cache-key digest
// shares one primitive implementation.

/// Standard boost::hash_combine mixing formula.
[[nodiscard]] inline uint64_t hash_combine(uint64_t seed, uint64_t value) {
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
}

/// Deterministic raw-bytes digest (XXH64).
[[nodiscard]] inline uint64_t hash_bytes(const void* data, std::size_t size) {
    return XXH64(data, size, 0);
}

/// Deterministic string digest (XXH64).
[[nodiscard]] inline uint64_t hash_string(std::string_view value) {
    return XXH64(value.data(), value.size(), 0);
}

/// Fluent hash builder for cache-key digests.
///
/// Usage:
///   return HashBuilder{}
///       .add(composition_id)
///       .add(frame)
///       .add(width)
///       .add(height)
///       .add(scene_hash)
///       .add(render_hash)
///       .finish();
class HashBuilder {
public:
    /// Hash a string_view via XXH64 (deterministic across runs).
    HashBuilder& add(std::string_view value) {
        mix(hash_string(value));
        return *this;
    }

    /// Hash any integral type (int, i32, u64, size_t, etc.).
    /// Enabled only for integral types to avoid accidental enum decay.
    template <std::integral T>
    HashBuilder& add(T value) {
        mix(static_cast<uint64_t>(value));
        return *this;
    }

    /// Hash a Frame (strong type, not integral).
    HashBuilder& add(Frame f) {
        mix(static_cast<uint64_t>(f.integral()));
        return *this;
    }

    /// Hash the bit pattern of a floating-point value.
    template <std::floating_point T>
    HashBuilder& add(T value) {
        uint64_t bits{0};
        std::memcpy(&bits, &value, sizeof(value));
        mix(bits);
        return *this;
    }

    /// Hash an enum by its underlying integer.
    template <typename T>
        requires std::is_enum_v<T>
    HashBuilder& add_enum(T value) {
        mix(static_cast<uint64_t>(value));
        return *this;
    }

    /// Hash raw bytes (reinterpret-cast to uint64_t blocks, then
    /// hash_combine the trailing bytes if any).
    HashBuilder& add_bytes(const void* data, std::size_t size) {
        const auto* p = static_cast<const uint8_t*>(data);
        const std::size_t full = size / sizeof(uint64_t);
        for (std::size_t i = 0; i < full; ++i) {
            uint64_t block{0};
            std::memcpy(&block, p + i * sizeof(uint64_t), sizeof(uint64_t));
            mix(block);
        }
        // Tail bytes
        const std::size_t tail = size % sizeof(uint64_t);
        if (tail) {
            uint64_t block{0};
            std::memcpy(&block, p + full * sizeof(uint64_t), tail);
            mix(block);
        }
        return *this;
    }

    /// Return the final digest.
    [[nodiscard]] uint64_t finish() const noexcept { return state_; }

private:
    void mix(uint64_t value) {
        state_ = hash_combine(state_, value);  // canonical free function above
    }

    uint64_t state_{0x9e3779b97f4a7c15ULL}; // golden-ratio seed
};

} // namespace chronon3d::core::hash
