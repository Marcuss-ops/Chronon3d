#include "command_registry.hpp"

namespace chronon3d::cli {

void register_all_commands(CLI::App& app, CliContext& ctx) {
    register_basic_commands(app, ctx);
    register_render_commands(app, ctx);
    register_video_commands(app, ctx);
    register_create_commands(app, ctx);
    register_inspect_commands(app, ctx);
#ifdef CHRONON3D_BUILD_BENCHMARKS
    register_bench_commands(app, ctx);
#endif
    register_dev_commands(app, ctx);
#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
    register_telemetry_commands(app, ctx);
#endif
    register_bake_layer_commands(app, ctx);
}

} // namespace chronon3d::cli
