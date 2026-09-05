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
#include <chronon3d/core/config.hpp>
#include <chronon3d/core/cpu_budget.hpp>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>
#include "cli_context.hpp"
#include "cli_init.hpp"
#include "commands/cli_groups.hpp"
#include "utils/process_start.hpp"

#ifdef CHRONON3D_ENABLE_CRASH_HANDLER
#include "src/core/crash/crash_handler.hpp"
#endif

int main(int argc, char** argv) {
    chronon3d::cli::record_process_start();
    chronon3d::cli::reset_startup_trace();

    {
        const auto t0 = chronon3d::profiling::now();
        (void)spdlog::default_logger();
        chronon3d::cli::startup_trace().logger_init_ms =
            chronon3d::profiling::duration_ms(t0, chronon3d::profiling::now());
    }
    const auto cli_bootstrap_t0 = chronon3d::profiling::now();

#ifdef CHRONON3D_ENABLE_CRASH_HANDLER
    // App-boundary opt-in: this getenv is intentionally not a runtime lookup.
    if (const char* env = std::getenv("CHRONON3D_DEV_CRASH_HANDLER")) {
        if (env[0] == '1' || env[0] == 'y' || env[0] == 'Y') {
            chronon3d::crash::install();
        }
    }
#endif

    // ── Single concurrency budget ───────────────────────────────────────
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

    // P2.11 — environment is resolved once at the application boundary.
    // TelemetryManager receives immutable values and does not call getenv()
    // from record/store code or any render-adjacent path.
    const chronon3d::Config process_config =
        chronon3d::Config::from_environment(cpu_budget);
    chronon3d::telemetry::TelemetryManager::instance().configure({
        .path_override = process_config.runtime().telemetry_path(),
        .default_directory = process_config.runtime().telemetry_default_directory(),
        .run_id_override = process_config.runtime().telemetry_run_id(),
        // Boundary-resolved capture level + Detailed/Trace retention window.
        // Summary (default) persists only durable rows; Detailed/Trace rows
        // older than the TTL are purged by the janitor at store init.
        .level = chronon3d::telemetry::parse_telemetry_level(
            process_config.runtime().telemetry_level()),
        .detail_ttl_days = chronon3d::telemetry::parse_retention_days(
            process_config.runtime().telemetry_detailed_ttl_days()),
    });

    tbb::global_control tbb_control(
        tbb::global_control::max_allowed_parallelism,
        static_cast<std::size_t>(cpu_budget.render_threads));

    std::string cmd_line;
    for (int i = 0; i < argc; ++i) {
        cmd_line += argv[i];
        if (i < argc - 1) cmd_line += " ";
    }

    CLI::App app{"Chronon3d CLI - Motion Graphics Engine"};
    app.require_subcommand(1);

    const auto composition_t0 = chronon3d::profiling::now();
    chronon3d::CompositionRegistry registry;
    chronon3d::AssetRegistry assets;
    chronon3d::cli::init_compositions(registry, assets);
    chronon3d::cli::startup_trace().composition_registration_ms =
        chronon3d::profiling::duration_ms(composition_t0, chronon3d::profiling::now());
    int exit_code = 0;
    auto video_runtimes = std::make_shared<chronon3d::media::VideoRuntimeRegistry>();
    chronon3d::cli::CliContext ctx{registry, exit_code, std::move(cmd_line), assets,
                                   cpu_budget, std::move(video_runtimes)};

    // Explicit group registration — only linked groups get registered.
    // Build profiles control which groups are compiled/linked:
    //   fast:        core only (list, info, doctor, verify)
    //   dev:         core + render + telemetry + dev
    //   dev-video:   core + render + telemetry + video + dev
    //   full:        all groups
    chronon3d::cli::register_all_groups(app, ctx);

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
