#include <chronon3d/chronon3d.hpp>
#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <spdlog/spdlog.h>
#include "commands/cli_groups.hpp"
#ifdef CHRONON3D_BUILD_CONTENT
#include <content/register_content_modules.hpp>
#endif

using namespace chronon3d;
using namespace chronon3d::cli;

namespace chronon3d::cli {
    std::string g_command_line;
}

int main(int argc, char** argv) {
    // Reconstruct command line
    for (int i = 0; i < argc; ++i) {
        g_command_line += argv[i];
        if (i < argc - 1) {
            g_command_line += " ";
        }
    }

    CLI::App app{"Chronon3d CLI - Motion Graphics Engine"};
    app.require_subcommand(1);

    // Initialize content ExtensionModules (Minimalist, Text, 2D5, etc.)
    // MUST happen before CompositionRegistry construction so module compositions
    // are in the static builtin_composition_entries() when populate is called.
#ifdef CHRONON3D_BUILD_CONTENT
    register_content_modules();
#endif

    CompositionRegistry registry;
    int exit_code = 0;
    CliContext ctx{registry, exit_code};

    // Explicit group registration — only linked groups get registered.
    // Build profiles control which groups are compiled/linked:
    //   fast:        core only (list, info, doctor, verify)
    //   dev:         core + render + telemetry + dev
    //   dev-video:   core + render + telemetry + video + dev
    //   full:        all groups
    register_all_groups(app, ctx);

    // Preprocess argv in place
    for (int i = 0; i < argc; ++i) {
        if (std::strcmp(argv[i], "-benchmark_all") == 0) {
            argv[i] = const_cast<char*>("--benchmark_all");
        } else if (std::strcmp(argv[i], "-report") == 0) {
            argv[i] = const_cast<char*>("--report");
        }
    }

    try {
        CLI11_PARSE(app, argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    spdlog::shutdown();
    return exit_code;
}