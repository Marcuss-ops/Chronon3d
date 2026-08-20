#pragma once

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/cpu_budget.hpp>
#include <chronon3d/assets/asset_registry.hpp>

// Local copy of apps/chronon3d_cli/cli_context.hpp.  The bench binary keeps
// the same CliContext shape (so existing implementations of command_bench /
// command_bench_convert compile without signature rewrites).  Only the
// `assets` field is unused by the bench code path; the real AssetRegistry
// is constructed in main.cpp.

namespace chronon3d::cli {

struct CliContext {
    CompositionRegistry& registry;
    int& exit_code;
    std::string command_line;
    AssetRegistry& assets;
    chronon3d::CpuBudget cpu_budget;
};

} // namespace chronon3d::cli
