#include "command_registry.hpp"

namespace chronon3d::cli {

void register_all_commands(CLI::App& app, CliContext& ctx) {
    register_basic_commands(app, ctx);
    register_render_commands(app, ctx);
    register_video_commands(app, ctx);
    register_dev_commands(app, ctx);
}

} // namespace chronon3d::cli
