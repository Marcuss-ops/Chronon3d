#include "../command_registry.hpp"
#include "../commands.hpp"
#include <spdlog/spdlog.h>
#include <memory>

namespace chronon3d::cli {

namespace {
struct RenderState { std::shared_ptr<RenderArgs> args{std::make_shared<RenderArgs>()}; };
}

void register_render_commands(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<RenderState>();
    auto& args = *state->args;

    auto* cmd = app.add_subcommand("render", "Render a composition id or .specscene file to image(s)");
    cmd->add_option("input", args.comp_id, "Composition name or .specscene path")->required();
    cmd->add_option("--frames", args.frames, "Frame range: 0 | 0-90 | 0-90x5");
    cmd->add_option("-o,--output", args.output, "Output path (use #### for frame number)");
    cmd->add_option("--trace", args.trace_file, "Path to output Chrome performance trace JSON file");
    cmd->add_flag("--diagnostic", args.pipeline.diagnostic, "Enable diagnostic overlays");
    cmd->add_flag("--graph", args.pipeline.use_modular_graph, "Use modular RenderGraph path");
    cmd->add_flag("--motion-blur", args.pipeline.quality.motion_blur, "Enable temporal motion blur");
    cmd->add_option("--motion-blur-samples", args.pipeline.quality.motion_blur_samples, "Subframe samples (default 8)");
    cmd->add_option("--shutter-angle", args.pipeline.quality.shutter_angle, "Shutter angle in degrees (default 180)");
    cmd->add_option("--ssaa", args.pipeline.quality.ssaa, "Super Sampling factor (default 1.0)");
    cmd->add_option("-v,--log-level", args.log_level, "Log level: trace | debug | info | warn | error");
    cmd->add_flag("--benchmark_all", args.benchmark_all, "Write detailed phase durations for all graph nodes");
    cmd->add_flag("--report", args.report, "Generate an execution report log");
    cmd->allow_windows_style_options();
    cmd->callback([state, &ctx]() {
        if (state->args->output.empty()) {
            state->args->output = "render_####.png";
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
