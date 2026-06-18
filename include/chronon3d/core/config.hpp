#pragma once

// ── Engine configuration singleton ─────────────────────────────────
//
// Centralizes all environment-variable-driven configuration.
// Values are read once from env vars at first access (Meyer's singleton).
// Thread-safe: C++11 guarantees local static initialization is thread-safe.
//
// Usage:
//   auto& cfg = chronon3d::Config::get();
//   if (cfg.debug_glow) { ... }
//   size_t max_bytes = cfg.fb_pool_max_bytes;

#include <cstddef>
#include <cstdint>
#include <string>

namespace chronon3d {

class Config {
public:
    static Config& get() {
        static Config instance;
        return instance;
    }

    // ── Debug flags ──────────────────────────────────────────────
    bool debug_glow              = false;
    bool debug_dump_alpha_mask   = false;
    bool debug_dump_text_raster  = false;
    bool debug_text_raster       = false;
    bool debug_text_bbox         = false;

    // ── Feature flags ────────────────────────────────────────────
    bool pingpong_framebuffer    = true;
    bool prefetch_enabled        = true;
    bool pip_mode                = false;
    bool disable_disk_node_cache = false;
    bool pin_main_thread         = false;

    // ── Cache limits (bytes) ─────────────────────────────────────
    std::size_t fb_pool_max_bytes     = 0;
    std::size_t fb_pool_budget_bytes   = 0;   // 0 = use pool default; override via CHRONON3D_FB_POOL_BUDGET_MB
    std::size_t image_cache_max_bytes = 0;
    std::size_t node_cache_max_bytes  = 0;
    std::size_t glyph_atlas_max_bytes = 0;
    std::size_t text_cache_max_bytes  = 0;
    std::size_t shadow_cache_max_bytes = 0;
    std::size_t glow_cache_max_bytes  = 0;

    // ── Cache limits (entry count) ─────────────────────────────
    // LruBackedCache instances (FrameCache, VideoFrameCache) honor these
    // when constructed without an explicit cap.  0 = use the per-cache
    // hardcoded fallback inside the cache's constructor.
    std::size_t frame_cache_max_entries   = 0;   // CHRONON3D_FRAME_CACHE_MAX_ENTRIES
    std::size_t video_frame_max_entries   = 0;   // CHRONON3D_VIDEO_FRAME_MAX_ENTRIES

    // ── Paths ────────────────────────────────────────────────────
    std::string bake_cache_dir;
    std::string disk_cache_dir;

    // ── Utility ──────────────────────────────────────────────────

    /// Read an env var as size_t in MB, convert to bytes. Returns default_mb * 1MB on failure.
    static std::size_t resolve_env_mb(const char* env_name, std::size_t default_mb);

    /// Read an env var as size_t directly (no MB conversion). Returns default_int
    /// on missing / invalid input.  Used for entry-count caps on
    /// LruBackedCache instances where the unit is entries rather than bytes.
    static std::size_t resolve_env_int(const char* env_name, std::size_t default_int);

private:
    Config();
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
};

} // namespace chronon3d
