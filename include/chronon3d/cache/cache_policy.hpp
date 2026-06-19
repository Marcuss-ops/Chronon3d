#pragma once

// ── cache_policy.hpp — Centralized cache-domain configuration ───────────
//
// Every cache in the engine declares its domain (CacheDomain) and the
// single resolve_cache_policy() function maps it to:
//   explicit ctor override → Config/env override → hardcoded default
//
// This removes duplicated capacity-resolution logic from every cache facade
// (previously each had its own resolve_*_capacity / resolve_*_max_entries
//  function with identical precedence logic).

#include <chronon3d/cache/lru_cache.hpp>
#include <cstddef>
#include <optional>

namespace chronon3d { class CacheConfig; }

namespace chronon3d::cache {

/// Each cache instance belongs to exactly one domain.
/// Used by resolve_cache_policy() to look up defaults and Config overrides.
enum class CacheDomain {
    /// NodeCache — stores rendered node outputs (weighted by framebuffer bytes).
    Nodes,
    /// FrameCache — stores fully rendered composition frames.
    RenderedFrames,
    /// VideoFrameCache — stores encoded video frames (YUV/RGBA).
    VideoFrames,
    /// ConvertedFrameCache — stores already-converted encoder frames.
    ConvertedFrames,
    /// SceneProgramCache — stores compiled scene programs (count-limited).
    ScenePrograms,
    /// ImageCache — stores loaded/decoded images.
    Images,
    /// Glyph atlas cache — stores rasterized glyph textures.
    GlyphAtlas,
    /// Text cache — stores text-raster results (weighted by bytes).
    Text,
    /// Shadow cache — stores pre-rendered drop-shadow textures.
    Shadows,
    /// Glow cache — stores pre-rendered glow textures.
    Glow,
};

/// How capacity is measured for a domain.
enum class CacheCapacityUnit {
    /// Capacity is measured in bytes (weights passed to LruCache).
    Bytes,
    /// Capacity is measured in number of entries (LruCache Count mode).
    Entries,
};

/// Resolved configuration for one cache instance.
struct CachePolicy {
    CacheDomain       domain;
    CacheCapacityUnit unit;
    std::size_t       capacity{0};   ///< Resolved capacity (bytes or entries).
    std::size_t       shards{2};     ///< Recommended shard count.
    bool              enabled{true}; ///< Feature-flag: false disables the cache.
};

/// Resolve the canonical policy for `domain`.
///
/// Precedence:
///   1. override_capacity  (explicit ctor argument from the caller)
///   2. Config field       (env-var override, e.g. CHRONON3D_FRAME_CACHE_MAX_ENTRIES)
///   3. Hardcoded default  (defined in the TU-private defaults table)
///
/// When `override_capacity` is provided it silently ignores Config and the
/// hardcoded default; the caller is responsible for correctness.
///
/// This overload accepts an explicit CacheConfig for dependency injection.
/// Prefer this in testable / decoupled contexts.
[[nodiscard]] CachePolicy resolve_cache_policy(
    CacheDomain                    domain,
    std::optional<std::size_t>     override_capacity,
    const chronon3d::CacheConfig&  cache_config);

/// Convenience overload that reads from a globally-injected CacheConfig
/// (set once at startup by SoftwareRenderer).  Falls back to Config::get()
/// only when the global config hasn't been injected yet (e.g. tests).
[[nodiscard]] CachePolicy resolve_cache_policy(
    CacheDomain                    domain,
    std::optional<std::size_t>     override_capacity = std::nullopt);

/// Inject the global CacheConfig once at startup (called by SoftwareRenderer).
/// Thread-safe via std::call_once.  All subsequent resolve_cache_policy()
/// calls without an explicit CacheConfig will use this injected instance.
void set_global_cache_config(const chronon3d::CacheConfig& cache_config);

/// Convenience: map a domain's CacheCapacityUnit to LruCache's CapacityMode.
/// Used by cache facade constructors so the mode is derived from the
/// single source of truth rather than hardcoded.
[[nodiscard]] inline CapacityMode capacity_mode_for(CacheDomain domain) {
    return resolve_cache_policy(domain).unit == CacheCapacityUnit::Bytes
               ? CapacityMode::Weight
               : CapacityMode::Count;
}

} // namespace chronon3d::cache
