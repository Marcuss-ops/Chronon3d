#include "render_job.hpp"

#include <spdlog/spdlog.h>

#include <optional>

namespace chronon3d::cli {

std::optional<RenderJob> make_render_job(const CompositionRegistry& registry,
                                         const RenderArgs& args) {
    return make_render_job(registry, args, CompositionProps{});
}

std::optional<RenderJob> make_render_job(const CompositionRegistry& registry,
                                         const RenderArgs& args,
                                         const CompositionProps& props) {
    const auto range = parse_frames(args.frames);
    auto resolved = resolve_composition(registry, args.comp_id, props);
    if (!resolved) {
        return std::nullopt;
    }

    RenderJob job;
    job.registry = &registry;
    job.comp = std::move(resolved.comp);
    job.comp_id = args.comp_id;
    job.output = args.output;
    job.settings = settings_from_args(args, true, args.pipeline.diagnostic);
    job.settings.diagnostics.plan_output = args.pipeline.diagnostic_plan_output;
    job.frame_step = Frame{range.step};

    if (range.start == range.end) {
        job.mode = RenderMode::Still;
        job.still_frame = Frame{range.start};
    } else {
        job.mode = RenderMode::Sequence;
        job.first_frame = Frame{range.start};
        job.last_frame = Frame{range.end};
    }

    job.execution.log_level = args.log_level;
    job.execution.benchmark_all = args.benchmark_all;
    job.execution.report = args.report;
    job.execution.command_line = args.command_line;
    job.execution.diagnostic_plan = args.pipeline.diagnostic_plan;
    job.execution.warmup_renderer = args.pipeline.warmup_renderer;
    job.execution.warmup_framebuffers = args.pipeline.warmup_framebuffers;
    job.execution.warmup_dummy_frame = args.pipeline.warmup_dummy_frame;
    job.execution.cpu_budget = args.cpu_budget;

    Config cfg;
    cfg.set_cpu_budget(args.cpu_budget);
    if (args.pipeline.fb_pool_budget_mb > 0) {
        cfg.set_fb_pool_budget(args.pipeline.fb_pool_budget_mb * 1024ULL * 1024ULL);
    }
    if (!args.pipeline.fb_pool_clear_policy.empty()) {
        const auto& policy_str = args.pipeline.fb_pool_clear_policy;
        if (auto parsed = chronon3d::cache::parse_framebuffer_pool_clear_policy(policy_str)) {
            cfg.set_fb_pool_clear_policy(*parsed);
        } else {
            spdlog::warn(
                "Invalid --fb-pool-clear-policy='{}'; valid values: "
                "keep-warm, trim-after-job, trim-on-memory-pressure. "
                "Using env-resolved default.",
                policy_str);
        }
    }
    job.execution.config = std::move(cfg);

    return job;
}

std::optional<RenderJob> plan_render_job(const CompositionRegistry& registry,
                                         const RenderArgs& args) {
    return make_render_job(registry, args);
}

std::optional<RenderJob> plan_render_job(const CompositionRegistry& registry,
                                         const RenderArgs& args,
                                         const CompositionProps& props) {
    return make_render_job(registry, args, props);
}

} // namespace chronon3d::cli
