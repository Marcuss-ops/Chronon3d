#pragma once

#include <chronon3d/chronon3d.hpp>
#include <CLI/App.hpp>
#include "cli_context.hpp"

namespace chronon3d::cli {

void register_basic_commands(CLI::App& app, CliContext& ctx);
void register_render_commands(CLI::App& app, CliContext& ctx);
void register_video_commands(CLI::App& app, CliContext& ctx);
void register_dev_commands(CLI::App& app, CliContext& ctx);
void register_inspect_commands(CLI::App& app, CliContext& ctx);
void register_bench_commands(CLI::App& app, CliContext& ctx);
void register_telemetry_commands(CLI::App& app, CliContext& ctx);

void register_all_commands(CLI::App& app, CliContext& ctx);

} // namespace chronon3d::cli
