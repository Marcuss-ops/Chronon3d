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
    /// Cache may persist across frames in the composition duration.
    /// When `disk_cacheable` is also true, the policy is eligible for the
    /// persistent disk bake (PersistentFramebufferStore).
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
/// Canonical cache descriptor for a single RenderGraphNode.
///
/// The GraphExecutor reads ONLY `node.cache_policy()` and dispatches on:
///   - `cacheable()` / `enabled()` to decide whether to skip lookup
///   - `frame_dependent()` to decide whether the frame number belongs in the
///     cache key
///   - `persistent()` to decide whether the PersistentFramebufferStore may be
///     queried
///
/// Builder passes set this at construction via `RenderGraphNode::set_cache_policy()`
/// before the node is observed by the executor.
struct RenderNodeCachePolicy {
    /// Whether the node's output can be cached at all.
    bool cacheable{true};

    /// If `true`, the frame number is part of the cache key (frame-variant).
    /// If `false`, the frame number is NOT part of the cache key (frame-invariant).
    bool frame_dependent{true};

    /// Reserved inverse of `frame_dependent`; kept for forward compatibility.
    /// Treat `frame_dependent` as the source of truth.
    bool frame_invariant{false};

    /// Whether the cache entry is eligible for the persistent disk bake
    /// (PersistentFramebufferStore).
    bool disk_cacheable{false};

    /// How long the cache entry should live.
    CacheLifetime lifetime{CacheLifetime::PerFrame};

    /// What triggers cache invalidation.
    CacheInvalidation invalidation{CacheInvalidation::WhenInputsChange};

    /// Human-readable reason for this policy (used in telemetry/debug).
    std::string debug_reason{"default"};

    /// Combined accessor matching the spec pseudocode `policy.persistent()`:
    /// the policy enables persistent (disk) lookup.
    [[nodiscard]] constexpr bool persistent() const noexcept {
        return disk_cacheable && lifetime == CacheLifetime::PersistentDisk;
    }

    /// Convenience: equivalent to `!cacheable`.  Used by GraphExecutor to take
    /// the "no cache" path (still executes the node, but skips all lookup).
    [[nodiscard]] constexpr bool enabled() const noexcept {
        return cacheable;
    }
};

// ---------------------------------------------------------------------------
// Policy factories (1:1 with the spec policies)
// ---------------------------------------------------------------------------
//
//   - `no_cache`              : never cache (Video, motion-blur, Output)
//   - `frame_variant_cache`   : animated, cacheable within the same frame
//   - `static_memory_cache`   : fully static, in-memory only
//   - `static_persistent_cache`: fully static, eligible for disk bake
//
// The structure is intentionally small: every factory produces a struct the
// executor reads, so adding a new factory never requires touching the
// executor.
//
// `frame_variant_cache` is the safe default (most fresh node kinds fall under
// "per-frame").
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

inline RenderNodeCachePolicy frame_variant_cache(std::string reason = {}) {
    return RenderNodeCachePolicy{
        .cacheable = true,
        .frame_dependent = true,
        .frame_invariant = false,
        .disk_cacheable = false,
        .lifetime = CacheLifetime::PerFrame,
        .invalidation = CacheInvalidation::WhenInputsChange,
        .debug_reason = reason.empty() ? "frame_variant_cache" : std::move(reason)
    };
}

inline RenderNodeCachePolicy static_memory_cache(std::string reason = {}) {
    return RenderNodeCachePolicy{
        .cacheable = true,
        .frame_dependent = false,
        .frame_invariant = true,
        .disk_cacheable = false,
        .lifetime = CacheLifetime::PerFrame,
        .invalidation = CacheInvalidation::WhenParamsChange,
        .debug_reason = reason.empty() ? "static_memory_cache" : std::move(reason)
    };
}

inline RenderNodeCachePolicy static_persistent_cache(std::string reason = {}) {
    return RenderNodeCachePolicy{
        .cacheable = true,
        .frame_dependent = false,
        .frame_invariant = true,
        .disk_cacheable = true,
        .lifetime = CacheLifetime::PersistentDisk,
        .invalidation = CacheInvalidation::WhenParamsChange,
        .debug_reason = reason.empty() ? "static_persistent_cache" : std::move(reason)
    };
}

} // namespace chronon3d::graph
