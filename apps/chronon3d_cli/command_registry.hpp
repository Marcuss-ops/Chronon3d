#pragma once
#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/sdk/render_output.hpp>
#include <chronon3d/sdk/render_error.hpp>
#include <chronon3d/sdk/render_request.hpp>
#include <chronon3d/sdk/render_settings.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <CLI/App.hpp>
#include "cli_context.hpp"

namespace chronon3d::cli {

void register_render_commands(CLI::App& app, CliContext& ctx);
void register_validate_commands(CLI::App& app, CliContext& ctx);
void register_video_commands(CLI::App& app, CliContext& ctx);
void register_dev_commands(CLI::App& app, CliContext& ctx);
void register_inspect_commands(CLI::App& app, CliContext& ctx);
// TICKET-V3-CLI-UNIFICATION-STARTER-TEMPLATE (Blocco 4.2)
void register_create_commands(CLI::App& app, CliContext& ctx);
// TICKET-V3-CLI-UNIFICATION-WATCH-SUPERVISOR (Blocco 4.1)
void register_watch_commands(CLI::App& app, CliContext& ctx);
// TICKET-V3-CLI-UNIFICATION-PREVIEW (Blocco 4.1)
void register_preview_commands(CLI::App& app, CliContext& ctx);


#ifdef CHRONON3D_BUILD_BENCHMARKS
void register_bench_commands(CLI::App& app, CliContext& ctx);
#endif
void register_telemetry_commands(CLI::App& app, CliContext& ctx);
void register_bake_layer_commands(CLI::App& app, CliContext& ctx);

} // namespace chronon3d::cli
