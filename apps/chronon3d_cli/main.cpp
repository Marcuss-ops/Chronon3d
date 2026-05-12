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
    render_cmd->add_option("--frame", render_args.frame, "Single frame to render");
    render_cmd->add_option("--start", render_args.start, "Start frame of the range");
    render_cmd->add_option("--end", render_args.end, "End frame of the range");
    render_cmd->add_option("-o,--output", render_args.output, "Output path or pattern");
    
    render_cmd->callback([&registry, &render_args]() {
        command_render(registry, render_args);
    });

    try {
        CLI11_PARSE(app, argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    spdlog::shutdown();
    return 0;
}
