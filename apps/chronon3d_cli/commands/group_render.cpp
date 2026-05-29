// ==============================================================================
// CLI Group: Render
// Contains: render, preflight, proofs, bake_layer, graph
// ==============================================================================
#include "command_registry.hpp"
#include "commands.hpp"

namespace chronon3d::cli::group_render {

void register_commands(CLI::App& app, CliContext& ctx) {
    register_render_commands(app, ctx);
    register_bake_layer_commands(app, ctx);
    // graph is registered by dev group but depends on render infrastructure
}

} // namespace chronon3d::cli::group_render