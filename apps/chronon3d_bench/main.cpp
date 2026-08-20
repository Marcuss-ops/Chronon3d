// =============================================================================
// apps/chronon3d_bench/main.cpp — standalone bench binary entry point.
//
// Extracted from apps/chronon3d_cli/commands/bench + apps/chronon3d_cli/commands/dev/command_bench_convert.cpp
// per the TICKET-CLI-ISOLATE-RUNTIME-DEV cleanup plan.
//
// Subcommands:
//   bench <comp_id> [--frames N] [--warmup N] [...]         (was: chronon3d_cli bench)
//   bench-convert <comp_id> [--frame N] [--iterations N]   (was: chronon3d_cli bench-convert)
//
// Gates:
//   CHRONON3D_BUILD_BENCHMARK_CLI (default OFF, this binary is OFF by default
//   to preserve fast default builds).
// =============================================================================

#include <cstdlib>
#include <string>

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/core/cpu_budget.hpp>

#include "cli_context.hpp"
#include "registry.hpp"

int main(int argc, char** argv) {
    chronon3d::CompositionRegistry registry;
    chronon3d::AssetRegistry assets;

    // Single CPU budget default — bench binary is single-purpose and does
    // not own a server-side scheduler.
    chronon3d::CpuBudget cpu_budget = chronon3d::cpu_budget_from_environment(
        static_cast<int>(std::thread::hardware_concurrency()));

    std::string cmd_line;
    for (int i = 0; i < argc; ++i) {
        cmd_line += argv[i];
        if (i < argc - 1) cmd_line += " ";
    }

    CLI::App app{"chronon3d_bench — standalone benchmark driver"};
    app.require_subcommand(1);

    int exit_code = 0;
    chronon3d::cli::CliContext ctx{registry, exit_code, std::move(cmd_line), assets, cpu_budget};

    chronon3d::cli::register_bench_commands(app, ctx);

    try {
        CLI11_PARSE(app, argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    spdlog::shutdown();
    return exit_code;
}
