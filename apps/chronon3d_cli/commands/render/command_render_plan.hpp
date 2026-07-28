#pragma once

#include "../../cli_context.hpp"

#include <string>

namespace CLI { class App; }

namespace chronon3d::cli {

int run_render_plan_file(CliContext& ctx,
                         const std::string& input,
                         const std::string& output = {});
void register_render_plan_command(CLI::App& app, CliContext& ctx);

}  // namespace chronon3d::cli
