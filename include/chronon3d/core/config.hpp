#pragma once

// ── Engine configuration — immutable, domain-split ───────────────────
//
// Config is constructed once from environment variables at first access
// (Meyer's singleton) and remains immutable thereafter.  Domain-specific
// sub-configs (DebugConfig, CacheConfig, SchedulerConfig, PathConfig)
// are exposed via const accessors so callers can consume only the section
// they need.
//
// The singleton returns a const reference — the only mutation pathway is
// set_fb_pool_budget_override() for the CLI pipeline override.
//
// Thread-safe: C++11 guarantees local static initialization is thread-safe.
//
// Usage:
//   const auto& cfg = chronon3d::Config::get();
//   if (cfg.debug().glow()) { ... }
//   size_t max_bytes = cfg.cache().node_cache_max_bytes();

#include <cstddef>
#include <cstdint>
#include <string>

namespace chronon3d {

// ═════════════════════════════════════════════════════════════════════════
//  DebugConfig — debug/diagnostic toggles
// ═════════════════════════════════════════════════════════════════════════

class DebugConfig {
public:
    [[nodiscard]] bool glow()              const noexcept { return glow_; }
    [[nodiscard]] bool dump_alpha_mask()   const noexcept { return dump_alpha_mask_; }
    [[nodiscard]] bool dump_text_raster()  const noexcept { return dump_text_raster_; }
    [[nodiscard]] bool text_raster()       const noexcept { return text_raster_; }
    [[nodiscard]] bool text_bbox()         const noexcept { return text_bbox_; }

private:
    friend class Config;
    bool glow_              = false;
    bool dump_alpha_mask_   = false;
    bool dump_text_raster_  = false;
    bool text_raster_       = false;
    bool text_bbox_         = false;
};

// ═════════════════════════════════════════════════════════════════════════
//  CacheConfig — cache capacity budgets (byte & entry-count)
// ═════════════════════════════════════════════════════════════════════════

class CacheConfig {
public:
    // ── Byte budgets ──────────────────────────────────────────────────
    [[nodiscard]] std::size_t fb_pool_max_bytes()   const noexcept { return fb_pool_max_bytes_; }
    [[nodiscard]] std::size_t fb_pool_budget_bytes() const noexcept { return fb_pool_budget_bytes_; }
    [[nodiscard]] std::size_t image_cache_max_bytes() const noexcept { return image_cache_max_bytes_; }
    [[nodiscard]] std::size_t node_cache_max_bytes()  const noexcept { return node_cache_max_bytes_; }
    [[nodiscard]] std::size_t glyph_atlas_max_bytes() const noexcept { return glyph_atlas_max_bytes_; }
    [[nodiscard]] std::size_t text_cache_max_bytes()  const noexcept { return text_cache_max_bytes_; }
    [[nodiscard]] std::size_t shadow_cache_max_bytes() const noexcept { return shadow_cache_max_bytes_; }
    [[nodiscard]] std::size_t glow_cache_max_bytes()  const noexcept { return glow_cache_max_bytes_; }

    // ── Entry-count limits ────────────────────────────────────────────
    [[nodiscard]] std::size_t frame_cache_max_entries()           const noexcept { return frame_cache_max_entries_; }
    [[nodiscard]] std::size_t video_frame_max_entries()           const noexcept { return video_frame_max_entries_; }
    [[nodiscard]] std::size_t converted_frame_cache_max_entries() const noexcept { return converted_frame_cache_max_entries_; }
    [[nodiscard]] std::size_t scene_program_cache_max_entries()   const noexcept { return scene_program_cache_max_entries_; }

    // ── Policy ───────────────────────────────────────────────────────
    [[nodiscard]] bool disable_persistent_framebuffer_cache() const noexcept {
        return disable_persistent_framebuffer_cache_;
    }

private:
    friend class Config;

    // byte budgets (0 = use hardcoded default)
    std::size_t fb_pool_max_bytes_    = 0;
    std::size_t fb_pool_budget_bytes_  = 0;
    std::size_t image_cache_max_bytes_ = 0;
    std::size_t node_cache_max_bytes_  = 0;
    std::size_t glyph_atlas_max_bytes_ = 0;
    std::size_t text_cache_max_bytes_  = 0;
    std::size_t shadow_cache_max_bytes_ = 0;
    std::size_t glow_cache_max_bytes_  = 0;

    // entry-count limits (0 = use hardcoded default)
    std::size_t frame_cache_max_entries_           = 0;
    std::size_t video_frame_max_entries_           = 0;
    std::size_t converted_frame_cache_max_entries_ = 0;
    std::size_t scene_program_cache_max_entries_   = 0;

    // policy
    bool disable_persistent_framebuffer_cache_ = false;
};

// ═════════════════════════════════════════════════════════════════════════
//  SchedulerConfig — scheduler / feature flags
// ═════════════════════════════════════════════════════════════════════════

class SchedulerConfig {
public:
    [[nodiscard]] bool pingpong_framebuffer()  const noexcept { return pingpong_framebuffer_; }
    [[nodiscard]] bool prefetch_enabled()      const noexcept { return prefetch_enabled_; }
    [[nodiscard]] bool pip_mode()              const noexcept { return pip_mode_; }
    [[nodiscard]] bool pin_main_thread()       const noexcept { return pin_main_thread_; }

private:
    friend class Config;
    bool pingpong_framebuffer_ = true;
    bool prefetch_enabled_     = true;
    bool pip_mode_             = false;
    bool pin_main_thread_      = false;
};

// ═════════════════════════════════════════════════════════════════════════
//  PathConfig — filesystem path overrides
// ═════════════════════════════════════════════════════════════════════════

class PathConfig {
public:
    [[nodiscard]] const std::string& persistent_framebuffer_cache_dir() const noexcept {
        return persistent_framebuffer_cache_dir_;
    }

private:
    friend class Config;
    std::string persistent_framebuffer_cache_dir_;
};

// ═════════════════════════════════════════════════════════════════════════
//  Config — immutable domain-split configuration singleton
// ═════════════════════════════════════════════════════════════════════════

class Config {
public:
    /// Returns the singleton (immutable — const reference).
    [[nodiscard]] static const Config& get() {
        static Config instance;
        return instance;
    }

    /// CLI pipeline override: sets the framebuffer pool budget in bytes.
    /// This is the single mutation pathway; all other fields are read-only
    /// after construction.
    static void set_fb_pool_budget_override(std::size_t bytes);

    // ── Domain accessors ──────────────────────────────────────────────

    [[nodiscard]] const DebugConfig&     debug()     const noexcept { return debug_; }
    [[nodiscard]] const CacheConfig&     cache()     const noexcept { return cache_; }
    [[nodiscard]] const SchedulerConfig& scheduler() const noexcept { return scheduler_; }
    [[nodiscard]] const PathConfig&      paths()     const noexcept { return paths_; }

    // ── Utility (kept public, unchanged) ──────────────────────────────

    /// Read an env var as size_t in MB, convert to bytes. Returns default_mb * 1MB on failure.
    [[nodiscard]] static std::size_t resolve_env_mb(const char* env_name, std::size_t default_mb);

    /// Read an env var as size_t directly (no MB conversion). Returns default_int
    /// on missing / invalid input.
    [[nodiscard]] static std::size_t resolve_env_int(const char* env_name, std::size_t default_int);

private:
    Config();
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    DebugConfig     debug_;
    CacheConfig     cache_;
    SchedulerConfig scheduler_;
    PathConfig      paths_;
};

} // namespace chronon3d
