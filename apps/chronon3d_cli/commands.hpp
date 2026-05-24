#pragma once

#include <chronon3d/core/composition_registry.hpp>
#include <chronon3d/core/frame.hpp>
#include <string>
#include <vector>
#include <filesystem>
#include "utils/common/cli_utils.hpp"

namespace chronon3d {
namespace cli {

struct RenderQualityArgs {
    bool   motion_blur{false};
    int    motion_blur_samples{8};
    float  shutter_angle{180.0f};
    float  ssaa{1.0f};
};

struct RenderPipelineArgs {
    bool   use_modular_graph{true};
    bool   diagnostic{false};
    bool   dirty_rects{false};
    RenderQualityArgs quality{};

    // Renderer warmup (preallocation + optional dummy frame)
    bool   warmup_renderer{false};
    size_t warmup_framebuffers{16};
    bool   warmup_dummy_frame{false};
};

struct RenderArgs {
    std::string comp_id;
    std::string frames{"0"}; // Supports "0", "0-90", "0-90x5"
    std::string output;      // No default
    std::string trace_file;  // Path to Chrome tracing JSON file
    RenderPipelineArgs pipeline{};
    std::string log_level{"info"};
    bool benchmark_all{false};
    bool report{false};
};

struct VideoArgs {
    std::string comp_id;
    std::string output;
    Frame start{0};
    Frame end{0};
    int fps{30};
    int crf{18};
    std::string codec{"auto"};
    std::string encode_preset{"veryfast"};
    bool keep_frames{false};
    RenderPipelineArgs pipeline{};
    std::string frames_dir;
    std::string hardware_encoder{"none"};
    int chunks{1};

    std::string ffmpeg_mode{"pipe"};
    bool ffmpeg_verbose{false};
    std::string pipe_pixfmt{"rgba"};
    std::string color_output{"srgb"};
    std::string pipe_writer{"classic"};
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
    RenderPipelineArgs pipeline{};
    std::string hardware_encoder{"none"};
};


struct BenchArgs {
    std::string comp_id;
    int frames{100};
    int warmup{10};
    bool use_modular_graph{true};
    bool dirty_rects{false};

    std::string json_file;
    std::string compare_file;

    bool quiet{false};
    bool include_frame_times{false};

    double fail_if_avg_slower_pct{0.0};
    double fail_if_p95_slower_pct{0.0};

    // Renderer warmup (preallocation + optional dummy frame)
    bool   warmup_renderer{false};
    size_t warmup_framebuffers{16};
    bool   warmup_dummy_frame{false};
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

struct TelemetryArgs {
    std::string run_id;
    std::string output_file;
};

struct PreflightArgs {
    std::string comp_id;
    Frame start{0};
    Frame end{0};
    int sample_step{1};
    std::string output;
    std::string json_file;
};

struct BakeLayerArgs {
    std::string comp_id;
    std::string layer_id;
    int frame{0};
    std::string output;
    bool quiet{false};
    bool diagnostic{false};
    bool exr_bake{false};
};

int command_list(const CompositionRegistry& registry);
int command_info(const CompositionRegistry& registry, const std::string& id);
int command_doctor(const CompositionRegistry& registry);
int command_verify(const CompositionRegistry& registry, const std::string& output_dir);
int command_render(const CompositionRegistry& registry, const RenderArgs& args);
int command_preview(const CompositionRegistry& registry, const RenderArgs& args);
int command_contact_sheet(const CompositionRegistry& registry, const RenderArgs& args);
int command_storyboard(const CompositionRegistry& registry, const RenderArgs& args);
int command_video(const CompositionRegistry& registry, const VideoArgs& args);
int command_video_camera(const CompositionRegistry& registry, const VideoCameraArgs& args);
int command_bench(const CompositionRegistry& registry, const BenchArgs& args);
int command_graph(const CompositionRegistry& registry, const GraphArgs& args);
int command_batch(const CompositionRegistry& registry, const std::vector<std::string>& job_specs);
int command_watch(const CompositionRegistry& registry, const std::string& comp_id);
int command_proofs(const CompositionRegistry& registry, const ProofsArgs& args);
int command_telemetry(const TelemetryArgs& args);
int command_preflight(const CompositionRegistry& registry, const PreflightArgs& args);
int command_bake_layer(const CompositionRegistry& registry, const BakeLayerArgs& args);

} // namespace cli
} // namespace chronon3d
