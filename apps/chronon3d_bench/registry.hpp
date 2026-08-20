#pragma once

#include <CLI/App.hpp>
#include "cli_context.hpp"

namespace chronon3d::cli {

void register_bench_commands(CLI::App& app, CliContext& ctx);

} // namespace chronon3d::cli
