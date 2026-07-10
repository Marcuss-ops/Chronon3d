// D3 — unified `inspect` command replacing scattered graph/preflight/camera-path/text-audit.
// Subcommands: inspect graph, inspect preflight, inspect camera, inspect text, inspect text-def.

#include "../../command_registry.hpp"
#include "../../commands.hpp"
#include <spdlog/spdlog.h>
#include <memory>

namespace chronon3d::cli {

// D5 — Inspect subcommands now bind CLI11 options directly to the canonical
// arg structs (GraphArgs, PreflightArgs, CameraPathArgs, TextAuditArgs)
// instead of using shadow duplicate structs with field-by-field copy.

void register_inspect_commands(CLI::App& app, CliContext& ctx) {
    auto* inspect = app.add_subcommand("inspect", "Unified inspection: graph, preflight, camera, text");

    // ── inspect graph ───────────────────────────────────────────────
    {
        auto s = std::make_shared<GraphArgs>();
        auto* cmd = inspect->add_subcommand("graph", "Inspect the render graph (summary, DOT export, layer plan)");
        cmd->add_option("id", s->comp_id, "Composition name")->required();
        cmd->add_option("--frame", s->frame, "Frame to inspect")->default_val(0);
        cmd->add_option("-o,--output", s->output, "Output .dot file path");
        cmd->add_flag("--summary", s->summary, "Print node counts, cache stats, and timing");
        cmd->add_flag("--plan", s->plan, "Print planned layer and bbox placement before rendering");
        cmd->callback([s, &ctx]() {
            if (!s->summary && s->output.empty() && !s->plan) {
                s->output = "output/graph.dot";
                spdlog::warn("No --output, --summary, or --plan; defaulting to {}", s->output);
            }
            ctx.exit_code = command_graph(ctx.registry, *s);
        });
    }

    // ── inspect preflight ───────────────────────────────────────────
    {
        auto s = std::make_shared<PreflightArgs>();
        auto* cmd = inspect->add_subcommand("preflight", "Validate assets and requirements before rendering");
        cmd->add_option("id", s->comp_id, "Composition name")->required();
        cmd->add_option("--start", s->start, "Start frame")->default_val(0);
        cmd->add_option("--end", s->end, "End frame")->default_val(0);
        cmd->add_option("--sample-step", s->sample_step, "Sample every N frames");
        cmd->add_option("-o,--output", s->output, "Output file path (to check writability)");
        cmd->add_option("--json", s->json_file, "Path to output preflight JSON report");
        cmd->add_flag("--legacy", s->legacy_preflight, "Also run legacy RenderPreflight (default: V2 manifest-only)");
        cmd->callback([s, &ctx]() {
            ctx.exit_code = command_preflight(ctx.registry, *s, ctx.assets);
        });
    }

    // ── inspect camera ──────────────────────────────────────────────
    {
        auto s = std::make_shared<CameraPathArgs>();
        auto* cmd = inspect->add_subcommand("camera", "Export camera path as JSON/CSV for debug and validation");
        cmd->add_option("id", s->comp_id, "Composition name")->required();
        cmd->add_option("--start", s->start, "Start frame (inclusive)")->default_val(0);
        cmd->add_option("--end", s->end, "End frame (inclusive, 0 = composition duration)")->default_val(0);
        cmd->add_option("--step", s->step, "Sample every N frames")->default_val(1);
        cmd->add_option("-o,--output", s->output, "Output file path (.json or .csv)");
        cmd->add_option("--format", s->format, "Export format: json or csv (auto-detected from -o extension)")->default_val("json");
        cmd->callback([s, &ctx]() {
            ctx.exit_code = command_camera_path(ctx.registry, *s);
        });
    }

    // ── inspect text ────────────────────────────────────────────────
    {
        auto s = std::make_shared<TextAuditArgs>();
        auto* cmd = inspect->add_subcommand("text", "Text placement audit with JSON output and optional frame renders");
        cmd->add_option("id", s->comp_id, "Composition name")->required();
        cmd->add_option("--frames", s->frames, "Frames to audit (e.g. \"0,19,40\" or \"0-300x10\")")->default_val("0");
        cmd->add_option("--json", s->json_output, "JSON output path");
        cmd->add_option("--render-dir", s->render_dir, "Directory for frame PNGs");
        cmd->add_option("--safe-margin-x", s->safe_margin_x, "Fraction of canvas width");
        cmd->add_option("--safe-margin-y", s->safe_margin_y, "Fraction of canvas height");
        cmd->add_option("--max-center-error-px", s->max_center_error_px, "Max center error in pixels");
        cmd->add_option("--max-border-alpha", s->max_border_alpha_pixels, "Max border alpha pixels");
        cmd->add_option("--glyph-tolerance", s->glyph_tolerance, "Glyph tolerance");
        cmd->add_option("--alpha-threshold", s->alpha_threshold, "Alpha threshold (0-255)");
        cmd->callback([s, &ctx]() {
            ctx.exit_code = command_text_audit(ctx.registry, *s);
        });
    }

    // ── inspect text-def ─────────────────────────────────────────────
    {
        auto s = std::make_shared<TextDefInspectArgs>();
        auto* cmd = inspect->add_subcommand("text-def", "Dump TextDefinition/TextRunSpec fields as JSON for frame 0");
        cmd->add_option("id", s->comp_id, "Composition name")->required();
        cmd->add_option("--json", s->json_output, "JSON output path (stdout if empty)");
        cmd->callback([s, &ctx]() {
            ctx.exit_code = command_text_def_inspect(ctx.registry, *s);
        });
    }
}

// D4 — All legacy top-level commands (graph, preflight, camera-path, text-audit)
// have been removed.  The canonical entry points are `inspect graph`,
// `inspect preflight`, `inspect camera`, `inspect text`, and `inspect text-def`.

} // namespace chronon3d::cli
