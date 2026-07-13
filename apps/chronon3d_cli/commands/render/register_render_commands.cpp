#include "../../command_registry.hpp"
#include "../../commands.hpp"
#include <spdlog/spdlog.h>
#include <filesystem>
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
    cmd->add_flag("--debug-text-layout", args.pipeline.text_layout_debug,
                  "Draw text layout debug overlay (canvas center, layout box, visual bounds, alpha centroid) + emit [text-layout] structured log per TextRun");
    cmd->add_flag("--diagnostic-overlay", args.pipeline.diagnostic_overlay,
                  "Draw diagnostic overlay on text layers: bbox (green rect), anchor point (blue dot), baseline (cyan line)");
    cmd->add_flag("--diagnostic-overlay-only", args.pipeline.diagnostic_overlay_only,
                  "Like --diagnostic-overlay but on transparent background (no scene content)");
    cmd->add_option("--debug-text-layout-json", args.pipeline.text_layout_debug_json_path,
                    "Write per-TextRun bounds JSON (alpha_bounds, layout_bounds, scratch_bounds, ascent, descent) to PATH (requires --debug-text-layout)");
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
        state->args->cpu_budget = ctx.cpu_budget;
        // fb_pool_budget_mb is handled in plan_render_job() via Config::set_fb_pool_budget()
        if (state->args->output.empty()) {
            // Default output path: <project_root>/output/<comp_id_basename>.png
            // (relative to CWD, which the Flask dashboard's ALLOWED_ARTIFACT_ROOTS
            // accepts via PROJECT_ROOT/OUTPUT_DIR). This ensures the resulting
            // output_path in render_runs.output_path is always dashboard-previewable
            // out of the box, instead of falling back to ~/.chronon3d/artifacts/renders/
            // (which is outside ALLOWED_ARTIFACT_ROOTS and would surface as
            // "Frame preview unavailable" in the dashboard UI).
            std::filesystem::path comp_path(state->args->comp_id);
            std::string comp_basename = comp_path.stem().string();
            if (comp_basename.empty()) {
                comp_basename = "render";
            }
            state->args->output = "output/" + comp_basename + ".png";
            spdlog::info("No output path specified, defaulting to {}", state->args->output);
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

    // ── still: single-frame render with FrameOnly preflight ────────
    auto still_state = std::make_shared<std::shared_ptr<StillArgs>>(std::make_shared<StillArgs>());
    auto& still_args = **still_state;

    auto* still = app.add_subcommand("still", "Render a single frame with asset preflight");
    still->add_option("input", still_args.comp_id, "Composition name or .specscene path")->required();
    still->add_option("--frame", still_args.frame, "Frame number to render")->default_val(0);
    still->add_option("-o,--output", still_args.output, "Output PNG path");
    still->add_flag("--skip-preflight", still_args.skip_preflight, "Skip FrameOnly asset preflight");
    still->add_option("-v,--log-level", still_args.log_level, "Log level: trace | debug | info | warn | error");
    // Pipeline options (subset of render)
    still->add_flag("--diagnostic-overlay", still_args.pipeline.diagnostic_overlay,
                  "Draw diagnostic overlay on text layers: bbox (green rect), anchor point (blue dot), baseline (cyan line)");
    still->add_flag("--diagnostic-overlay-only", still_args.pipeline.diagnostic_overlay_only,
                  "Like --diagnostic-overlay but on transparent background (no scene content)");
    still->add_flag("--graph,!--no-graph", still_args.pipeline.use_modular_graph, "Use modular RenderGraph path");
    still->add_flag("--no-dirty-rects", still_args.pipeline.no_dirty_rects, "Disable dirty-rectangle invalidation");
    still->add_flag("--motion-blur", still_args.pipeline.quality.motion_blur, "Enable temporal motion blur");
    still->add_option("--ssaa", still_args.pipeline.quality.ssaa, "Super Sampling factor (default 1.0)");
    still->allow_windows_style_options();
    still->callback([still_state, &ctx]() {
        if ((*still_state)->log_level == "trace") {
            spdlog::set_level(spdlog::level::trace);
        } else if ((*still_state)->log_level == "debug") {
            spdlog::set_level(spdlog::level::debug);
        } else if ((*still_state)->log_level == "warn") {
            spdlog::set_level(spdlog::level::warn);
        } else if ((*still_state)->log_level == "error" || (*still_state)->log_level == "err") {
            spdlog::set_level(spdlog::level::err);
        }
        ctx.exit_code = command_still(ctx.registry, **still_state);
    });
}

} // namespace chronon3d::cli
