// =============================================================================
// cache_policy.cpp — Centralized cache-domain policy resolver.
//
// All hardcoded capacity defaults and the Domain→Config→Env mapping live here.
// Cache facades call resolve_cache_policy() instead of rolling their own
// resolve_*_capacity / resolve_*_max_entries functions.
// =============================================================================

#include <chronon3d/cache/cache_policy.hpp>
#include <chronon3d/core/config.hpp>
#include <cstddef>

namespace chronon3d::cache {

// ── Defaults table ─────────────────────────────────────────────────────────

namespace {

struct PolicyDefaults {
    CacheDomain        domain;
    CacheCapacityUnit  unit;
    std::size_t        capacity;   // hardcoded fallback
    std::size_t        shards;
};

/// These values are used ONLY when neither the caller's explicit override
/// nor a Config/env variable supplies a value.  They must be conservative
/// (safe for all workloads) — tighter defaults come from env vars or benchmarks.
constexpr PolicyDefaults kDefaults[] = {
    // ── Byte-weighted domains ───────────────────────────────────────────
    { CacheDomain::Nodes,           CacheCapacityUnit::Bytes,   2048ULL * 1024ULL * 1024ULL, 2 },  // 2 GiB
    { CacheDomain::Images,          CacheCapacityUnit::Bytes,    512ULL * 1024ULL * 1024ULL, 2 },  // 512 MiB
    { CacheDomain::GlyphAtlas,      CacheCapacityUnit::Bytes,     64ULL * 1024ULL * 1024ULL, 2 },  // 64 MiB
    { CacheDomain::Text,            CacheCapacityUnit::Bytes,     32ULL * 1024ULL * 1024ULL, 2 },  // 32 MiB
    { CacheDomain::Shadows,         CacheCapacityUnit::Bytes,     64ULL * 1024ULL * 1024ULL, 2 },  // 64 MiB
    { CacheDomain::Glow,            CacheCapacityUnit::Bytes,     64ULL * 1024ULL * 1024ULL, 2 },  // 64 MiB

    // ── Count-limited domains ───────────────────────────────────────────
    // These use CapacityMode::Count in their LruCache; every entry weighs 1.
    { CacheDomain::RenderedFrames,  CacheCapacityUnit::Entries,  256, 2 },
    { CacheDomain::VideoFrames,     CacheCapacityUnit::Entries,   64, 2 },
    { CacheDomain::ConvertedFrames, CacheCapacityUnit::Entries,    8, 2 },
    { CacheDomain::ScenePrograms,   CacheCapacityUnit::Entries,    8, 2 },
};

/// Look up the hardcoded default for a domain.
/// Returns nullptr if the domain has no entry (shouldn't happen).
const PolicyDefaults* find_defaults(CacheDomain domain) {
    for (const auto& d : kDefaults) {
        if (d.domain == domain) return &d;
    }
    return nullptr;
}

/// Return the Config field value for a domain, or 0 if the domain is
/// not configured via Config (uses the hardcoded default).
std::size_t config_value(CacheDomain domain) {
    const auto& cfg = chronon3d::Config::get();
    switch (domain) {
        case CacheDomain::Nodes:           return cfg.node_cache_max_bytes;
        case CacheDomain::RenderedFrames:  return cfg.frame_cache_max_entries;
        case CacheDomain::VideoFrames:     return cfg.video_frame_max_entries;
        case CacheDomain::ConvertedFrames: return cfg.converted_frame_cache_max_entries;
        case CacheDomain::ScenePrograms:   return cfg.scene_program_cache_max_entries;
        case CacheDomain::Images:          return cfg.image_cache_max_bytes;
        case CacheDomain::GlyphAtlas:      return cfg.glyph_atlas_max_bytes;
        case CacheDomain::Text:            return cfg.text_cache_max_bytes;
        case CacheDomain::Shadows:         return cfg.shadow_cache_max_bytes;
        case CacheDomain::Glow:            return cfg.glow_cache_max_bytes;
        // No default case — compiler warns if an enum value is missing.
    }
    return 0;
}

} // namespace

// ── Public resolver ────────────────────────────────────────────────────────

CachePolicy resolve_cache_policy(
    CacheDomain                domain,
    std::optional<std::size_t> override_capacity)
{
    const auto* def = find_defaults(domain);

    CachePolicy policy{};
    policy.domain   = domain;
    policy.unit     = def ? def->unit     : CacheCapacityUnit::Entries;
    policy.shards   = def ? def->shards   : 2;
    policy.enabled  = true;

    // Precedence: 1. explicit override  2. Config  3. hardcoded default
    if (override_capacity.has_value() && *override_capacity > 0) {
        policy.capacity = *override_capacity;
    } else {
        const std::size_t cfg_val = config_value(domain);
        policy.capacity = (cfg_val > 0) ? cfg_val
                                        : (def ? def->capacity : 0);
    }

    return policy;
}

} // namespace chronon3d::cache
