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
    // SceneProgramCache stays count-limited; the others moved to byte-weighted
    // in PR 3 (byte budgets).
    { CacheDomain::RenderedFrames,  CacheCapacityUnit::Bytes,   512ULL * 1024ULL * 1024ULL, 2 },  // 512 MiB
    { CacheDomain::VideoFrames,     CacheCapacityUnit::Bytes,   256ULL * 1024ULL * 1024ULL, 2 },  // 256 MiB
    { CacheDomain::ConvertedFrames, CacheCapacityUnit::Bytes,   128ULL * 1024ULL * 1024ULL, 2 },  // 128 MiB
    { CacheDomain::ScenePrograms,   CacheCapacityUnit::Entries,   8,                             2 },
};

/// Look up the hardcoded default for a domain.
/// Returns nullptr if the domain has no entry (shouldn't happen).
const PolicyDefaults* find_defaults(CacheDomain domain) {
    for (const auto& d : kDefaults) {
        if (d.domain == domain) return &d;
    }
    return nullptr;
}

/// Return the CacheConfig field value for a domain, or 0 if the domain is
/// not configured via CacheConfig (uses the hardcoded default).
static std::size_t config_value_from(CacheDomain domain,
                                      const chronon3d::CacheConfig& cache_cfg) {
    switch (domain) {
        case CacheDomain::Nodes:           return cache_cfg.node_cache_max_bytes();
        case CacheDomain::RenderedFrames:  return cache_cfg.frame_cache_max_entries();
        case CacheDomain::VideoFrames:     return cache_cfg.video_frame_max_entries();
        case CacheDomain::ConvertedFrames: return cache_cfg.converted_frame_cache_max_entries();
        case CacheDomain::ScenePrograms:   return cache_cfg.scene_program_cache_max_entries();
        case CacheDomain::Images:          return cache_cfg.image_cache_max_bytes();
        case CacheDomain::GlyphAtlas:      return cache_cfg.glyph_atlas_max_bytes();
        case CacheDomain::Text:            return cache_cfg.text_cache_max_bytes();
        case CacheDomain::Shadows:         return cache_cfg.shadow_cache_max_bytes();
        case CacheDomain::Glow:            return cache_cfg.glow_cache_max_bytes();
    }
    return 0;
}

static std::size_t config_value(CacheDomain domain) {
    return config_value_from(domain, chronon3d::Config::get().cache());
}

} // namespace

// ── Public resolver ────────────────────────────────────────────────────────

CachePolicy resolve_cache_policy(
    CacheDomain                    domain,
    std::optional<std::size_t>     override_capacity,
    const chronon3d::CacheConfig&  cache_config)
{
    const auto* def = find_defaults(domain);

    CachePolicy policy{};
    policy.domain   = domain;
    policy.unit     = def ? def->unit     : CacheCapacityUnit::Entries;
    policy.shards   = def ? def->shards   : 2;
    policy.enabled  = true;

    // Precedence: 1. explicit override  2. CacheConfig  3. hardcoded default
    if (override_capacity.has_value() && *override_capacity > 0) {
        policy.capacity = *override_capacity;
    } else {
        const std::size_t cfg_val = config_value_from(domain, cache_config);
        policy.capacity = (cfg_val > 0) ? cfg_val
                                        : (def ? def->capacity : 0);
    }

    return policy;
}

CachePolicy resolve_cache_policy(
    CacheDomain                domain,
    std::optional<std::size_t> override_capacity)
{
    return resolve_cache_policy(domain, override_capacity,
                                chronon3d::Config::get().cache());
}

} // namespace chronon3d::cache
