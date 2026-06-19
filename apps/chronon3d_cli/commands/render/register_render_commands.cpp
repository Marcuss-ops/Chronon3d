#include "../../command_registry.hpp"
#include "../../commands.hpp"
#include "../../utils/common/cli_utils.hpp"
#include <chronon3d/core/config.hpp>
#include <spdlog/spdlog.h>
#include <memory>

namespace chronon3d::cli {

namespace {
struct RenderState { std::shared_ptr<RenderArgs> args{std::make_shared<RenderArgs>()}; };
}

void register_render_commands(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<RenderState>();
    auto& args = *state->args;

    auto* cmd = app.add_subcommand("render", "Render a composition id to image(s)");
    cmd->add_option("input", args.comp_id, "Composition name or .specscene path")->required();
    cmd->add_option("--frames", args.frames, "Frame range: 0 | 0-90 | 0-90x5");
    cmd->add_option("--frame", args.frames, "Single frame number (alias for --frames)");
    cmd->add_option("-o,--output", args.output, "Output path (use #### for frame number)");
    cmd->add_flag("--diagnostic,--layout-preview", args.pipeline.diagnostic,
                  "Enable layout preview overlays (bbox, anchors, center guide)");
    cmd->add_flag("--diagnostic-plan", args.pipeline.diagnostic_plan,
                  "Log graph preflight diagnostics before rendering each frame");
    cmd->add_option("--diagnostic-plan-output", args.pipeline.diagnostic_plan_output,
                    "Write graph preflight report to a file path pattern");
    cmd->add_flag("--graph,!--no-graph", args.pipeline.use_modular_graph, "Use modular RenderGraph path");
    cmd->add_flag("--no-dirty-rects", args.pipeline.no_dirty_rects, "Disable dirty-rectangle invalidation (dirty rects are ON by default)");
    cmd->add_option("--tile-size", args.pipeline.tile_size, "Tile size for dirty-rect tile execution (e.g. 64)");
    cmd->add_flag("--motion-blur", args.pipeline.quality.motion_blur, "Enable temporal motion blur");
    cmd->add_option("--motion-blur-samples", args.pipeline.quality.motion_blur_samples, "Subframe samples (default 8)");
    cmd->add_option("--shutter-angle", args.pipeline.quality.shutter_angle_deg, "Shutter angle in degrees (default 180)");
    cmd->add_option("--shutter-phase", args.pipeline.quality.shutter_phase_deg, "Shutter phase in degrees (default -90, centres exposure on frame)");
    cmd->add_option("--motion-blur-pattern", args.pipeline.quality.motion_blur_pattern, "Sample pattern: 0=Uniform, 1=Stratified, 2=Halton")->default_val(1);
    cmd->add_option("--motion-blur-filter", args.pipeline.quality.motion_blur_filter, "Reconstruction filter: 0=Box, 1=Triangle, 2=Gaussian")->default_val(0);
    cmd->add_option("--ssaa", args.pipeline.quality.ssaa, "Super Sampling factor (default 1.0)");
    cmd->add_option("-v,--log-level", args.log_level, "Log level: trace | debug | info | warn | error");
    cmd->add_flag("--benchmark-all", args.benchmark_all,
        "Write detailed phase durations for all graph nodes");

    cmd->add_flag("--report", args.report,
        "Generate an execution report log");
    cmd->add_flag("--force-scalar-normal-blend", args.pipeline.force_scalar_normal_blend,
                  "Force scalar (non-SIMD) Normal blend for diagnosing rendering regressions");
    cmd->add_flag("--warmup-renderer", args.pipeline.warmup_renderer,
                  "Preallocate framebuffers and prime caches before rendering");
    cmd->add_option("--warmup-framebuffers", args.pipeline.warmup_framebuffers,
                    "Number of framebuffers to preallocate (default 8)");
    cmd->add_flag("--warmup-dummy-frame", args.pipeline.warmup_dummy_frame,
                  "Render a dummy frame 0 to prime all caches");
    cmd->add_option("--fb-pool-budget-mb", args.pipeline.fb_pool_budget_mb,
                    "Framebuffer pool retention budget in MB (0=unlimited, default 384)");
    cmd->add_option("--program-cache-capacity", args.pipeline.program_cache_capacity,
                    "SceneProgramCache entries per Precomp node (0=default 8)");
    cmd->add_flag("--program-cache-tune", args.pipeline.program_cache_tune,
                  "Auto-tune SceneProgramCache capacity based on hit/eviction ratio");
    cmd->add_option("--program-cache-tune-interval", args.pipeline.program_cache_tune_interval,
                    "Frames between auto-tune checks (default 30)");
    cmd->add_option("--program-cache-tune-min", args.pipeline.program_cache_tune_min_capacity,
                    "Minimum capacity when down-tuning (default 2)");
    cmd->add_option("--program-cache-tune-max", args.pipeline.program_cache_tune_max_capacity,
                    "Maximum capacity when up-tuning (default 128)");
    cmd->allow_windows_style_options();
    cmd->callback([state, &ctx]() {
        state->args->command_line = ctx.command_line;
        if (state->args->pipeline.fb_pool_budget_mb > 0) {
            Config::get().fb_pool_budget_bytes = state->args->pipeline.fb_pool_budget_mb * 1024ULL * 1024ULL;
        }
        if (state->args->output.empty()) {
            state->args->output = chronon_artifact_path("renders", "render_####.png").string();
            spdlog::warn("No output path specified, defaulting to {}", state->args->output);
        }
        if (state->args->log_level == "trace") {
            spdlog::set_level(spdlog::level::trace);
        } else if (state->args->log_level == "debug") {
            spdlog::set_level(spdlog::level::debug);
        } else if (state->args->log_level == "info") {
            spdlog::set_level(spdlog::level::info);
        } else if (state->args->log_level == "warn") {
            spdlog::set_level(spdlog::level::warn);
        } else if (state->args->log_level == "error" || state->args->log_level == "err") {
            spdlog::set_level(spdlog::level::err);
        }
        ctx.exit_code = command_render(ctx.registry, *state->args);
    });
}

} // namespace chronon3d::cli
