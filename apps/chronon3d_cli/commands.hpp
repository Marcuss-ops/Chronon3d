#pragma once

#include <chronon3d/core/composition_registry.hpp>
#include <string>
#include <vector>

namespace chronon3d {
namespace cli {

struct FrameRange {
    int64_t start{0};
    int64_t end{0};
    int64_t step{1};
};

struct RenderArgs {
    std::string comp_id;
    std::string frames{"0"}; // Supports "0", "0-90", "0-90x5"
    std::string output{"render_####.png"};
    bool diagnostic{false};
    
    // Legacy support for basic CLI11 options
    int64_t start_old{-1};
    int64_t end_old{-1};
    int64_t frame_old{-1};
};

int command_list(const CompositionRegistry& registry);
int command_info(const CompositionRegistry& registry, const std::string& id);
int command_render(const CompositionRegistry& registry, const RenderArgs& args);
int command_batch(const CompositionRegistry& registry, const std::string& config_path);
int command_watch(const CompositionRegistry& registry, const std::string& comp_id);

// Internal helpers
FrameRange parse_frames(const std::string& s);
std::string format_path(const std::string& pattern, int64_t frame);

} // namespace cli
} // namespace chronon3d
