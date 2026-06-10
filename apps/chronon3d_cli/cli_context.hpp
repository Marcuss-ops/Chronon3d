#pragma once

#include <chronon3d/core/composition/composition_registry.hpp>

namespace chronon3d::cli {

struct CliContext {
    CompositionRegistry& registry;
    int& exit_code;
    std::string command_line;
};

} // namespace chronon3d::cli
