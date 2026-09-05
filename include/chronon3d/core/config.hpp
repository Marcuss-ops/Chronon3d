#pragma once

// ── Engine configuration — domain-split, per-instance ───────────────
//
// Config is constructed from environment variables via the factory
// `Config::from_environment()`.  Domain-specific sub-configs (DebugConfig,
// CacheConfig, SchedulerConfig) are exposed via const accessors so callers
// can consume only the section they need.
//
// The old singleton pattern (`Config::get()`) is deprecated.  Callers should
// construct a Config instance via `from_environment()` and pass it explicitly
// (e.g. stored in SoftwareRenderer, threaded through RenderGraphContext).
//
// Thread-safe construction: the factory reads env vars once at call time.
//
// Usage:
//   Config cfg = Config::from_environment();
//   if (cfg.debug().glow()) { ... }
//   cfg.set_fb_pool_budget(512 * 1024 * 1024);

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/scheduler/scheduler_mode.hpp>
#include <chronon3d/core/cpu_budget.hpp>
#include <chronon3d/core/gpu_hot_path_mode.hpp>
#include <chronon3d/render_graph/backend_selection.hpp>

namespace chronon3d {

class DebugConfig {
public:
    [[nodiscard]] bool glow()             const noexcept { return glow_; }
    [[nodiscard]] bool text_clip_debug() const noexcept { return text_clip_debug_; }
    [[nodiscard]] bool proj_diag()       const noexcept { return proj_diag_; }
    [[nodiscard]] bool dump_alpha_mask() const noexcept { return dump_alpha_mask_; }
    [[nodiscard]] bool dump_text_raster() const noexcept { return dump_text_raster_; }
    [[nodiscard]] bool text_raster()     const noexcept { return text_raster_; }
    [[nodiscard]] bool text_bbox()       const noexcept { return text_bbox_; }

private:
    friend class Config;
    bool glow_ = false;
    bool dump_alpha_mask_ = false;
    bool dump_text_raster_ = false;
    bool text_raster_ = false;
    bool text_bbox_ = false;
    bool text_clip_debug_ = false;
    bool proj_diag_ = false;
};

class CacheConfig {
public:
    [[nodiscard]] std::size_t fb_pool_max_bytes() const noexcept { return fb_pool_max_bytes_; }
    [[nodiscard]] std::size_t fb_pool_budget_bytes() const noexcept { return fb_pool_budget_bytes_; }
    [[nodiscard]] std::size_t image_cache_max_bytes() const noexcept { return image_cache_max_bytes_; }
    [[nodiscard]] std::size_t node_cache_max_bytes() const noexcept { return node_cache_max_bytes_; }
    [[nodiscard]] std::size_t glyph_atlas_max_bytes() const noexcept { return glyph_atlas_max_bytes_; }
    [[nodiscard]] std::size_t text_cache_max_bytes() const noexcept { return text_cache_max_bytes_; }
    [[nodiscard]] std::size_t shadow_cache_max_bytes() const noexcept { return shadow_cache_max_bytes_; }
    [[nodiscard]] std::size_t glow_cache_max_bytes() const noexcept { return glow_cache_max_bytes_; }
    [[nodiscard]] std::size_t frame_cache_max_entries() const noexcept { return frame_cache_max_entries_; }
    [[nodiscard]] std::size_t video_frame_max_entries() const noexcept { return video_frame_max_entries_; }
    [[nodiscard]] std::size_t converted_frame_cache_max_bytes() const noexcept { return converted_frame_cache_max_bytes_; }
    [[nodiscard]] std::size_t scene_program_cache_max_entries() const noexcept { return scene_program_cache_max_entries_; }
    [[nodiscard]] chronon3d::cache::FramebufferPoolClearPolicy
    framebuffer_pool_clear_policy() const noexcept {
        return framebuffer_pool_clear_policy_;
    }

private:
    friend class Config;
    std::size_t fb_pool_max_bytes_ = 0;
    std::size_t fb_pool_budget_bytes_ = 0;
    std::size_t image_cache_max_bytes_ = 0;
    std::size_t node_cache_max_bytes_ = 0;
    std::size_t glyph_atlas_max_bytes_ = 0;
    std::size_t text_cache_max_bytes_ = 0;
    std::size_t shadow_cache_max_bytes_ = 0;
    std::size_t glow_cache_max_bytes_ = 0;
    std::size_t frame_cache_max_entries_ = 0;
    std::size_t video_frame_max_entries_ = 0;
    std::size_t converted_frame_cache_max_bytes_ = 0;
    std::size_t scene_program_cache_max_entries_ = 0;
    chronon3d::cache::FramebufferPoolClearPolicy framebuffer_pool_clear_policy_{
        chronon3d::cache::FramebufferPoolClearPolicy::TrimAfterJob};
};

class SchedulerConfig {
public:
    [[nodiscard]] bool pingpong_framebuffer() const noexcept { return pingpong_framebuffer_; }
    [[nodiscard]] bool prefetch_enabled() const noexcept { return prefetch_enabled_; }
    [[nodiscard]] bool pip_mode() const noexcept { return pip_mode_; }
    [[nodiscard]] SchedulerMode mode() const noexcept { return mode_; }
    [[nodiscard]] int worker_count() const noexcept { return worker_count_; }
    [[nodiscard]] bool pin_calling_thread() const noexcept { return pin_calling_thread_; }
    [[nodiscard]] bool pin_main_thread() const noexcept { return pin_calling_thread_; }

private:
    friend class Config;
    bool pingpong_framebuffer_ = true;
    bool prefetch_enabled_ = true;
    bool pip_mode_ = false;
    SchedulerMode mode_{SchedulerMode::TbbFixed};
    int worker_count_{0};
    bool pin_calling_thread_{false};
};

// Process-boundary values that are allowed to originate from environment
// variables. Runtime subsystems receive these immutable strings and must not
// call getenv() again. telemetry_path() remains the explicit override; the
// default directory is separate so callers can distinguish override vs fallback.
class RuntimePathConfig {
public:
    [[nodiscard]] const std::string& assets_root() const noexcept { return assets_root_; }
    [[nodiscard]] const std::string& telemetry_path() const noexcept { return telemetry_path_; }
    [[nodiscard]] const std::string& telemetry_default_directory() const noexcept {
        return telemetry_default_directory_;
    }
    [[nodiscard]] const std::string& telemetry_run_id() const noexcept {
        return telemetry_run_id_;
    }
    [[nodiscard]] const std::string& telemetry_level() const noexcept { return telemetry_level_; }
    [[nodiscard]] const std::string& telemetry_detailed_ttl_days() const noexcept {
        return telemetry_detailed_ttl_days_;
    }
    [[nodiscard]] const std::string& cli_assets_root() const noexcept { return cli_assets_root_; }

private:
    friend class Config;
    std::string assets_root_;
    std::string telemetry_path_;
    std::string telemetry_default_directory_;
    std::string telemetry_run_id_;
    // Boundary-resolved capture level + Detailed/Trace retention window
    // (parsed to TelemetryLevel / days at the CLI startup boundary, never by
    // telemetry runtime code). Defaults mirror TelemetryRuntimeConfig.
    std::string telemetry_level_{std::string{"summary"}};
    std::string telemetry_detailed_ttl_days_{std::string{"30"}};
    std::string cli_assets_root_;
};

class Config {
public:
    static constexpr std::uint32_t kAutoGpuDevice = UINT32_MAX;
    [[nodiscard]] static Config from_environment();
    [[nodiscard]] static Config from_environment(const CpuBudget& budget);

