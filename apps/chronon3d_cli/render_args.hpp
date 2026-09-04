#pragma once

#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/cpu_budget.hpp>
#include <chronon3d/core/gpu_hot_path_mode.hpp>
#include <chronon3d/render_graph/backend_selection.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/timeline/render_job.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d::cli {

struct RenderQualityArgs {
    bool motion_blur{false};
    MotionBlurMode motion_blur_mode{MotionBlurMode::Off};
    int motion_blur_samples{8};
    float shutter_angle_deg{180.0f};
    float shutter_phase_deg{-90.0f};
    TemporalSamplePattern motion_blur_pattern{TemporalSamplePattern::Stratified};
    TemporalFilter motion_blur_filter{TemporalFilter::Box};
    float ssaa{1.0f};
};

struct RenderPipelineArgs {
    bool diagnostic{false};
    bool diagnostic_plan{false};
    std::string diagnostic_plan_output;
    int tile_size{0};
    RenderQualityArgs quality{};

    bool no_dirty_rects{false};

    bool warmup_renderer{false};
    size_t warmup_framebuffers{2};
    bool warmup_dummy_frame{false};

    bool force_scalar_normal_blend{false};
    size_t fb_pool_budget_mb{0};
    std::optional<cache::FramebufferPoolClearPolicy> fb_pool_clear_policy;

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

/// Runtime-facing render arguments. Values with canonical runtime enums are
/// parsed exactly once by the CLI registration boundary before this value is
/// passed to job construction.
struct RenderArgs {
    std::string comp_id;
    std::string frames{"0"};
    std::string output;
    std::string assets_root;
    graph::BackendPreference backend{graph::BackendPreference::Auto};
    RenderPipelineArgs pipeline{};
    VideoSettings video_settings{};
    bool pipe_pixfmt_explicit{false};
    // P2.4 follow-up: trace/log/pixel-format remain stringly because their
    // current canonical runtime carriers are still strings. Do not add a
    // second CLI-only enum; migrate the canonical carrier first.
    std::string log_level{"info"};
    bool benchmark_all{false};
    bool report{false};
    std::string command_line;
    CpuBudget cpu_budget;
    std::string trace_output;
    std::string trace_level{"pipeline"};
    GpuHotPathMode gpu_hot_path_mode{GpuHotPathMode::Auto};
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
    std::string assets_root;
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
    std::string assets_root;
    Frame start{0};
    Frame end{0};
    int step{1};
    std::string output;
    std::string format{"auto"};
};

} // namespace chronon3d::cli
