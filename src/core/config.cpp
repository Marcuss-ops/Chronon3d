#include <chronon3d/core/config.hpp>
#include <chronon3d/core/scheduler/scheduler_mode.hpp>
#include <chronon3d/core/cpu_budget.hpp>
#include <cstdlib>
#include <cstring>
#include <charconv>
#include <string_view>
#include <spdlog/spdlog.h>
#include <thread>

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
        spdlog::warn("Invalid environment value {}='{}'; using default {} MB",
                     env_name, env, default_mb);
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
        spdlog::warn("Invalid environment value {}='{}'; using default {}",
                     env_name, env, default_int);
        return default_int;
    }
    return value;
}

Config Config::from_environment() {
    Config cfg;
    cfg.set_cpu_budget(cpu_budget_from_environment(
        static_cast<int>(std::thread::hardware_concurrency())));
    return cfg;
}

Config Config::from_environment(const CpuBudget& budget) {
    Config cfg;
    cfg.set_cpu_budget(budget);
    return cfg;
}

void Config::set_fb_pool_budget(std::size_t bytes) {
    cache_.fb_pool_budget_bytes_ = bytes;
}

void Config::set_fb_pool_clear_policy(chronon3d::cache::FramebufferPoolClearPolicy policy) {
    cache_.framebuffer_pool_clear_policy_ = policy;
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
    // Diagnostic/debug environment variables are parsed once here. Runtime
    // render code consumes DebugConfig and must not perform secondary lookups.
    debug_.glow_             = env_bool("CHRONON_DEBUG_GLOW");
    debug_.dump_alpha_mask_  = env_bool("CHRONON_DEBUG_DUMP_ALPHA_MASK");
    debug_.dump_text_raster_ = env_bool("CHRONON_DEBUG_DUMP_TEXT_RASTER");
    debug_.text_raster_      = env_bool("CHRONON_DEBUG_TEXT_RASTER");
    debug_.text_bbox_        = env_bool("CHRONON_DEBUG_TEXT_BBOX");
    debug_.text_clip_debug_  = env_bool("CHRONON3D_TEXT_CLIP_DEBUG");
    debug_.proj_diag_        = env_bool("CHRONON3D_PROJ_DIAG");

    {
        const char* pp_v = std::getenv("CHRONON_PINGPONG_FRAMEBUFFER");
        if (pp_v && *pp_v) {
            scheduler_.pingpong_framebuffer_ = env_bool("CHRONON_PINGPONG_FRAMEBUFFER");
        }
    }
    scheduler_.prefetch_enabled_ = env_bool("CHRONON_PREFETCH");
    scheduler_.pip_mode_ = env_bool("CHRONON_PIP_MODE");
    scheduler_.pin_calling_thread_ = env_bool("CHRONON3D_PIN_MAIN_THREAD");

    {
        const char* mode_env = std::getenv("CHRONON3D_SCHEDULER_MODE");
        if (mode_env && *mode_env) {
            SchedulerMode parsed;
            if (parse_scheduler_mode(mode_env, parsed)) {
                scheduler_.mode_ = parsed;
            } else {
                spdlog::warn("Invalid CHRONON3D_SCHEDULER_MODE='{}'; defaulting to {}",
                             mode_env, scheduler_mode_name(scheduler_.mode_));
            }
        }
    }
    {
        const char* workers_env = std::getenv("CHRONON3D_SCHEDULER_WORKERS");
        if (workers_env && *workers_env) {
            const std::size_t len = std::strlen(workers_env);
            int n = 0;
            const auto [ptr, ec] = std::from_chars(workers_env, workers_env + len, n);
            if (ec == std::errc{} && ptr == workers_env + len && n > 0) {
                scheduler_.worker_count_ = n;
            } else {
                spdlog::warn("Invalid CHRONON3D_SCHEDULER_WORKERS='{}'; defaulting to 0",
                             workers_env);
            }
        }
    }

    cache_.fb_pool_max_bytes_ = resolve_env_mb("CHRONON_FB_POOL_MAX_MB", 0);
    cache_.fb_pool_budget_bytes_ = resolve_env_mb("CHRONON3D_FB_POOL_BUDGET_MB", 0);
    cache_.image_cache_max_bytes_ = resolve_env_mb("CHRONON_IMAGE_CACHE_MAX_MB", 0);
    cache_.node_cache_max_bytes_ = resolve_env_mb("CHRONON_NODE_CACHE_MAX_MB", 0);
    cache_.glyph_atlas_max_bytes_ = resolve_env_mb("CHRONON_GLYPH_ATLAS_MAX_MB", 0);
    cache_.text_cache_max_bytes_ = resolve_env_mb("CHRONON_TEXT_CACHE_MAX_MB", 128);
    cache_.shadow_cache_max_bytes_ = resolve_env_mb("CHRONON_SHADOW_CACHE_MAX_MB", 64);
    cache_.glow_cache_max_bytes_ = resolve_env_mb("CHRONON_GLOW_CACHE_MAX_MB", 64);

    cache_.frame_cache_max_entries_ = resolve_env_int("CHRONON3D_FRAME_CACHE_MAX_ENTRIES", 0);
    cache_.video_frame_max_entries_ = resolve_env_int("CHRONON3D_VIDEO_FRAME_MAX_ENTRIES", 0);
    cache_.converted_frame_cache_max_bytes_ = resolve_env_int("CHRONON3D_CONVERTED_FRAME_CACHE_MAX_BYTES", 0);
    cache_.scene_program_cache_max_entries_ = resolve_env_int("CHRONON3D_SCENE_PROGRAM_CACHE_MAX_ENTRIES", 0);

    {
        const char* policy_env = std::getenv("CHRONON3D_FB_POOL_CLEAR_POLICY");
        if (policy_env && *policy_env) {
            std::string_view sv(policy_env);
            if (auto parsed = chronon3d::cache::parse_framebuffer_pool_clear_policy(sv)) {
                cache_.framebuffer_pool_clear_policy_ = *parsed;
            } else {
                spdlog::warn(
                    "Invalid CHRONON3D_FB_POOL_CLEAR_POLICY='{}'; valid values: "
                    "keep-warm, trim-after-job, trim-on-memory-pressure. "
                    "Defaulting to trim-after-job.", policy_env);
            }
        }
    }

    runtime_.assets_root_ = env_string("CHRONON3D_CLI_ASSETS_ROOT");
    runtime_.cli_assets_root_ = runtime_.assets_root_;

    // Observability environment belongs to the process/config boundary too.
    // TelemetryManager receives these resolved values and never re-reads env.
    runtime_.telemetry_path_ = env_string("CHRONON3D_TELEMETRY_PATH");
    runtime_.telemetry_run_id_ = env_string("CHRONON3D_RUN_ID");
    if (const std::string level = env_string("CHRONON3D_TELEMETRY_LEVEL"); !level.empty()) {
        runtime_.telemetry_level_ = level;
    }
    if (const std::string ttl = env_string("CHRONON3D_TELEMETRY_DETAIL_TTL_DAYS"); !ttl.empty()) {
        runtime_.telemetry_detailed_ttl_days_ = ttl;
    }
    const std::string home = env_string("HOME");
    runtime_.telemetry_default_directory_ = home.empty()
        ? std::string{"/tmp/.chronon3d/telemetry"}
        : home + "/.chronon3d/telemetry";

    const char* hot_path_env = std::getenv("CHRONON3D_GPU_HOT_PATH_MODE");
    if (hot_path_env && *hot_path_env) {
        gpu_hot_path_mode_ = parse_gpu_hot_path_mode(hot_path_env);
    }
}

} // namespace chronon3d
