#pragma once

#include "../../cli_context.hpp"
#include "../../daemon/chronon_ipc.hpp"

#include <string>

namespace CLI { class App; }

namespace chronon3d::cli {

int run_render_plan_file(const CompositionRegistry& registry,
                         const std::string& input,
                         const std::string& output = {},
                         const std::string& assets_root = {});
void register_render_plan_command(CLI::App& app, CliContext& ctx);

/// RENDER_JOB (daemon IPC): render a chronon.render-plan.v1 file. The payload
/// is JSON {"plan_path", "assets_root", "output"}; on success the reply is
/// {"status":"ok","output":"..."}.
ipc::Reply ipc_render_job(const CompositionRegistry& registry,
                          const std::string& payload);

}  // namespace chronon3d::cli
