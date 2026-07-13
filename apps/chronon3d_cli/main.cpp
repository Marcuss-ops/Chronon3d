#include <cstdlib>
#include <string>
#include <thread>

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <spdlog/spdlog.h>
#include <tbb/global_control.h>

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/cpu_budget.hpp>
#include "cli_context.hpp"
#include "cli_init.hpp"
#include "commands/cli_groups.hpp"

int main(int argc, char** argv) {
    // Unified CPU budget: render/decode/encode thread counts are derived
    // from the hardware and the CHRONON3D_CPU_* environment variables.
    // TBB is capped to the render pool so that decode/encode threads do
    // not contend with the renderer.
    //
    // CHRONON3D_THREADS is preserved as a legacy override for the total
    // budget input (and therefore the render pool / TBB global limit).
    std::size_t total_threads = std::thread::hardware_concurrency();
    if (const char* env_threads = std::getenv("CHRONON3D_THREADS")) {
        char* end = nullptr;
        const long parsed = std::strtol(env_threads, &end, 10);
        if (parsed > 0 && end != env_threads && *end == '\0') {
            total_threads = static_cast<std::size_t>(parsed);
        }
    }

    const chronon3d::CpuBudget cpu_budget = chronon3d::cpu_budget_from_environment(
        static_cast<int>(total_threads));

    tbb::global_control tbb_control(
        tbb::global_control::max_allowed_parallelism,
        static_cast<std::size_t>(cpu_budget.render_threads)
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
    chronon3d::cli::CliContext ctx{registry, exit_code, std::move(cmd_line), assets, cpu_budget};

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