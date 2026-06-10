#include "../../command_registry.hpp"
#include "../../commands.hpp"
#include <spdlog/spdlog.h>
#include <memory>

namespace chronon3d::cli {

namespace {
struct GraphState { std::shared_ptr<GraphArgs> args{std::make_shared<GraphArgs>()}; };
struct PreflightState { std::shared_ptr<PreflightArgs> args{std::make_shared<PreflightArgs>()}; };
struct CameraPathState { std::shared_ptr<CameraPathArgs> args{std::make_shared<CameraPathArgs>()}; };
}

void register_inspect_commands(CLI::App& app, CliContext& ctx) {
    // --- graph ---
    {
        auto state = std::make_shared<GraphState>();
        auto& args = *state->args;
        auto* cmd = app.add_subcommand("graph", "Inspect the render graph: summary stats or DOT export");
        cmd->add_option("id", args.comp_id, "Composition name or .specscene path")->required();
        cmd->add_option("--frame", args.frame, "Frame to inspect")->default_val(0);
        cmd->add_option("-o,--output", args.output, "Output .dot file path");
        cmd->add_flag("--summary", args.summary, "Print node counts, cache stats, and timing");
        cmd->add_flag("--plan", args.plan, "Print planned layer and bbox placement before rendering");
        cmd->callback([state, &ctx]() {
            if (!state->args->summary && state->args->output.empty()) {
                state->args->output = "output/graph.dot";
                spdlog::warn("No --output and no --summary; defaulting to {}", state->args->output);
            }
            ctx.exit_code = command_graph(ctx.resources.registry, *state->args);
        });
    }

    // --- camera-path ---
    {
        auto state = std::make_shared<CameraPathState>();
        auto& args = *state->args;
        auto* cmd = app.add_subcommand("camera-path", "Export camera path as JSON/CSV for debug and validation");
        cmd->add_option("id", args.comp_id, "Composition name or .specscene path")->required();
        cmd->add_option("--start", args.start, "Start frame (inclusive)")->default_val(0);
        cmd->add_option("--end", args.end, "End frame (inclusive, 0 = composition duration)")->default_val(0);
        cmd->add_option("--step", args.step, "Sample every N frames")->default_val(1);
        cmd->add_option("-o,--output", args.output, "Output file path (.json or .csv)");
        cmd->add_option("--format", args.format, "Export format: json or csv (auto-detected from -o extension)")->default_val("json");
        cmd->callback([state, &ctx]() { ctx.exit_code = command_camera_path(ctx.resources.registry, *state->args); });
    }

    // --- preflight ---
    {
        auto state = std::make_shared<PreflightState>();
        auto& args = *state->args;
        auto* cmd = app.add_subcommand("preflight", "Validate assets and requirements before rendering");
        cmd->add_option("id", args.comp_id, "Composition name")->required();
        cmd->add_option("--start", args.start, "Start frame")->default_val(0);
        cmd->add_option("--end", args.end, "End frame")->default_val(0);
        cmd->add_option("--sample-step", args.sample_step, "Sample every N frames");
        cmd->add_option("-o,--output", args.output, "Output file path (to check writability)");
        cmd->add_option("--json", args.json_file, "Path to output preflight JSON report");
        cmd->callback([state, &ctx]() { ctx.exit_code = command_preflight(ctx.resources.registry, *state->args); });
    }
}

} // namespace chronon3d::cli
