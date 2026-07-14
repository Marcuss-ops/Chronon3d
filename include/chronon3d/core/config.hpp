#pragma once

// ── Engine configuration — domain-split, per-instance ───────────────
//
// Config is constructed from environment variables via the factory
// `Config::from_environment()`.  Domain-specific sub-configs (DebugConfig,
// CacheConfig, SchedulerConfig, PathConfig) are exposed via const accessors
// so callers can consume only the section they need.
//
// The old singleton pattern (`Config::get()`) is deprecated.  Callers should
// construct a Config instance via `from_environment()` and pass it explicitly
// (e.g. stored in SoftwareRenderer, threaded through RenderGraphContext).
//
// Thread-safe construction: the factory reads env vars once at call time.
//
// Usage:
//   // New (per-instance):
//   Config cfg = Config::from_environment();
//   if (cfg.debug().glow()) { ... }
//   cfg.set_fb_pool_budget(512 * 1024 * 1024);
//
//   // Deprecated (singleton — still works, emits warning):
//   const auto& cfg = chronon3d::Config::get();
//   if (cfg.debug().glow()) { ... }

#include <cstddef>
#include <cstdint>
#include <string>

#include <chronon3d/cache/framebuffer_pool.hpp>  // P1-21: FramebufferPoolClearPolicy
#include <chronon3d/core/scheduler/scheduler_mode.hpp>
#include <chronon3d/core/cpu_budget.hpp>

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
    [[nodiscard]] std::size_t converted_frame_cache_max_bytes() const noexcept { return converted_frame_cache_max_bytes_; }
    [[nodiscard]] std::size_t scene_program_cache_max_entries()   const noexcept { return scene_program_cache_max_entries_; }

    // ── Policy ───────────────────────────────────────────────────────
    [[nodiscard]] bool disable_persistent_framebuffer_cache() const noexcept {
        return disable_persistent_framebuffer_cache_;
    }

    /// P1-21: explicit post-job clear policy for the framebuffer pool.
    /// Replaces the legacy implicit-clear-after-job pattern.  Default is
    /// `TrimAfterJob` (matches pre-P1-21 production behavior: pipe_export_loop
    /// unconditionally called clear() at the end of every job).
    [[nodiscard]] chronon3d::cache::FramebufferPoolClearPolicy
    framebuffer_pool_clear_policy() const noexcept {
        return framebuffer_pool_clear_policy_;
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
    std::size_t converted_frame_cache_max_bytes_ = 0;
    std::size_t scene_program_cache_max_entries_   = 0;

    // policy
    bool disable_persistent_framebuffer_cache_ = false;
    chronon3d::cache::FramebufferPoolClearPolicy
        framebuffer_pool_clear_policy_{
            chronon3d::cache::FramebufferPoolClearPolicy::TrimAfterJob};
};

// ═════════════════════════════════════════════════════════════════════════
//  SchedulerConfig — scheduler / feature flags
// ═════════════════════════════════════════════════════════════════════════

class SchedulerConfig {
public:
    [[nodiscard]] bool pingpong_framebuffer()  const noexcept { return pingpong_framebuffer_; }
    [[nodiscard]] bool prefetch_enabled()      const noexcept { return prefetch_enabled_; }
    [[nodiscard]] bool pip_mode()              const noexcept { return pip_mode_; }

    // ── PR-B: scheduler mode / worker pool tuning (audit §9.3) ───────
    // Default behaviour is preserved: TbbFixed with worker_count = 0
    // (resolved at ExecutionScheduler construction as hardware_concurrency).
    [[nodiscard]] SchedulerMode mode()         const noexcept { return mode_; }
    [[nodiscard]] int worker_count()           const noexcept { return worker_count_; }
    [[nodiscard]] bool pin_calling_thread()    const noexcept { return pin_calling_thread_; }

    /// Backwards-compat alias for `pin_calling_thread`.  The legacy name
    /// `pin_main_thread` survives as a getter to keep any third-party
    /// caller that referenced the field name before the rename compiling.
    [[nodiscard]] bool pin_main_thread()       const noexcept { return pin_calling_thread_; }

private:
    friend class Config;
    bool pingpong_framebuffer_ = true;
    bool prefetch_enabled_     = true;
    bool pip_mode_             = false;

    // ── PR-B state ─────────────────────────────────────────────────────
    SchedulerMode mode_               {SchedulerMode::TbbFixed};
    int           worker_count_       {0};
    bool          pin_calling_thread_ {false};
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
    /// Factory: construct a Config by reading environment variables.
    /// This is the preferred creation path for per-instance configuration.
    [[nodiscard]] static Config from_environment();

    /// Factory: construct a Config using a pre-built CpuBudget.
    /// This guarantees the same immutable budget is shared by the
    /// scheduler, decoder, encoder and writer pipeline.
    [[nodiscard]] static Config from_environment(const CpuBudget& budget);

    /// Set the framebuffer pool budget on this instance (non-static).
    void set_fb_pool_budget(std::size_t bytes);

    /// P1-21: set the framebuffer pool clear policy on this instance
    /// (non-static).  Overrides the env-resolved default
    /// (CHRONON3D_FB_POOL_CLEAR_POLICY).  The CLI flag
    /// `--fb-pool-clear-policy` calls this on the per-job Config.
    void set_fb_pool_clear_policy(chronon3d::cache::FramebufferPoolClearPolicy policy);

    /// Inject the CPU budget so the same immutable instance is used by
    /// the scheduler, decoder, encoder and writer pipeline.
    void set_cpu_budget(const CpuBudget& budget) { cpu_budget_ = budget; }

    // ── Domain accessors ──────────────────────────────────────────────

    [[nodiscard]] const DebugConfig&     debug()     const noexcept { return debug_; }
    [[nodiscard]] const CacheConfig&     cache()     const noexcept { return cache_; }
    [[nodiscard]] const SchedulerConfig& scheduler() const noexcept { return scheduler_; }
    [[nodiscard]] const PathConfig&      paths()     const noexcept { return paths_; }
    [[nodiscard]] const CpuBudget&       cpu_budget() const noexcept { return cpu_budget_; }

    // ── Utility (kept public, unchanged) ──────────────────────────────

    /// Read an env var as size_t in MB, convert to bytes. Returns default_mb * 1MB on failure.
    [[nodiscard]] static std::size_t resolve_env_mb(const char* env_name, std::size_t default_mb);

    /// Read an env var as size_t directly (no MB conversion). Returns default_int
    /// on missing / invalid input.
    [[nodiscard]] static std::size_t resolve_env_int(const char* env_name, std::size_t default_int);

    Config();
    Config(Config&&) noexcept = default;
    Config& operator=(Config&&) noexcept = default;
    Config(const Config&) = default;
    Config& operator=(const Config&) = default;

private:
    DebugConfig     debug_;
    CacheConfig     cache_;
    SchedulerConfig scheduler_;
    PathConfig      paths_;
    CpuBudget       cpu_budget_;
};

} // namespace chronon3d

// ── TICKET-007 — Per-instance debug config propagation ──────────────────
//
// The legacy process-wide pointer `detail::g_debug_config` and its
// setter `detail::set_debug_config()` have been removed.  DebugConfig
// is now propagated per-instance through
// `RenderGraphContext::options::debug_config`, populated by the
// `SoftwareRenderer`-owning code path at the single construction
// site `src/render_graph/pipeline/scene.cpp`.
// See `docs/FOLLOWUP_TICKETS.md` TICKET-007 for the full migration.
