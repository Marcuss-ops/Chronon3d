#pragma once

#include "../../cli_context.hpp"
#include "../../daemon/chronon_ipc.hpp"
#include <chronon3d/media/video/video_job_execution_context.hpp>
#include <chronon3d/runtime/job/render_job_request.hpp>

#include <cstdint>
#include <string>
#include <memory>
#include <optional>

namespace chronon3d { class SoftwareRenderer; }

namespace CLI { class App; }

namespace chronon3d::cli {

struct RenderPlanVideoOverrides {
    std::string codec;
    /// Requested output audio sample rate (Hz) for the muxed master audio.
    /// 0 keeps the source rate (copy semantics).
    int audio_sample_rate{0};
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
    std::string pipe_pixfmt;
};

int run_render_plan_file(
    const CompositionRegistry& registry,
    runtime::job::RenderJobRequest request,
    std::shared_ptr<SoftwareRenderer> warm_renderer = {},
    std::shared_ptr<media::VideoJobExecutionContext> video_execution = {});
void register_render_plan_command(CLI::App& app, CliContext& ctx);

/// RENDER_JOB (daemon IPC): execute an already-normalized render job. Phase 9
/// (CLI/IPC parity): the daemon boundary decoded + normalized the wire JSON
/// exactly once; the render group receives the immutable NormalizedRenderJob
/// and never re-parses JSON nor re-normalizes. On success the reply is
/// {"status":"ok","output":"..."}.
ipc::Reply ipc_render_job(const CompositionRegistry& registry,
                          const runtime::job::NormalizedRenderJob& job,
                          std::shared_ptr<SoftwareRenderer> warm_renderer = {},
                          std::shared_ptr<media::VideoJobExecutionContext> video_execution = {});

}  // namespace chronon3d::cli
