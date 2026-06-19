#include <chronon3d/core/config.hpp>
#include <cstdlib>
#include <charconv>
#include <string_view>
#include <spdlog/spdlog.h>

namespace chronon3d {

// ── Public static utilities ────────────────────────────────────────────────

std::size_t Config::resolve_env_mb(const char* env_name, std::size_t default_mb) {
    const auto fallback = default_mb * 1024ULL * 1024ULL;

    const char* env = std::getenv(env_name);
    if (!env || !*env) return fallback;

    std::string_view sv(env);
    std::size_t mb = 0;

    const auto* begin = sv.data();
    const auto* end = sv.data() + sv.size();
    auto [ptr, ec] = std::from_chars(begin, end, mb);

    if (ec != std::errc{} || ptr != end || mb == 0) {
        spdlog::warn(
            "Invalid environment value {}='{}'; using default {} MB",
            env_name, env, default_mb
        );
        return fallback;
    }

    return mb * 1024ULL * 1024ULL;
}

std::size_t Config::resolve_env_int(const char* env_name, std::size_t default_int) {
    const char* env = std::getenv(env_name);
    if (!env || !*env) return default_int;

    std::string_view sv(env);
    std::size_t value = 0;

    const auto* begin = sv.data();
    const auto* end = sv.data() + sv.size();
    auto [ptr, ec] = std::from_chars(begin, end, value);

    if (ec != std::errc{} || ptr != end || value == 0) {
        spdlog::warn(
            "Invalid environment value {}='{}'; using default {}",
            env_name, env, default_int
        );
        return default_int;
    }

    return value;
}

// ── Mutation pathway ───────────────────────────────────────────────────────

void Config::set_fb_pool_budget_override(std::size_t bytes) {
    // const_cast: the singleton is conceptually immutable to callers,
    // but the CLI pipeline budget is a valid runtime override.
    auto& instance = const_cast<Config&>(get());
    instance.cache_.fb_pool_budget_bytes_ = bytes;
}

// ── Private helpers ────────────────────────────────────────────────────────

static bool env_bool(const char* name) {
    const char* v = std::getenv(name);
    if (!v || !*v) return false;
    std::string_view sv(v);
    return sv != "0" && sv != "false" && sv != "off";
}

static std::string env_string(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string{};
}

// ── Constructor — populate all sub-configs from env vars ──────────────────

Config::Config() {
    // ═══════════════════════════════════════════════════════════════════
    //  DebugConfig
    // ═══════════════════════════════════════════════════════════════════
    debug_.glow_             = env_bool("CHRONON_DEBUG_GLOW");
    debug_.dump_alpha_mask_  = env_bool("CHRONON_DEBUG_DUMP_ALPHA_MASK");
    debug_.dump_text_raster_ = env_bool("CHRONON_DEBUG_DUMP_TEXT_RASTER");
    debug_.text_raster_      = env_bool("CHRONON_DEBUG_TEXT_RASTER");
    debug_.text_bbox_        = env_bool("CHRONON_DEBUG_TEXT_BBOX");

    // ═══════════════════════════════════════════════════════════════════
    //  SchedulerConfig
    // ═══════════════════════════════════════════════════════════════════
    // pingpong: default true. Only override if env var is explicitly set.
    {
        const char* pp_v = std::getenv("CHRONON_PINGPONG_FRAMEBUFFER");
        if (pp_v && *pp_v) {
            scheduler_.pingpong_framebuffer_ = env_bool("CHRONON_PINGPONG_FRAMEBUFFER");
        }
    }
    scheduler_.prefetch_enabled_ = env_bool("CHRONON_PREFETCH");
    scheduler_.pip_mode_         = env_bool("CHRONON_PIP_MODE");
    scheduler_.pin_main_thread_  = env_bool("CHRONON3D_PIN_MAIN_THREAD");

    // ═══════════════════════════════════════════════════════════════════
    //  CacheConfig — byte budgets
    // ═══════════════════════════════════════════════════════════════════
    cache_.fb_pool_max_bytes_    = resolve_env_mb("CHRONON_FB_POOL_MAX_MB", 0);
    cache_.fb_pool_budget_bytes_ = resolve_env_mb("CHRONON3D_FB_POOL_BUDGET_MB", 0);
    cache_.image_cache_max_bytes_ = resolve_env_mb("CHRONON_IMAGE_CACHE_MAX_MB", 0);
    cache_.node_cache_max_bytes_  = resolve_env_mb("CHRONON_NODE_CACHE_MAX_MB", 0);
    cache_.glyph_atlas_max_bytes_ = resolve_env_mb("CHRONON_GLYPH_ATLAS_MAX_MB", 0);
    cache_.text_cache_max_bytes_  = resolve_env_mb("CHRONON_TEXT_CACHE_MAX_MB", 128);
    cache_.shadow_cache_max_bytes_ = resolve_env_mb("CHRONON_SHADOW_CACHE_MAX_MB", 64);
    cache_.glow_cache_max_bytes_  = resolve_env_mb("CHRONON_GLOW_CACHE_MAX_MB", 64);

    // ═══════════════════════════════════════════════════════════════════
    //  CacheConfig — entry-count limits
    // ═══════════════════════════════════════════════════════════════════
    cache_.frame_cache_max_entries_           = resolve_env_int("CHRONON3D_FRAME_CACHE_MAX_ENTRIES", 0);
    cache_.video_frame_max_entries_           = resolve_env_int("CHRONON3D_VIDEO_FRAME_MAX_ENTRIES", 0);
    cache_.converted_frame_cache_max_entries_ = resolve_env_int("CHRONON3D_CONVERTED_FRAME_CACHE_MAX_ENTRIES", 0);
    cache_.scene_program_cache_max_entries_   = resolve_env_int("CHRONON3D_SCENE_PROGRAM_CACHE_MAX_ENTRIES", 0);

    // ═══════════════════════════════════════════════════════════════════
    //  CacheConfig — policy
    // ═══════════════════════════════════════════════════════════════════
    cache_.disable_persistent_framebuffer_cache_ = env_bool("CHRONON_DISABLE_PERSISTENT_FB_CACHE");

    // ═══════════════════════════════════════════════════════════════════
    //  PathConfig
    // ═══════════════════════════════════════════════════════════════════
    paths_.persistent_framebuffer_cache_dir_ = env_string("CHRONON_PERSISTENT_FB_CACHE_DIR");
}

} // namespace chronon3d
