#include "software_text_processor_internal.hpp"
#include <cstdlib>

namespace chronon3d::renderer {

size_t resolve_cache_max_mb(const char* env_name, size_t default_mb) {
    const char* env = std::getenv(env_name);
    if (!env || !*env) return default_mb * 1024ULL * 1024ULL;
    try {
        size_t mb = static_cast<size_t>(std::stoull(env));
        return mb > 0 ? mb * 1024ULL * 1024ULL : default_mb * 1024ULL * 1024ULL;
    } catch (...) {
        return default_mb * 1024ULL * 1024ULL;
    }
}

ShadowCache& get_shadow_cache() {
    static ShadowCache cache(resolve_cache_max_mb("CHRONON_SHADOW_CACHE_MAX_MB", 64), 4);
    return cache;
}

ShadowCache& get_glow_cache() {
    static ShadowCache cache(resolve_cache_max_mb("CHRONON_GLOW_CACHE_MAX_MB", 64), 4);
    return cache;
}

std::mutex g_text_glow_cache_mutex;
std::mutex g_text_shadow_cache_mutex;

void clear_text_glow_cache() {
    std::lock_guard<std::mutex> lock(g_text_glow_cache_mutex);
    get_glow_cache().clear();
}

void clear_text_shadow_cache() {
    std::lock_guard<std::mutex> lock(g_text_shadow_cache_mutex);
    get_shadow_cache().clear();
}

} // namespace chronon3d::renderer
