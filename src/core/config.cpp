#include <chronon3d/core/config.hpp>
#include <cstdlib>
#include <string_view>

namespace chronon3d {

std::size_t Config::resolve_env_mb(const char* env_name, std::size_t default_mb) {
    const char* env = std::getenv(env_name);
    if (!env || !*env) return default_mb * 1024ULL * 1024ULL;
    try {
        std::size_t mb = static_cast<std::size_t>(std::stoull(env));
        return mb > 0 ? mb * 1024ULL * 1024ULL : default_mb * 1024ULL * 1024ULL;
    } catch (...) {
        return default_mb * 1024ULL * 1024ULL;
    }
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
    pingpong_framebuffer    = env_bool("CHRONON_PINGPONG_FRAMEBUFFER");
    prefetch_enabled        = env_bool("CHRONON_PREFETCH");
    pip_mode                = env_bool("CHRONON_PIP_MODE");
    disable_disk_node_cache = env_bool("CHRONON_DISABLE_DISK_NODE_CACHE");
    pin_main_thread         = env_bool("CHRONON3D_PIN_MAIN_THREAD");

    // Cache limits
    fb_pool_max_bytes      = resolve_env_mb("CHRONON_FB_POOL_MAX_MB", 0);
    image_cache_max_bytes  = resolve_env_mb("CHRONON_IMAGE_CACHE_MAX_MB", 0);
    node_cache_max_bytes   = resolve_env_mb("CHRONON_NODE_CACHE_MAX_MB", 0);
    glyph_atlas_max_bytes = resolve_env_mb("CHRONON_GLYPH_ATLAS_MAX_MB", 0);
    text_cache_max_bytes   = resolve_env_mb("CHRONON_TEXT_CACHE_MAX_MB", 128);
    shadow_cache_max_bytes = resolve_env_mb("CHRONON_SHADOW_CACHE_MAX_MB", 64);
    glow_cache_max_bytes   = resolve_env_mb("CHRONON_GLOW_CACHE_MAX_MB", 64);

    // Paths
    bake_cache_dir = env_string("CHRONON_BAKE_CACHE_DIR");
    disk_cache_dir = env_string("CHRONON_DISK_CACHE_DIR");
}

} // namespace chronon3d
