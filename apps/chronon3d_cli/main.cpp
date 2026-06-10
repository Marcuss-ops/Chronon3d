#include <string>

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <spdlog/spdlog.h>

#include <chronon3d/core/composition/composition_registry.hpp>
#include "cli_context.hpp"
#include "cli_init.hpp"
#include "commands/cli_groups.hpp"

int main(int argc, char** argv) {
    // Reconstruct command line into CliContext
    std::string cmd_line;
    for (int i = 0; i < argc; ++i) {
        cmd_line += argv[i];
        if (i < argc - 1) {
            cmd_line += " ";
        }
    }

    CLI::App app{"Chronon3d CLI - Motion Graphics Engine"};
    app.require_subcommand(1);

    // Initialize content ExtensionModules via the init hook
    // (Must happen before CompositionRegistry construction so module
    //  compositions are in the static builtin_composition_entries()
    //  when populate() is called.)
    chronon3d::cli::init_content_modules();

    chronon3d::CompositionRegistry registry;
    int exit_code = 0;
    chronon3d::cli::CliContext ctx{registry, exit_code, std::move(cmd_line)};

    // Explicit group registration — only linked groups get registered.
    // Build profiles control which groups are compiled/linked:
    //   fast:        core only (list, info, doctor, verify)
    //   dev:         core + render + telemetry + dev
    //   dev-video:   core + render + telemetry + video + dev
    //   full:        all groups
    chronon3d::cli::register_all_groups(app, ctx);

    // NOTE: -benchmark_all and -report are handled via CLI11 aliases
    // (see register_render_commands.cpp) — no argv mutation needed.

    try {
        CLI11_PARSE(app, argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    spdlog::shutdown();
    return exit_code;
}