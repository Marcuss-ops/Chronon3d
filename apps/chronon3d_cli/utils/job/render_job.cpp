#include "render_job.hpp"

#include <spdlog/spdlog.h>

#include <optional>

namespace chronon3d::cli {

std::optional<RenderJobPlan> plan_render_job(const CompositionRegistry& registry,
                                             const RenderArgs& args) {
    auto range = parse_frames(args.frames);
    auto resolved = resolve_composition(registry, args.comp_id);
    if (!resolved) {
        return std::nullopt;
    }

    RenderJobPlan plan;
    plan.range = range;
    plan.comp = std::move(resolved.comp);
    plan.comp_id = args.comp_id;
    plan.output = args.output;
    plan.settings = settings_from_args(args, true, args.pipeline.diagnostic);
    plan.log_level = args.log_level;
    plan.benchmark_all = args.benchmark_all;
    plan.report = args.report;
    plan.command_line = args.command_line;
    plan.diagnostic_plan = args.pipeline.diagnostic_plan;
    plan.settings.diagnostics.plan_output = args.pipeline.diagnostic_plan_output;
    plan.warmup_renderer = args.pipeline.warmup_renderer;
    plan.warmup_framebuffers = args.pipeline.warmup_framebuffers;
    plan.warmup_dummy_frame = args.pipeline.warmup_dummy_frame;

    // Build a per-instance Config that carries the single CLI CpuBudget.
    Config cfg;  // reads env vars once
    cfg.set_cpu_budget(args.cpu_budget);
    if (args.pipeline.fb_pool_budget_mb > 0) {
        cfg.set_fb_pool_budget(args.pipeline.fb_pool_budget_mb * 1024ULL * 1024ULL);
    }
    // P1-21: propagate the framebuffer pool clear policy to the per-job
    // Config.  The CLI flag accepts "keep-warm" | "trim-after-job" |
    // "trim-on-memory-pressure" (case-insensitive).  Empty = use the
    // env-resolved default (CHRONON3D_FB_POOL_CLEAR_POLICY).
    if (!args.pipeline.fb_pool_clear_policy.empty()) {
        const auto& policy_str = args.pipeline.fb_pool_clear_policy;
        using CachePolicy = chronon3d::cache::FramebufferPoolClearPolicy;
        if (policy_str == "keep-warm" || policy_str == "KeepWarm") {
            cfg.set_fb_pool_clear_policy(CachePolicy::KeepWarm);
        } else if (policy_str == "trim-after-job" || policy_str == "TrimAfterJob") {
            cfg.set_fb_pool_clear_policy(CachePolicy::TrimAfterJob);
        } else if (policy_str == "trim-on-memory-pressure" || policy_str == "TrimOnMemoryPressure") {
            cfg.set_fb_pool_clear_policy(CachePolicy::TrimOnMemoryPressure);
        } else {
            spdlog::warn(
                "Invalid --fb-pool-clear-policy='{}'; valid values: "
                "keep-warm, trim-after-job, trim-on-memory-pressure. "
                "Using env-resolved default.",
                policy_str);
        }
    }
    plan.config = std::move(cfg);

    return plan;
}

} // namespace chronon3d::cli
