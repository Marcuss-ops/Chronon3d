#pragma once

#include <chronon3d/core/composition_registry.hpp>
#include <chronon3d/core/frame.hpp>
#include <string>
#include <vector>
#include <filesystem>
#include "cli_utils.hpp"

namespace chronon3d {
namespace cli {

struct RenderArgs {
    std::string comp_id;
    std::string frames{"0"}; // Supports "0", "0-90", "0-90x5"
    std::string output{"render_####.png"};
    bool diagnostic{false};
    bool use_modular_graph{false};
    bool   motion_blur{false};
    int    motion_blur_samples{8};
    float  shutter_angle{180.0f};
    float  ssaa{1.0f};


    // Legacy support
    int64_t start_old{-1};
    int64_t end_old{-1};
    int64_t frame_old{-1};
};

struct VideoArgs {
    std::string comp_id;
    std::string output;
    Frame start{0};
    Frame end{0};
    int fps{30};
    int crf{18};
    std::string preset{"medium"};
    bool keep_frames{false};
    bool use_modular_graph{false};
    bool   motion_blur{false};
    int    motion_blur_samples{8};
    float  shutter_angle{180.0f};
    float  ssaa{1.0f};
    std::string frames_dir;
};


struct BenchArgs {
    std::string comp_id;
    int frames{120};
    int warmup{10};
    bool use_modular_graph{false};
};

struct GraphArgs {
    std::string comp_id;
    Frame frame{0};
    std::string output{"output/graph.dot"};
};

int command_list(const CompositionRegistry& registry);
int command_info(const CompositionRegistry& registry, const std::string& id);
int command_render(const CompositionRegistry& registry, const RenderArgs& args);
int command_video(const CompositionRegistry& registry, const VideoArgs& args);
int command_bench(const CompositionRegistry& registry, const BenchArgs& args);
int command_graph(const CompositionRegistry& registry, const GraphArgs& args);
int command_batch(const CompositionRegistry& registry, const std::string& config_path);
int command_watch(const CompositionRegistry& registry, const std::string& comp_id);

} // namespace cli
} // namespace chronon3d
