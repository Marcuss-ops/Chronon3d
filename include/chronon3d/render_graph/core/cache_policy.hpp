#pragma once

#include <string>
#include <string_view>

namespace chronon3d::graph {

// ---------------------------------------------------------------------------
// CacheLifetime
// ---------------------------------------------------------------------------
enum class CacheLifetime {
    /// Cache lives only for the current frame's execution (default for animated nodes).
    PerFrame,
    /// Cache persists for the entire composition duration (static/invariant nodes).
    PersistentDisk
};

// ---------------------------------------------------------------------------
// CacheInvalidation
// ---------------------------------------------------------------------------
enum class CacheInvalidation {
    /// Always re-execute the node (no caching).
    Always,
    /// Re-execute only when this node's parameters change.
    WhenParamsChange,
    /// Re-execute when this node's input data changes.
    WhenInputsChange
};

// ---------------------------------------------------------------------------
// RenderNodeCachePolicy
// ---------------------------------------------------------------------------
/// Describes the caching behaviour of a single RenderGraphNode.
/// Replaces the simpler CacheFramePolicy with a richer, more expressive policy
/// that the GraphExecutor can use to make smarter caching decisions.
struct RenderNodeCachePolicy {
    /// Whether the node's output can be cached at all.
    bool cacheable{true};

    /// If false, the frame number is NOT part of the cache key.
    /// Equivalent to CacheFramePolicy::FrameInvariant.
    bool frame_dependent{true};

    /// If true, the output is invariant across frames (frame number not needed
    /// as a cache dimension).  Implies frame_dependent == false.
    bool frame_invariant{false};

    /// Whether the cache entry can be serialized to disk (future).
    bool disk_cacheable{false};

    /// How long the cache entry should live.
    CacheLifetime lifetime{CacheLifetime::PerFrame};

    /// What triggers cache invalidation.
    CacheInvalidation invalidation{CacheInvalidation::WhenInputsChange};

    /// Human-readable reason for this policy (used in telemetry/debug).
    std::string debug_reason{"default"};
};

// ---------------------------------------------------------------------------
// Policy helpers
// ---------------------------------------------------------------------------

/// A fully static node whose output never changes once computed
/// (e.g. grid background, static image source, static shape).
inline RenderNodeCachePolicy static_memory_cache(std::string reason = {}) {
    return RenderNodeCachePolicy{
        .cacheable = true,
        .frame_dependent = false,
        .frame_invariant = true,
        .disk_cacheable = true,
        .lifetime = CacheLifetime::PersistentDisk,
        .invalidation = CacheInvalidation::WhenParamsChange,
        .debug_reason = reason.empty() ? "static_memory_cache" : std::move(reason)
    };
}

/// An animated node whose output changes per-frame and must be re-evaluated
/// each time, but is still cacheable within the same frame
/// (e.g. animated transform, per-frame effect).
inline RenderNodeCachePolicy animated_cache(std::string reason = {}) {
    return RenderNodeCachePolicy{
        .cacheable = true,
        .frame_dependent = true,
        .frame_invariant = false,
        .disk_cacheable = false,
        .lifetime = CacheLifetime::PerFrame,
        .invalidation = CacheInvalidation::WhenInputsChange,
        .debug_reason = reason.empty() ? "animated_cache" : std::move(reason)
    };
}

/// A node that should never be cached under any circumstances
/// (e.g. video source, motion blur, output node).
inline RenderNodeCachePolicy no_cache(std::string reason = {}) {
    return RenderNodeCachePolicy{
        .cacheable = false,
        .frame_dependent = true,
        .frame_invariant = false,
        .disk_cacheable = false,
        .lifetime = CacheLifetime::PerFrame,
        .invalidation = CacheInvalidation::Always,
        .debug_reason = reason.empty() ? "no_cache" : std::move(reason)
    };
}

} // namespace chronon3d::graph
