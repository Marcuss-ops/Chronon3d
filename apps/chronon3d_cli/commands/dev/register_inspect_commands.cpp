// D3 — unified `inspect` command replacing scattered graph/preflight/camera-path/text-audit.
// Subcommands: inspect graph, inspect preflight, inspect camera, inspect text.

#include "../../command_registry.hpp"
#include "../../commands.hpp"
#include <spdlog/spdlog.h>
#include <memory>

namespace chronon3d::cli {

namespace {

struct InspectGraphArgs {
    std::string comp_id;
    Frame frame{0};
    std::string output;
    bool summary{false};
    bool plan{false};
};

struct InspectPreflightArgs {
    std::string comp_id;
    Frame start{0};
    Frame end{0};
    int sample_step{1};
    std::string output;
    std::string json_file;
    bool legacy_preflight{false};
};

struct InspectCameraArgs {
    std::string comp_id;
    Frame start{0};
    Frame end{0};
    int step{1};
    std::string output;
    std::string format{"auto"};
};

struct InspectTextArgs {
    std::string comp_id;
    std::string frames;
    std::string json_output;
    std::string render_dir;
    float safe_margin_x{0.05f};
    float safe_margin_y{0.05f};
    float max_center_error_px{2.0f};
    int max_border_alpha_pixels{0};
    float glyph_tolerance{0.01f};
    int alpha_threshold{8};
};

} // namespace

void register_inspect_commands(CLI::App& app, CliContext& ctx) {
    auto* inspect = app.add_subcommand("inspect", "Unified inspection: graph, preflight, camera, text (D3)");

    // ── inspect graph ───────────────────────────────────────────────
    {
        auto s = std::make_shared<InspectGraphArgs>();
        auto* cmd = inspect->add_subcommand("graph", "Inspect the render graph (summary, DOT export, layer plan)");
        cmd->add_option("id", s->comp_id, "Composition name")->required();
        cmd->add_option("--frame", s->frame, "Frame to inspect")->default_val(0);
        cmd->add_option("-o,--output", s->output, "Output .dot file path");
        cmd->add_flag("--summary", s->summary, "Print node counts, cache stats, and timing");
        cmd->add_flag("--plan", s->plan, "Print planned layer and bbox placement before rendering");
        cmd->callback([s, &ctx]() {
            GraphArgs ga;
            ga.comp_id = s->comp_id;
            ga.frame   = s->frame;
            ga.output  = s->output;
            ga.summary = s->summary;
            ga.plan    = s->plan;
            if (!s->summary && s->output.empty() && !s->plan) {
                s->output = "output/graph.dot";
                spdlog::warn("No --output, --summary, or --plan; defaulting to {}", s->output);
            }
            ctx.exit_code = command_graph(ctx.registry, ga);
        });
    }

    // ── inspect preflight ───────────────────────────────────────────
    {
        auto s = std::make_shared<InspectPreflightArgs>();
        auto* cmd = inspect->add_subcommand("preflight", "Validate assets and requirements before rendering");
        cmd->add_option("id", s->comp_id, "Composition name")->required();
        cmd->add_option("--start", s->start, "Start frame")->default_val(0);
        cmd->add_option("--end", s->end, "End frame")->default_val(0);
        cmd->add_option("--sample-step", s->sample_step, "Sample every N frames");
        cmd->add_option("-o,--output", s->output, "Output file path (to check writability)");
        cmd->add_option("--json", s->json_file, "Path to output preflight JSON report");
        cmd->add_flag("--legacy", s->legacy_preflight, "Also run legacy RenderPreflight (default: V2 manifest-only)");
        cmd->callback([s, &ctx]() {
            PreflightArgs pa;
            pa.comp_id         = s->comp_id;
            pa.start           = s->start;
            pa.end             = s->end;
            pa.sample_step     = s->sample_step;
            pa.output          = s->output;
            pa.json_file       = s->json_file;
            pa.legacy_preflight = s->legacy_preflight;
            ctx.exit_code = command_preflight(ctx.registry, pa, ctx.assets);
        });
    }

    // ── inspect camera ──────────────────────────────────────────────
    {
        auto s = std::make_shared<InspectCameraArgs>();
        auto* cmd = inspect->add_subcommand("camera", "Export camera path as JSON/CSV for debug and validation");
        cmd->add_option("id", s->comp_id, "Composition name")->required();
        cmd->add_option("--start", s->start, "Start frame (inclusive)")->default_val(0);
        cmd->add_option("--end", s->end, "End frame (inclusive, 0 = composition duration)")->default_val(0);
        cmd->add_option("--step", s->step, "Sample every N frames")->default_val(1);
        cmd->add_option("-o,--output", s->output, "Output file path (.json or .csv)");
        cmd->add_option("--format", s->format, "Export format: json or csv (auto-detected from -o extension)")->default_val("json");
        cmd->callback([s, &ctx]() {
            CameraPathArgs cpa;
            cpa.comp_id = s->comp_id;
            cpa.start   = s->start;
            cpa.end     = s->end;
            cpa.step    = s->step;
            cpa.output  = s->output;
            cpa.format  = s->format;
            ctx.exit_code = command_camera_path(ctx.registry, cpa);
        });
    }

    // ── inspect text ────────────────────────────────────────────────
    {
        auto s = std::make_shared<InspectTextArgs>();
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
            TextAuditArgs ta;
            ta.comp_id              = s->comp_id;
            ta.frames               = s->frames;
            ta.json_output          = s->json_output;
            ta.render_dir           = s->render_dir;
            ta.safe_margin_x        = s->safe_margin_x;
            ta.safe_margin_y        = s->safe_margin_y;
            ta.max_center_error_px   = s->max_center_error_px;
            ta.max_border_alpha_pixels = s->max_border_alpha_pixels;
            ta.glyph_tolerance      = s->glyph_tolerance;
            ta.alpha_threshold      = s->alpha_threshold;
            ctx.exit_code = command_text_audit(ctx.registry, ta);
        });
    }
}

// ── Legacy registration (backward compat) ───────────────────────────
// The old `graph`, `preflight`, `camera-path` top-level commands are
// retained via register_inspect_commands() but will be deprecated in
// a follow-up PR once all callers have migrated to `inspect <subcmd>`.

} // namespace chronon3d::cli
