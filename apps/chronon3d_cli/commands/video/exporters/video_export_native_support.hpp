#pragma once

#include "../common/pipe_export_pipeline.hpp"
#include "pipe_timing_sidecar.hpp"

#include <chronon3d/media/video/native_video_frame_decoder.hpp>

namespace chronon3d::cli {

/// Configure the scheduler owned by the native export path when the caller
/// did not provide an already-admitted scheduler.
[[nodiscard]] bool configure_local_device_scheduler(
    const FfmpegExportOptions& opts,
    runtime::DeviceScheduler& local_scheduler,
    runtime::DeviceScheduler*& active_scheduler);

/// Phase 5 of the native export — project the backend's GPU telemetry
/// counters onto the typed sidecar payload. Pure projection: the backend
/// exports name/value pairs and this maps them onto JobTimings::gpu fields.
void project_backend_gpu_counters(
    PipeExportSession& session,
    pipe_timing::JobTimings& timings);

/// Phase 6 of the native export — job-lifecycle accounting. Projects the
/// measured per-phase walls onto the exclusive-wall timeline (whose remainder
/// must equal the process wall within the reported unaccounted_ms) and
/// finalizes the session's startup/prepare breakdowns, which are projections
/// of the same measurements. Lives in this TU so `video_export_native.cpp`
/// stays an orchestrator.
void account_exclusive_wall_timeline(
    PipeExportSession& session,
    const RenderLoopOutput& loop_output,
    const EncoderCloseResult& close_result,
    const PipeExportResult& result,
    double encode_ms,
    double warmup_ms,
    bool is_native,
    pipe_timing::JobTimings& timings);

/// Phase 4 of the native export — projection of the render/session counters
/// onto the typed sidecar payload: backend GPU counters, CUDA host-upload
/// windows, glyph-atlas statistics, prepare-time image/font cache hits and
/// video-runtime reuse counters. Pure projection of finished state.
void project_render_counters(
    PipeExportSession& session,
    const RenderCounters* counters,
    pipe_timing::JobTimings& timings);

/// Phase 7 of the native export — internal (sub-system) profiling projection.
/// Projects the decoder, DirectYUV and encoder internals already measured
/// during the run onto the typed sidecar payload. Pure projection: it reads
/// finished state and writes only into `timings`.
void account_internal_profiling(
    PipeExportSession& session,
    const EncoderCloseResult& close_result,
    const media::NativeVideoFrameDecoder::DecodeProfilingStats& decode_stats,
    const RenderCounters* counters,
    pipe_timing::JobTimings& timings);

} // namespace chronon3d::cli
