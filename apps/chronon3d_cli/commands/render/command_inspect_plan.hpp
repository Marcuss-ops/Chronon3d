#pragma once

#include "../../cli_context.hpp"

#include <string>

namespace CLI { class App; }

namespace chronon3d::cli {

struct InspectPlanArgs {
    std::string plan_file;
    std::string assets_root;
    bool json{false};
};

/// `chronon inspect --plan <file>` — render the read-only resolved view of a
/// prepared render plan (human text or JSON).  Exits 0 on success, 1 when
/// preparation fails.  Never renders a frame and never feeds the inspection
/// back into the renderer.
int command_inspect_plan(const InspectPlanArgs& args);

void register_inspect_plan_command(CLI::App& app, CliContext& ctx);

} // namespace chronon3d::cli
