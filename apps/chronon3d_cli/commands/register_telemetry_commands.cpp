#include "../command_registry.hpp"
#include "../commands.hpp"
#include <memory>

namespace chronon3d::cli {

void register_telemetry_commands(CLI::App& app, CliContext& ctx) {
#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
    auto args = std::make_shared<TelemetryArgs>();
    auto* cmd = app.add_subcommand("telemetry", "Generate a markdown report from the local telemetry database");
    
    cmd->add_option("--run-id", args->run_id, "Specific run ID to report (defaults to latest)");
    cmd->add_option("-o,--output", args->output_file, "Output markdown file path (default: output/telemetry_report.md)");
    
    cmd->callback([args, &ctx]() {
        ctx.exit_code = command_telemetry(*args);
    });
#else
    (void)app;
    (void)ctx;
#endif
}

} // namespace chronon3d::cli
