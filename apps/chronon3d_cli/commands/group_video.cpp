// ==============================================================================
// CLI Group: Video (video commands + FFmpeg utils)
// Depends: core, telemetry
// ==============================================================================
#include "command_registry.hpp"
#include "commands.hpp"

namespace chronon3d::cli::group_video {

void register_commands(CLI::App& app, CliContext& ctx) {
    register_video_commands(app, ctx);
}

} // namespace chronon3d::cli::group_video