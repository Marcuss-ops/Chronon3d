#pragma once

#include <chronon3d/core/composition_registry.hpp>
#include <string>
#include <vector>

namespace chronon3d {
namespace cli {

struct RenderArgs {
    std::string comp_id;
    int64_t start{-1};
    int64_t end{-1};
    int64_t frame{-1};
    std::string output{"render.png"};
};

int command_list(const CompositionRegistry& registry);
int command_info(const CompositionRegistry& registry, const std::string& id);
int command_render(const CompositionRegistry& registry, const RenderArgs& args);

} // namespace cli
} // namespace chronon3d
