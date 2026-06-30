// ==============================================================================
// Explicit CLI Group Registration
// Each command group exports a register function that main.cpp calls.
// This replaces the monolithic register_all_commands() approach with
// per-group explicit registration, enabling fine-grained linker control.
// ==============================================================================
#pragma once

#include <CLI/CLI.hpp>
#include "../command_registry.hpp"
#include "../commands.hpp"

namespace chronon3d::cli {

// Each group implements register_commands(CLI::App&, CliContext&)
// using the existing register_*_commands functions.
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

// Master registration — calls all groups in dependency order.
// Exposed as inline for convenience; each group is a separate target anyway.
inline void register_all_groups(CLI::App& app, CliContext& ctx) {
    group_core::register_commands(app, ctx);
    group_render::register_commands(app, ctx);
#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
    group_telemetry::register_commands(app, ctx);
#endif
#ifdef CHRONON3D_ENABLE_VIDEO
    group_video::register_commands(app, ctx);
#endif
#ifdef CHRONON3D_BUILD_CLI_DEV
    group_dev::register_commands(app, ctx);  // link-clean under -DCHRONON3D_BUILD_CLI_DEV=OFF
#endif
#ifdef CHRONON3D_BUILD_BENCHMARKS
    group_bench::register_commands(app, ctx);
#endif
}

} // namespace chronon3d::cli