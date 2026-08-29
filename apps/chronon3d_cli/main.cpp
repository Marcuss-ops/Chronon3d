#include <cstdlib>
#include <algorithm>
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
#include "utils/process_start.hpp"

#ifdef CHRONON3D_ENABLE_CRASH_HANDLER
#include "src/core/crash/crash_handler.hpp"
#endif

int main(int argc, char** argv) {
    chronon3d::cli::record_process_start();

    // Force construction of the default logger at the process boundary so
    // logger setup is measured separately from CLI parsing and registration.
    {
        const auto t0 = chronon3d::profiling::now();
        (void)spdlog::default_logger();
        chronon3d::cli::startup_trace().logger_init_ms =
            chronon3d::profiling::duration_ms(t0, chronon3d::profiling::now());
    }
    const auto cli_bootstrap_t0 = chronon3d::profiling::now();

#ifdef CHRONON3D_ENABLE_CRASH_HANDLER
    // Optional dev-mode crash handler.  A library must NOT install signal
    // handlers in a client process; the CLI is an app boundary, so it may —
    // but only when the developer explicitly opts in (keeps default CLI
    // behavior identical to before).
    if (const char* env = std::getenv("CHRONON3D_DEV_CRASH_HANDLER")) {
        if (env[0] == '1' || env[0] == 'y' || env[0] == 'Y') {
            chronon3d::crash::install();
        }
    }
#endif

    // ── Single concurrency budget ───────────────────────────────────────
    //
    // Architecture (certified by tests/core/test_concurrency_budget.cpp):
    //
    //   CpuBudget (render/decode/encode split)  ← single authority
    //       ↓
    //   tbb::global_control(max_allowed_parallelism, render_threads)  ← global cap
    //       ↓
    //   ExecutionScheduler::task_arena(slots = render_threads)  ← single arena
    //       ↓
    //   All tbb::parallel_for / for_each_tile calls go through the arena
    //
    // No oversubscription: frames are rendered sequentially (one frame
    // thread).  All internal parallelism (tiles, effects, composite,
    // blur, transform) uses the SAME capped TBB arena — no "N frame
    // threads × T TBB workers = N*T explosion" possible.
    //
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
    const auto composition_t0 = chronon3d::profiling::now();
    chronon3d::CompositionRegistry registry;
    chronon3d::AssetRegistry assets;
    chronon3d::cli::init_compositions(registry, assets);
    chronon3d::cli::startup_trace().composition_registration_ms =
        chronon3d::profiling::duration_ms(composition_t0, chronon3d::profiling::now());
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
        chronon3d::cli::startup_trace().cli_bootstrap_ms =
            std::max(0.0, chronon3d::profiling::duration_ms(
                cli_bootstrap_t0, chronon3d::profiling::now()) -
                chronon3d::cli::startup_trace().composition_registration_ms);
        const auto parse_t0 = chronon3d::profiling::now();
        CLI11_PARSE(app, argc, argv);
        chronon3d::cli::startup_trace().cli_parse_ms =
            chronon3d::profiling::duration_ms(parse_t0, chronon3d::profiling::now());
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    spdlog::shutdown();
    return exit_code;
}
