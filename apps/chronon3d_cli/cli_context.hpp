#pragma once

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/cpu_budget.hpp>
#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/media/video/video_device_runtime.hpp>
#include <memory>

namespace chronon3d::cli {

struct CliContext {
    CompositionRegistry& registry;
    int& exit_code;
    std::string command_line;
    AssetRegistry& assets;
    chronon3d::CpuBudget cpu_budget;
    // Process-lifetime registry for standalone CLI renders. The daemon
    // injects its own registry through VideoJobExecutionContext; the CLI
    // keeps this one alive across all commands/jobs in the process.
    std::shared_ptr<media::VideoRuntimeRegistry> video_runtimes;
};

} // namespace chronon3d::cli