    void set_fb_pool_budget(std::size_t bytes);
    void set_fb_pool_clear_policy(chronon3d::cache::FramebufferPoolClearPolicy policy);
    void set_cpu_budget(const CpuBudget& budget) { cpu_budget_ = budget; }
    void set_backend_preference(chronon3d::graph::BackendPreference preference) noexcept {
        backend_preference_ = preference;
    }
    void set_gpu_device_id(std::uint32_t device_id) noexcept { gpu_device_id_ = device_id; }
    void set_gpu_hot_path_mode(GpuHotPathMode mode) noexcept { gpu_hot_path_mode_ = mode; }

    [[nodiscard]] const DebugConfig& debug() const noexcept { return debug_; }
    [[nodiscard]] const CacheConfig& cache() const noexcept { return cache_; }
    [[nodiscard]] const SchedulerConfig& scheduler() const noexcept { return scheduler_; }
    [[nodiscard]] const RuntimePathConfig& runtime() const noexcept { return runtime_; }
    [[nodiscard]] const CpuBudget& cpu_budget() const noexcept { return cpu_budget_; }
    [[nodiscard]] chronon3d::graph::BackendPreference backend_preference() const noexcept {
        return backend_preference_;
    }
    [[nodiscard]] std::uint32_t gpu_device_id() const noexcept { return gpu_device_id_; }
    [[nodiscard]] GpuHotPathMode gpu_hot_path_mode() const noexcept { return gpu_hot_path_mode_; }

    [[nodiscard]] static std::size_t resolve_env_mb(const char* env_name, std::size_t default_mb);
    [[nodiscard]] static std::size_t resolve_env_int(const char* env_name, std::size_t default_int);

    Config();
    Config(Config&&) noexcept = default;
    Config& operator=(Config&&) noexcept = default;
    Config(const Config&) = default;
    Config& operator=(const Config&) = default;

private:
    DebugConfig debug_;
    CacheConfig cache_;
    SchedulerConfig scheduler_;
    RuntimePathConfig runtime_;
    CpuBudget cpu_budget_;
    chronon3d::graph::BackendPreference backend_preference_{
        chronon3d::graph::BackendPreference::Auto};
    std::uint32_t gpu_device_id_{kAutoGpuDevice};
    GpuHotPathMode gpu_hot_path_mode_{GpuHotPathMode::Auto};
};

} // namespace chronon3d

// TICKET-007: DebugConfig is propagated per-instance through
// RenderGraphContext::options::debug_config. No process-global debug pointer.
