// ==============================================================================
// CLI Group: Dev
// Contains: batch, studio, inspect, watch, graph
// Depends: core, render
// ==============================================================================
#include "command_registry.hpp"
#include "commands.hpp"

namespace chronon3d::cli::group_dev {

void register_commands(CLI::App& app, CliContext& ctx) {
    register_dev_commands(app, ctx);
    register_inspect_commands(app, ctx);
}

} // namespace chronon3d::cli::group_dev