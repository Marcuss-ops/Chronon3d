#pragma once

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/cpu_budget.hpp>
#include <chronon3d/assets/asset_registry.hpp>

namespace chronon3d::cli {

struct CliContext {
    CompositionRegistry& registry;
    int& exit_code;
    std::string command_line;
    AssetRegistry& assets;
    chronon3d::CpuBudget cpu_budget;
};

} // namespace chronon3d::cli
