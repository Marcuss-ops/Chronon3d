#pragma once

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/cpu_budget.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/timeline/render_job.hpp>
#include <string>
#include <vector>
#include <filesystem>
#include "utils/common/cli_utils.hpp"

namespace chronon3d {
namespace cli {

struct RenderQualityArgs {
    // PR1 — motion blur is now a tri-state (Off | TemporalAccumulation |
    // VelocityApproximation). The `motion_blur` boolean is the convenient CLI
    // switch for TemporalAccumulation; `motion_blur_mode` selects an explicit
    // mode when finer control is required.
    //   motion_blur_mode: 0=Off, 1=TemporalAccumulation, 2=VelocityApproximation
    bool   motion_blur{false};
    int    motion_blur_mode{0};
    int    motion_blur_samples{8};
    float  shutter_angle_deg{180.0f};
    float  shutter_phase_deg{-90.0f};
    int    motion_blur_pattern{1};
    int    motion_blur_filter{0};
    float  ssaa{1.0f};
};

struct RenderPipelineArgs {
    bool   use_modular_graph{true};
    bool   diagnostic{false};
    bool   diagnostic_plan{false};
    std::string diagnostic_plan_output;
    int    tile_size{0};
    RenderQualityArgs quality{};

    bool   no_dirty_rects{false};

    bool   warmup_renderer{false};
    size_t warmup_framebuffers{2};
    bool   warmup_dummy_frame{false};

    bool   force_scalar_normal_blend{false};
    size_t fb_pool_budget_mb{0};
    std::string fb_pool_clear_policy;

    size_t program_cache_capacity{0};
    bool program_cache_tune{false};
    size_t program_cache_tune_interval{30};
    size_t program_cache_tune_min_capacity{2};
    size_t program_cache_tune_max_capacity{128};

    bool text_layout_debug{false};
    bool diagnostic_overlay{false};
    bool diagnostic_overlay_only{false};
    std::string text_layout_debug_json_path;
};

/// Canonical CLI render input for stills, sequences and video output.
/// Video options reuse the same VideoSettings value carried by RenderJob;
/// there is no CLI-only video argument mirror.
struct RenderArgs {
    std::string comp_id;
    std::string frames{"0"}; // Supports "0", "0-90", "0-90x5"
    std::string output;      // Output extension selects image or video mode
    RenderPipelineArgs pipeline{};
    VideoSettings video_settings{};
    std::string log_level{"info"};
    bool benchmark_all{false};
    bool report{false};
    std::string command_line;
    chronon3d::CpuBudget cpu_budget;
};

struct BenchConvertArgs {
    std::string comp_id;
    int frame{0};
    int iterations{10};
    std::string format{"yuv420p"};
    bool apply_gamma{true};
};

struct BenchArgs {
    std::string comp_id;
    int frames{100};
    int warmup{10};
    bool use_modular_graph{true};
    bool no_dirty_rects{false};

    std::string json_file;
    std::string compare_file;
    std::string stats_json_file;

    bool quiet{false};
    bool include_frame_times{false};
    double fail_if_avg_slower_pct{0.0};

    bool   warmup_renderer{false};
    size_t warmup_framebuffers{2};
    bool   warmup_dummy_frame{false};
};

struct GraphArgs {
    std::string comp_id;
    Frame frame{0};
    std::string output;
    bool summary{false};
    bool plan{false};
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
    bool legacy_preflight{false};
};

struct WatchArgs {
    std::string comp_id;
    int frame{0};
    std::filesystem::path output{"/tmp/preview.png"};
    std::vector<std::filesystem::path> watch_dirs;
    std::string build_command{"bash build-fast.sh"};
    int poll_ms{500};
    std::string chronon_binary;
    bool no_build{false};
    std::string props_file;
};

struct PreviewArgs {
    std::string comp_id;
    std::string frames{"0,30,60,90"};
    std::filesystem::path output_dir{"./preview"};
    std::string contact_sheet;
    int cell_width{640};
    int cell_padding{8};
    RenderPipelineArgs pipeline{};
    std::string log_level{"info"};
};

struct BakeLayerArgs {
    std::string comp_id;
    std::string layer_id;
    int frame{0};
    std::string output;
    bool quiet{false};
    bool diagnostic{false};
    bool diagnostic_overlay{false};
    bool diagnostic_overlay_only{false};
    bool exr_bake{false};
};

struct CameraPathArgs {
    std::string comp_id;
    Frame start{0};
    Frame end{0};
    int step{1};
    std::string output;
    std::string format{"auto"};
};

struct InspectTextArgs {
    std::string comp_id;
    Frame frame{0};
    bool json{true};
};

struct TextDefInspectArgs {
    std::string comp_id;
    Frame frame{0};
    std::string json_output;
};

int command_list(const CompositionRegistry& registry);
int command_daemon(const CompositionRegistry& registry,
                   const std::string& assets_root = "",
                   const std::string& build_command = "");
int command_info(const CompositionRegistry& registry, const std::string& id);
int command_doctor(const CompositionRegistry& registry);
int command_verify(const CompositionRegistry& registry, const std::string& output_dir);
int command_render(const CompositionRegistry& registry, const RenderArgs& args);
int command_bench_convert(const CompositionRegistry& registry, const BenchConvertArgs& args);
int command_bench(const CompositionRegistry& registry, const BenchArgs& args);
int command_graph(const CompositionRegistry& registry, const GraphArgs& args);
int command_batch(const CompositionRegistry& registry, const std::vector<std::string>& job_specs);
int command_telemetry(const TelemetryArgs& args);
int command_preflight(const CompositionRegistry& registry, const PreflightArgs& args, AssetRegistry& assets);
int command_watch(const CompositionRegistry& registry, const WatchArgs& args);
int command_preview(const CompositionRegistry& registry, const PreviewArgs& args);
int command_bake_layer(const CompositionRegistry& registry, const BakeLayerArgs& args);
int command_camera_path(const CompositionRegistry& registry, const CameraPathArgs& args);
int command_inspect_text(const CompositionRegistry& registry, const InspectTextArgs& args);
int command_text_def_inspect(const CompositionRegistry& registry, const TextDefInspectArgs& args);

} // namespace cli

struct CreateArgs {
    std::string name;
    std::string template_name{"basic"};
    bool force{false};
};

} // namespace chronon3d
