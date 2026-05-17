#pragma once

#include <chronon3d/core/composition_registry.hpp>
#include <chronon3d/core/frame.hpp>
#include <string>
#include <vector>
#include <filesystem>
#include "utils/cli_utils.hpp"

namespace chronon3d {
namespace cli {

struct RenderArgs {
    std::string comp_id;
    std::string frames{"0"}; // Supports "0", "0-90", "0-90x5"
    std::string output;      // No default
    bool diagnostic{false};
    bool use_modular_graph{false};
    bool   motion_blur{false};
    int    motion_blur_samples{8};
    float  shutter_angle{180.0f};
    float  ssaa{1.0f};
};

struct VideoArgs {
    std::string comp_id;
    std::string output;
    Frame start{0};
    Frame end{0};
    int fps{30};
    int crf{18};
    std::string codec{"auto"};
    std::string encode_preset{"medium"};
    bool keep_frames{false};
    bool use_modular_graph{false};
    bool   motion_blur{false};
    int    motion_blur_samples{8};
    float  shutter_angle{180.0f};
    float  ssaa{1.0f};
    std::string frames_dir;
};

struct VideoCameraArgs {
    std::string axis{"Tilt"};
    std::string reference_image{"assets/images/camera_reference.jpg"};
    std::string output;
    Frame start{0};
    Frame end{60};
    float roll_start_deg{-4.0f};
    float roll_end_deg{0.0f};
    int width{1280};
    int height{720};
    int fps{30};
    int crf{18};
    std::string codec{"auto"};
    std::string encode_preset{"medium"};
    bool use_modular_graph{false};
    bool   motion_blur{false};
    int    motion_blur_samples{8};
    float  shutter_angle{180.0f};
    float  ssaa{1.0f};
};


struct BenchArgs {
    std::string comp_id;
    int frames{100};
    int warmup{10};
    bool use_modular_graph{false};
};

struct GraphArgs {
    std::string comp_id;
    Frame frame{0};
    std::string output;   // optional .dot output path
    bool summary{false};  // print node/cache/timing diagnostics
};

struct ProofsArgs {
    std::string output_dir{"output/proofs"};
    float ssaa{1.0f};
};

int command_list(const CompositionRegistry& registry);
int command_info(const CompositionRegistry& registry, const std::string& id);
int command_doctor(const CompositionRegistry& registry);
int command_verify(const CompositionRegistry& registry, const std::string& output_dir);
int command_render(const CompositionRegistry& registry, const RenderArgs& args);
int command_video(const CompositionRegistry& registry, const VideoArgs& args);
int command_video_camera(const CompositionRegistry& registry, const VideoCameraArgs& args);
int command_bench(const CompositionRegistry& registry, const BenchArgs& args);
int command_graph(const CompositionRegistry& registry, const GraphArgs& args);
int command_batch(const CompositionRegistry& registry, const std::vector<std::string>& job_specs);
int command_watch(const CompositionRegistry& registry, const std::string& comp_id);
int command_proofs(const CompositionRegistry& registry, const ProofsArgs& args);

} // namespace cli
} // namespace chronon3d
