#pragma once

#include "../../cli_context.hpp"
#include "../../daemon/chronon_ipc.hpp"
#include <chronon3d/media/video/video_job_execution_context.hpp>

#include <cstdint>
#include <string>
#include <memory>

namespace chronon3d { class SoftwareRenderer; }

namespace CLI { class App; }

namespace chronon3d::cli {

struct RenderPlanVideoOverrides {
    std::string codec;
    std::string hardware_encoder;
    std::string encoder_backend;
    std::string ffmpeg_mode;
    std::string encode_preset;
    std::string rate_control_mode;
    int crf{-1};
    int qp{-1};
    std::int64_t bitrate{0};
    std::string gop_source;
    bool gop_copy_only{false};
};

int run_render_plan_file(const CompositionRegistry& registry,
                         const std::string& input,
                         const std::string& output = {},
                         const std::string& assets_root = {},
                         bool report = false,
                         std::shared_ptr<SoftwareRenderer> warm_renderer = {},
                         const std::string& backend = "auto",
                         RenderPlanVideoOverrides video = {},
                         const std::string& trace_output = {},
                         const std::string& trace_level = "pipeline",
                         const std::string& gpu_hot_path_mode = "auto",
                         std::shared_ptr<media::VideoJobExecutionContext> video_execution = {});
void register_render_plan_command(CLI::App& app, CliContext& ctx);

/// RENDER_JOB (daemon IPC): render a chronon.render-plan.v1 file. The payload
/// is JSON {"plan_path", "assets_root", "output"}; on success the reply is
/// {"status":"ok","output":"..."}.
ipc::Reply ipc_render_job(const CompositionRegistry& registry,
                          const std::string& payload);
ipc::Reply ipc_render_job(const CompositionRegistry& registry,
                          const std::string& payload,
                          std::shared_ptr<SoftwareRenderer> warm_renderer,
                          std::shared_ptr<media::VideoJobExecutionContext> video_execution = {});

}  // namespace chronon3d::cli
