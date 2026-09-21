#pragma once

#include "command_render_plan.hpp"

#include <chronon3d/backends/software/software_renderer.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace chronon3d::cli {

struct RenderPlanState {
    std::string input;
    std::string prepared_package;
    std::string output;
    std::string assets_root;
    std::uint64_t start_frame{0};
    std::uint64_t end_frame{0};
    bool start_frame_explicit{false};
    bool end_frame_explicit{false};
    std::uint32_t fps_num{30};
    std::uint32_t fps_den{1};
    int crf{-1};
    std::string encode_preset;
    bool report{false};
    std::string backend{std::string{media::kDefaultBackend}};
    std::shared_ptr<SoftwareRenderer> warm_renderer;
    RenderPlanVideoOverrides video;
    std::string trace_output;
    std::string trace_level{"pipeline"};
    std::string gpu_hot_path_mode{"auto"};
    std::string video_sink;
    std::string receipt_verify;
    std::shared_ptr<media::VideoJobExecutionContext> video_execution;
    std::optional<runtime::job::NormalizedRenderJob> normalized_request;
};

runtime::job::RenderJobRequest make_render_job_request(const RenderPlanState& args);

int execute_render_plan(const CompositionRegistry& registry, RenderPlanState& args);

int execute_normalized_render_job(
    const CompositionRegistry& registry,
    runtime::job::NormalizedRenderJob job,
    std::shared_ptr<SoftwareRenderer> warm_renderer,
    std::shared_ptr<media::VideoJobExecutionContext> video_execution);

} // namespace chronon3d::cli
