#include <chronon3d/core/config.hpp>
#include <cstdlib>
#include <charconv>
#include <string_view>
#include <spdlog/spdlog.h>

namespace chronon3d {

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

Config::Config() {
    // Debug flags
    debug_glow             = env_bool("CHRONON_DEBUG_GLOW");
    debug_dump_alpha_mask  = env_bool("CHRONON_DEBUG_DUMP_ALPHA_MASK");
    debug_dump_text_raster = env_bool("CHRONON_DEBUG_DUMP_TEXT_RASTER");
    debug_text_raster      = env_bool("CHRONON_DEBUG_TEXT_RASTER");
    debug_text_bbox        = env_bool("CHRONON_DEBUG_TEXT_BBOX");

    // Feature flags
    // pingpong: default true (in-class default). Only override if env var is explicitly set.
    {
        const char* pp_v = std::getenv("CHRONON_PINGPONG_FRAMEBUFFER");
        if (pp_v && *pp_v) pingpong_framebuffer = env_bool("CHRONON_PINGPONG_FRAMEBUFFER");
    }
    prefetch_enabled                           = env_bool("CHRONON_PREFETCH");
    pip_mode                                   = env_bool("CHRONON_PIP_MODE");
    disable_persistent_framebuffer_cache       = env_bool("CHRONON_DISABLE_PERSISTENT_FB_CACHE");
    pin_main_thread                            = env_bool("CHRONON3D_PIN_MAIN_THREAD");

    // Cache limits
    fb_pool_max_bytes      = resolve_env_mb("CHRONON_FB_POOL_MAX_MB", 0);
    fb_pool_budget_bytes   = resolve_env_mb("CHRONON3D_FB_POOL_BUDGET_MB", 0);
    image_cache_max_bytes  = resolve_env_mb("CHRONON_IMAGE_CACHE_MAX_MB", 0);
    node_cache_max_bytes   = resolve_env_mb("CHRONON_NODE_CACHE_MAX_MB", 0);
    glyph_atlas_max_bytes = resolve_env_mb("CHRONON_GLYPH_ATLAS_MAX_MB", 0);
    text_cache_max_bytes   = resolve_env_mb("CHRONON_TEXT_CACHE_MAX_MB", 128);
    shadow_cache_max_bytes = resolve_env_mb("CHRONON_SHADOW_CACHE_MAX_MB", 64);
    glow_cache_max_bytes   = resolve_env_mb("CHRONON_GLOW_CACHE_MAX_MB", 64);

    // Per-cache entry-count caps for LruBackedCache instances.
    frame_cache_max_entries           = resolve_env_int("CHRONON3D_FRAME_CACHE_MAX_ENTRIES", 0);
    video_frame_max_entries           = resolve_env_int("CHRONON3D_VIDEO_FRAME_MAX_ENTRIES", 0);
    converted_frame_cache_max_entries = resolve_env_int("CHRONON3D_CONVERTED_FRAME_CACHE_MAX_ENTRIES", 0);
    scene_program_cache_max_entries   = resolve_env_int("CHRONON3D_SCENE_PROGRAM_CACHE_MAX_ENTRIES", 0);

    // Paths — persistent framebuffer store (replaces bake + disk cache dirs).
    persistent_framebuffer_cache_dir = env_string("CHRONON_PERSISTENT_FB_CACHE_DIR");
}

} // namespace chronon3d
