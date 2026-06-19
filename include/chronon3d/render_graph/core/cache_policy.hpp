#pragma once

#include <cstdint>
#include <string_view>

namespace chronon3d::graph {

// ---------------------------------------------------------------------------
// CacheMode — single canonical axis for the cache contract
// ---------------------------------------------------------------------------
//
// fields. Every node now describes its cache behaviour *exclusively* via this
// enum, exposed through `RenderNodeCachePolicy::mode`. The combinations below
// are exhaustive: by collapsing onto one enum, callers cannot construct
// contradictory states (e.g. "disabled but reusable across frames") that the
// old 4-flag struct permitted.
// ---------------------------------------------------------------------------
enum class CacheMode : std::uint8_t {
    /// Caching is disabled — node re-executes for every consumer call.
    /// `reusable_across_frames` and `persistent` are both false and undefined.
    Disabled,

    /// Output changes per frame; cache key must include the frame number.
    /// Multiple consumers within the same render frame may still dedupe.
    FrameVariant,

    /// Output is invariant across frames; entries live in memory only.
    /// Survives across frames within a single composition run.
    FrameInvariantMemory,

    /// Output is invariant across frames AND persisted to durable storage.
    /// Survives across process restarts (designed for precomputed bakes).
    FrameInvariantPersistent,
};

// ---------------------------------------------------------------------------
// CacheInvalidation — orthogonal axis for invalidation triggers
// ---------------------------------------------------------------------------
enum class CacheInvalidation {
    Always,              ///< re-execute unconditionally (used with Disabled)
    WhenParamsChange,    ///< re-execute when node parameters change
    WhenInputsChange,    ///< re-execute when any input node produces a new output
};

// ---------------------------------------------------------------------------
// RenderNodeCachePolicy — immutable single-axis descriptor
// ---------------------------------------------------------------------------
struct RenderNodeCachePolicy {
    CacheMode mode{CacheMode::FrameVariant};
    CacheInvalidation invalidation{CacheInvalidation::WhenInputsChange};
    std::string_view reason{"default"};

    [[nodiscard]] constexpr bool enabled() const noexcept {
        return mode != CacheMode::Disabled;
    }

    [[nodiscard]] constexpr bool frame_dependent() const noexcept {
        return mode == CacheMode::FrameVariant;
    }

    [[nodiscard]] constexpr bool reusable_across_frames() const noexcept {
        return mode == CacheMode::FrameInvariantMemory
            || mode == CacheMode::FrameInvariantPersistent;
    }

    [[nodiscard]] constexpr bool persistent() const noexcept {
        return mode == CacheMode::FrameInvariantPersistent;
    }
};

// ---------------------------------------------------------------------------
// Canonical factory helpers (constexpr noexcept, string_view reason)
// ---------------------------------------------------------------------------
constexpr RenderNodeCachePolicy no_cache(std::string_view reason) noexcept {
    return RenderNodeCachePolicy{
        .mode = CacheMode::Disabled,
        .invalidation = CacheInvalidation::Always,
        .reason = reason,
    };
}

constexpr RenderNodeCachePolicy frame_variant_cache(std::string_view reason) noexcept {
    return RenderNodeCachePolicy{
        .mode = CacheMode::FrameVariant,
        .invalidation = CacheInvalidation::WhenInputsChange,
        .reason = reason,
    };
}

constexpr RenderNodeCachePolicy static_memory_cache(std::string_view reason) noexcept {
    return RenderNodeCachePolicy{
        .mode = CacheMode::FrameInvariantMemory,
        .invalidation = CacheInvalidation::WhenParamsChange,
        .reason = reason,
    };
}

constexpr RenderNodeCachePolicy static_persistent_cache(std::string_view reason) noexcept {
    return RenderNodeCachePolicy{
        .mode = CacheMode::FrameInvariantPersistent,
        .invalidation = CacheInvalidation::WhenParamsChange,
        .reason = reason,
    };
}

} // namespace chronon3d::graph
