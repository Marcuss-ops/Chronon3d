#include "render_job.hpp"

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

    // Build a per-instance Config when CLI budget override is requested.
    if (args.pipeline.fb_pool_budget_mb > 0) {
        Config cfg;  // default-constructs from environment
        cfg.set_fb_pool_budget(args.pipeline.fb_pool_budget_mb * 1024ULL * 1024ULL);
        plan.config = std::move(cfg);
    }

    return plan;
}

} // namespace chronon3d::cli
