#include "../command_registry.hpp"
#include "../commands.hpp"
#include <spdlog/spdlog.h>
#include <memory>

namespace chronon3d::cli {

namespace {

struct RenderState { std::shared_ptr<RenderArgs> args{std::make_shared<RenderArgs>()}; };
struct BenchState { std::shared_ptr<BenchArgs> args{std::make_shared<BenchArgs>()}; };
struct GraphState { std::shared_ptr<GraphArgs> args{std::make_shared<GraphArgs>()}; };
struct ProofsState { std::shared_ptr<ProofsArgs> args{std::make_shared<ProofsArgs>()}; };

void register_render(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<RenderState>();
    auto& args = *state->args;

    auto* cmd = app.add_subcommand("render", "Render a composition id or .specscene file to image(s)");
    cmd->add_option("input", args.comp_id, "Composition name or .specscene path")->required();
    cmd->add_option("--frames", args.frames, "Frame range: 0 | 0-90 | 0-90x5");
    cmd->add_option("-o,--output", args.output, "Output path (use #### for frame number)");
    cmd->add_flag("--diagnostic", args.pipeline.diagnostic, "Enable diagnostic overlays");
    cmd->add_flag("--graph", args.pipeline.use_modular_graph, "Use modular RenderGraph path");
    cmd->add_flag("--motion-blur", args.pipeline.quality.motion_blur, "Enable temporal motion blur");
    cmd->add_option("--motion-blur-samples", args.pipeline.quality.motion_blur_samples, "Subframe samples (default 8)");
    cmd->add_option("--shutter-angle", args.pipeline.quality.shutter_angle, "Shutter angle in degrees (default 180)");
    cmd->add_option("--ssaa", args.pipeline.quality.ssaa, "Super Sampling factor (default 1.0)");
    cmd->callback([state, &ctx]() {
        if (state->args->output.empty()) {
            state->args->output = "render_####.png";
            spdlog::warn("No output path specified, defaulting to {}", state->args->output);
        }
        ctx.exit_code = command_render(ctx.registry, *state->args);
    });
}

void register_bench(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<BenchState>();
    auto& args = *state->args;
    auto* cmd = app.add_subcommand("bench", "Benchmark a composition in-memory");
    cmd->add_option("id", args.comp_id, "Composition name")->required();
    cmd->add_option("--frames", args.frames, "Measured frames")->default_val(120);
    cmd->add_option("--warmup", args.warmup, "Warmup frames")->default_val(10);
    cmd->add_flag("--graph", args.use_modular_graph, "Use modular RenderGraph path");
    cmd->callback([state, &ctx]() { ctx.exit_code = command_bench(ctx.registry, *state->args); });
}

void register_graph(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<GraphState>();
    auto& args = *state->args;
    auto* cmd = app.add_subcommand("graph", "Inspect the render graph: summary stats or DOT export");
    cmd->add_option("id", args.comp_id, "Composition name or .specscene path")->required();
    cmd->add_option("--frame", args.frame, "Frame to inspect")->default_val(0);
    cmd->add_option("-o,--output", args.output, "Output .dot file path");
    cmd->add_flag("--summary", args.summary, "Print node counts, cache stats, and timing");
    cmd->callback([state, &ctx]() {
        if (!state->args->summary && state->args->output.empty()) {
            state->args->output = "output/graph.dot";
            spdlog::warn("No --output and no --summary; defaulting to {}", state->args->output);
        }
        ctx.exit_code = command_graph(ctx.registry, *state->args);
    });
}

void register_proofs(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<ProofsState>();
    auto& args = *state->args;
    auto* cmd = app.add_subcommand("proofs", "Render all 7 diagnostic proof compositions to PNG snapshots");
    cmd->add_option("-o,--out", args.output_dir, "Output directory")->default_val("output/proofs");
    cmd->add_option("--ssaa", args.ssaa, "Super sampling factor")->default_val(1.0f);
    cmd->callback([state, &ctx]() { ctx.exit_code = command_proofs(ctx.registry, *state->args); });
}

void register_preview(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<RenderState>();
    auto& args = *state->args;
    auto* cmd = app.add_subcommand("preview", "Render a single frame of a composition (default: middle frame)");
    cmd->add_option("input", args.comp_id, "Composition name or .specscene path")->required();
    cmd->add_option("--frame", args.frames, "Frame number to preview (default: middle frame)");
    cmd->add_option("-o,--output", args.output, "Output image path")->default_val("preview.png");
    cmd->add_flag("--diagnostic", args.pipeline.diagnostic, "Enable diagnostic overlays");
    cmd->add_flag("--graph", args.pipeline.use_modular_graph, "Use modular RenderGraph path");
    cmd->callback([state, &ctx]() {
        ctx.exit_code = command_preview(ctx.registry, *state->args);
    });
}

void register_contact_sheet(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<RenderState>();
    auto& args = *state->args;
    auto* cmd = app.add_subcommand("contact-sheet", "Render 4 frames stitched horizontally into a contact sheet");
    cmd->add_option("input", args.comp_id, "Composition name or .specscene path")->required();
    cmd->add_option("-o,--output", args.output, "Output image path")->default_val("contact_sheet.png");
    cmd->add_flag("--diagnostic", args.pipeline.diagnostic, "Enable diagnostic overlays");
    cmd->add_flag("--graph", args.pipeline.use_modular_graph, "Use modular RenderGraph path");
    cmd->callback([state, &ctx]() {
        ctx.exit_code = command_contact_sheet(ctx.registry, *state->args);
    });
}

void register_storyboard(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<RenderState>();
    auto& args = *state->args;
    auto* cmd = app.add_subcommand("storyboard", "Render 6 frames in a 2x3 grid with overlays");
    cmd->add_option("input", args.comp_id, "Composition name or .specscene path")->required();
    cmd->add_option("-o,--output", args.output, "Output image path")->default_val("storyboard.png");
    cmd->add_flag("--diagnostic", args.pipeline.diagnostic, "Enable diagnostic overlays");
    cmd->add_flag("--graph", args.pipeline.use_modular_graph, "Use modular RenderGraph path");
    cmd->callback([state, &ctx]() {
        ctx.exit_code = command_storyboard(ctx.registry, *state->args);
    });
}

} // namespace

void register_render_commands(CLI::App& app, CliContext& ctx) {
    register_render(app, ctx);
    register_preview(app, ctx);
    register_contact_sheet(app, ctx);
    register_storyboard(app, ctx);
    register_bench(app, ctx);
    register_graph(app, ctx);
    register_proofs(app, ctx);
}

} // namespace chronon3d::cli
