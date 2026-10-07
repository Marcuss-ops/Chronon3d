#pragma once

#include "../../cli_context.hpp"

#include <string>

namespace CLI { class App; }

namespace chronon3d::cli {

struct InspectPlanArgs {
    std::string plan_file;
    std::string assets_root;
    bool json{false};
    bool report_temporal_rates{false};
};

/// `chronon inspect --plan <file>` — show the resolved plan or a pure temporal
/// rate report. Report mode only reads/decodes JSON and never resolves assets,
/// prepares, compiles, or renders. Exits 1 for read/decode failures.
int command_inspect_plan(const InspectPlanArgs& args);

void register_inspect_plan_command(CLI::App& app, CliContext& ctx);

} // namespace chronon3d::cli
