#include "../../command_registry.hpp"
#include "../../commands.hpp"
#include <memory>

namespace chronon3d::cli {

void register_telemetry_commands(CLI::App& app, CliContext& ctx) {
    auto args = std::make_shared<TelemetryArgs>();
    auto* cmd = app.add_subcommand("telemetry", "Report on or export from the local telemetry database");

    cmd->add_option("--run-id", args->run_id, "Specific run ID to report (defaults to latest)");
    cmd->add_option("-o,--output", args->output_file, "Output markdown file path (default: output/telemetry_report.md)");

    cmd->callback([args, &ctx]() {
        ctx.exit_code = command_telemetry(*args);
    });

    // Explicit compatibility export (SQLite -> JSONL).  Renders never write
    // JSONL by default; this is the only path that produces it, on request.
    auto export_args = std::make_shared<TelemetryExportArgs>();
    auto* export_cmd = cmd->add_subcommand("export", "Export a run from SQLite as one JSONL record");
    export_cmd->add_option("--run-id", export_args->run_id, "Specific run ID to export (defaults to latest)");
    export_cmd->add_option("-o,--output", export_args->output_file,
                           "Output file path (default: stdout)");
    export_cmd->callback([export_args, &ctx]() {
        ctx.exit_code = command_telemetry_export(*export_args);
    });
}

} // namespace chronon3d::cli
