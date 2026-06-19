#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace chronon3d::graph {

// ---------------------------------------------------------------------------
// CacheMode — single discriminator that drives the executor's dispatch.
// ---------------------------------------------------------------------------
///
/// All cache decisions in the render graph flow through one of four explicit
/// modes.  Adding a new mode is a struct-only change; the executor and node
/// ctors do not need updating.
///
/// - `Disabled`               : never cache.  Executor still bypasses the
///                              node, computes the cache key for telemetry,
///                              and writes bypass counters.  (Video, motion
///                              blur, Output/Clear.)
/// - `FrameVariant`           : animated node.  Frame number is part of the
///                              cache key.  Cacheable within the same frame
///                              via the in-memory node cache.  Default for
///                              freshly constructed nodes whose kind is not
///                              statically known to be cacheable.
/// - `FrameInvariantMemory`   : fully static node.  Frame number excluded
///                              from the cache key.  Reusable across frames
///                              within the current run via the LRU in-memory
///                              cache only.
/// - `FrameInvariantPersistent`: fully static, deterministic, serializable
///                              output.  Eligible for the persistent disk
///                              bake via PersistentFramebufferStore across
///                              runs.
enum class CacheMode : std::uint8_t {
    Disabled = 0,
    FrameVariant = 1,
    FrameInvariantMemory = 2,
    FrameInvariantPersistent = 3,
};

// ---------------------------------------------------------------------------
// CacheInvalidation — orthogonal field; tells the executor what event
// triggers a re-execute (only meaningful when `mode != Disabled`).
// ---------------------------------------------------------------------------
enum class CacheInvalidation {
    /// Always re-execute the node (no caching).  Implied by `Disabled`.
    Always,
    /// Re-execute only when this node's parameters change.
    /// Pairs naturally with `FrameInvariant*` modes.
    WhenParamsChange,
    /// Re-execute when this node's input data changes.
    /// Pairs naturally with `FrameVariant` mode.
    WhenInputsChange,
};

// ---------------------------------------------------------------------------
// RenderNodeCachePolicy
// ---------------------------------------------------------------------------
///
/// Closed-form cache descriptor for a `RenderGraphNode`.
///
/// **Construction-only mutability**: the policy is set exactly once via the
/// `RenderGraphNode` ctor.  No setters, no field-by-field mutation; the four
/// factory helpers are the only way to obtain an instance.
///
/// **Single source of truth**: the GraphExecutor reads ONLY
/// `node.cache_policy()` and dispatches on the four accessors below.
/// Cacheable-look virtuals and policy mutation methods were removed.
struct RenderNodeCachePolicy {
    CacheMode mode{CacheMode::FrameVariant};
    CacheInvalidation invalidation{CacheInvalidation::WhenInputsChange};
    std::string debug_reason{"default"};

    /// True when the policy allows ANY caching (lookup, lookup-with-persist,
    /// key-fingerprint reuse).  Disabled → false.
    [[nodiscard]] constexpr bool enabled() const noexcept {
        return mode != CacheMode::Disabled;
    }

    /// True when the frame number is part of the cache key (frame-variant).
    /// Only `FrameVariant` is frame-dependent.
    [[nodiscard]] constexpr bool is_frame_variant() const noexcept {
        return mode == CacheMode::FrameVariant;
    }

    /// True when the policy enables persistent (cross-run disk) lookup.
    /// Only `FrameInvariantPersistent` is persistent.
    [[nodiscard]] constexpr bool persistent() const noexcept {
        return mode == CacheMode::FrameInvariantPersistent;
    }

    /// True when the policy allows cache reuse across frames (i.e. the
    /// node is frame-invariant in any form).  Composed of the two static
    /// modes.
    [[nodiscard]] constexpr bool reusable_across_frames() const noexcept {
        return mode == CacheMode::FrameInvariantMemory
            || mode == CacheMode::FrameInvariantPersistent;
    }
};

// ---------------------------------------------------------------------------
// Policy factories — the only public way to construct a policy.
// ---------------------------------------------------------------------------
inline RenderNodeCachePolicy no_cache(std::string reason = {}) {
    return RenderNodeCachePolicy{
        .mode = CacheMode::Disabled,
        .invalidation = CacheInvalidation::Always,
        .debug_reason = reason.empty() ? "no_cache" : std::move(reason)
    };
}

inline RenderNodeCachePolicy frame_variant_cache(std::string reason = {}) {
    return RenderNodeCachePolicy{
        .mode = CacheMode::FrameVariant,
        .invalidation = CacheInvalidation::WhenInputsChange,
        .debug_reason = reason.empty() ? "frame_variant_cache" : std::move(reason)
    };
}

inline RenderNodeCachePolicy static_memory_cache(std::string reason = {}) {
    return RenderNodeCachePolicy{
        .mode = CacheMode::FrameInvariantMemory,
        .invalidation = CacheInvalidation::WhenParamsChange,
        .debug_reason = reason.empty() ? "static_memory_cache" : std::move(reason)
    };
}

inline RenderNodeCachePolicy static_persistent_cache(std::string reason = {}) {
    return RenderNodeCachePolicy{
        .mode = CacheMode::FrameInvariantPersistent,
        .invalidation = CacheInvalidation::WhenParamsChange,
        .debug_reason = reason.empty() ? "static_persistent_cache" : std::move(reason)
    };
}

} // namespace chronon3d::graph
