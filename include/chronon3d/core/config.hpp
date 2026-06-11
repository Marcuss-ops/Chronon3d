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
    std::size_t image_cache_max_bytes = 0;
    std::size_t node_cache_max_bytes  = 0;
    std::size_t glyph_atlas_max_bytes = 0;
    std::size_t text_cache_max_bytes  = 0;
    std::size_t shadow_cache_max_bytes = 0;
    std::size_t glow_cache_max_bytes  = 0;

    // ── Paths ────────────────────────────────────────────────────
    std::string bake_cache_dir;
    std::string disk_cache_dir;

    // ── Utility ──────────────────────────────────────────────────

    /// Read an env var as size_t in MB, convert to bytes. Returns default_mb * 1MB on failure.
    static std::size_t resolve_env_mb(const char* env_name, std::size_t default_mb);

private:
    Config();
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
};

} // namespace chronon3d
