#pragma once

#include <CLI/CLI.hpp>
#include "../command_registry.hpp"
#include "../commands.hpp"

namespace chronon3d::cli {

namespace group_core {
void register_commands(CLI::App& app, CliContext& ctx);
}
namespace group_render {
void register_commands(CLI::App& app, CliContext& ctx);
}
namespace group_video {
void register_commands(CLI::App& app, CliContext& ctx);
}
namespace group_telemetry {
void register_commands(CLI::App& app, CliContext& ctx);
}
namespace group_dev {
void register_commands(CLI::App& app, CliContext& ctx);
}
namespace group_bench {
void register_commands(CLI::App& app, CliContext& ctx);
}

/// Register exactly the command groups linked into the executable.  Feature
/// options such as CHRONON3D_ENABLE_VIDEO describe engine capability; they do
/// not prove that a CLI archive exists, so target-presence definitions are the
/// only valid gate here.
inline void register_all_groups(CLI::App& app, CliContext& ctx) {
    group_core::register_commands(app, ctx);
#ifdef CHRONON3D_HAS_CLI_RENDER
    group_render::register_commands(app, ctx);
#endif
#ifdef CHRONON3D_HAS_CLI_VIDEO
    group_video::register_commands(app, ctx);
#endif
#ifdef CHRONON3D_HAS_CLI_TELEMETRY
    group_telemetry::register_commands(app, ctx);
#endif
#ifdef CHRONON3D_HAS_CLI_DEV
    group_dev::register_commands(app, ctx);
#endif
#ifdef CHRONON3D_HAS_CLI_BENCH
    group_bench::register_commands(app, ctx);
#endif
}

} // namespace chronon3d::cli
