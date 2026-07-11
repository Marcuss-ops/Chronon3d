#include <cstdlib>
#include <string>
#include <thread>

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <spdlog/spdlog.h>
#include <tbb/global_control.h>

#include <chronon3d/core/composition/composition_registry.hpp>
#include "cli_context.hpp"
#include "cli_init.hpp"
#include "commands/cli_groups.hpp"

int main(int argc, char** argv) {
    // Ensure TBB uses all available hardware cores for maximum parallelism.
    // Without this, TBB's default thread count may be limited by the task_arena
    // or environment constraints, leading to underutilized cores.
    //
    // Tests can override this via the CHRONON3D_THREADS environment variable
    // (e.g. CHRONON3D_THREADS=1) to verify bit-exact output regardless of
    // parallelism.
    std::size_t thread_limit = std::thread::hardware_concurrency();
    if (const char* env_threads = std::getenv("CHRONON3D_THREADS")) {
        try {
            const long parsed = std::strtol(env_threads, nullptr, 10);
            if (parsed > 0) {
                thread_limit = static_cast<std::size_t>(parsed);
            }
        } catch (...) {
            // Ignore malformed env var; fall back to hardware concurrency.
        }
    }

    tbb::global_control tbb_control(
        tbb::global_control::max_allowed_parallelism,
        thread_limit
    );

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

    // Register content and built-in compositions into the registry.
    // (CompositionRegistry now starts empty — compositions are added
    //  explicitly via init_compositions()).
    chronon3d::CompositionRegistry registry;
    chronon3d::AssetRegistry assets;
    chronon3d::cli::init_compositions(registry, assets);
    int exit_code = 0;
    chronon3d::cli::CliContext ctx{registry, exit_code, std::move(cmd_line), assets};

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