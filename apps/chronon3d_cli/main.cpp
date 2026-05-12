#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>
#include "commands.hpp"

using namespace chronon3d;
using namespace chronon3d::cli;

int main(int argc, char** argv) {
    CLI::App app{"Chronon3d CLI - Motion Graphics Engine"};
    app.require_subcommand(1);

    CompositionRegistry registry;
    register_all_compositions(registry);

    // List Command
    auto* list_cmd = app.add_subcommand("list", "List all registered compositions");
    list_cmd->callback([&registry]() {
        command_list(registry);
    });

    // Info Command
    std::string info_id;
    auto* info_cmd = app.add_subcommand("info", "Get detailed information about a composition");
    info_cmd->add_option("id", info_id, "ID of the composition")->required();
    info_cmd->callback([&registry, &info_id]() {
        command_info(registry, info_id);
    });

    // Render Command
    RenderArgs render_args;
    auto* render_cmd = app.add_subcommand("render", "Render a composition to image(s)");
    render_cmd->add_option("id", render_args.comp_id, "ID of the composition to render")->required();
    render_cmd->add_option("--frames", render_args.frames, "Frames to render (e.g. 0, 0-90, 0-90x5)");
    render_cmd->add_option("-o,--output", render_args.output, "Output path pattern (use #### for frame number)");
    render_cmd->add_flag("--diagnostic", render_args.diagnostic, "Enable diagnostic rendering overlays");
    
    // Legacy options for compatibility
    render_cmd->add_option("--frame", render_args.frame_old, "Single frame to render (legacy)");
    render_cmd->add_option("--start", render_args.start_old, "Start frame (legacy)");
    render_cmd->add_option("--end", render_args.end_old, "End frame (legacy)");
    
    render_cmd->callback([&registry, &render_args]() {
        command_render(registry, render_args);
    });

    // Batch Command
    std::string batch_config;
    auto* batch_cmd = app.add_subcommand("batch", "Run multiple rendering jobs from a config file");
    batch_cmd->add_option("--config", batch_config, "Path to TOML batch config")->required();
    batch_cmd->callback([&registry, &batch_config]() {
        command_batch(registry, batch_config);
    });

    // Watch Command
    std::string watch_id;
    auto* watch_cmd = app.add_subcommand("watch", "Watch for source changes, rebuild and re-render");
    watch_cmd->add_option("id", watch_id, "ID of the composition to watch")->required();
    watch_cmd->callback([&registry, &watch_id]() {
        command_watch(registry, watch_id);
    });

    // Render All Command
    std::string all_output_dir;
    auto* all_cmd = app.add_subcommand("render-all", "Render one frame of every registered composition for verification");
    all_cmd->add_option("-o,--output-dir", all_output_dir, "Directory to save renders")->default_val("output/verify");
    all_cmd->callback([&registry, &all_output_dir]() {
        for (const auto& id : registry.available()) {
            RenderArgs args;
            args.comp_id = id;
            args.frames = "0";
            args.output = all_output_dir + "/" + id + ".png";
            command_render(registry, args);
        }
    });

    try {
        CLI11_PARSE(app, argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    spdlog::shutdown();
    return 0;
}
