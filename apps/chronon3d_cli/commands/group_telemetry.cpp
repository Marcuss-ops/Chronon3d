// ==============================================================================
// CLI Group: Telemetry
// Depends: core
// ==============================================================================
#include "command_registry.hpp"
#include "commands.hpp"

namespace chronon3d::cli::group_telemetry {

void register_commands(CLI::App& app, CliContext& ctx) {
    register_telemetry_commands(app, ctx);
}

} // namespace chronon3d::cli::group_telemetry