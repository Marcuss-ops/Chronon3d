#pragma once

#include "pipe_export_queue.hpp"
#include "pipe_export_helpers.hpp"

#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>
#include <chronon3d/media/video/video_execution_resolver.hpp>
#include <chronon3d/media/video/native_video_frame_decoder.hpp>
#include <chronon3d/core/triple_buffer_arena.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d::cli {

struct PipeExportSession;

struct PipeExportResult {
    int return_code{1};
    bool success{false};
    bool cancelled{false};
    bool render_failed{false};
    bool writer_error{false};
    bool exception_error{false};
    bool encoder_close_failed{false};
    bool output_published{false};
    bool copy_eligible{false};
    std::string sha256;
    double ffprobe_ms{0.0};
    double sha256_ms{0.0};
    int frames_rendered{0};
    int frames_enqueued{0};
    int frames_encoded{0};
    double wall_time_ms{0.0};
    double render_ms{0.0};
    double encode_ms{0.0};
    double validation_ms{0.0};
    double output_finalize_ms{0.0};
};

struct PipeExportTelemetry {
    double render_graph_eval_ms{0.0};
    double queue_wait_ms{0.0};
    double writer_encode_ms{0.0};
    double conv_copy_ms{0.0};
    double write_blocked_ms{0.0};
    double ffmpeg_flush_close_ms{0.0};
    double native_convert_ms{0.0};
    double native_send_ms{0.0};
    double native_receive_ms{0.0};
    double native_mux_ms{0.0};
    double native_trailer_ms{0.0};
};

struct WriterThreadContext {
    runtime::BoundedChannel<RenderFramePackage>& queue;
    std::atomic<bool>& writer_failed;
    TripleBufferArena* triple_arena{nullptr};
    IVideoEncoder& encoder;
    SoftwareRenderer* renderer{nullptr};
    RenderCounters* counters{nullptr};
    GpuHotPathMode hot_path_mode{GpuHotPathMode::Auto};
    std::optional<media::VideoExecutionPlan> execution_plan;
    std::atomic<uint64_t>& writer_encode_us_total;
    std::atomic<int>& frames_encoded;
    bool require_native_gpu{false};
    std::vector<chronon3d::telemetry::FrameTelemetry>& frame_encoder_telemetry;
    std::uint64_t trace_job_id{0};
};

struct RenderLoopContext {
    graph::RenderBackend& backend;
    cache::NodeCache& node_cache;
    const RenderSettings& settings;
    const CompositionRegistry& registry;
    media::MediaFrameProvider* video_decoder;
    const CompiledComposition& compiled;
    Frame start;
    Frame end;
    const FfmpegExportOptions& opts;
    std::optional<media::VideoExecutionPlan> execution_plan;
    SoftwareRenderer* sw_renderer;
    runtime::BoundedChannel<RenderFramePackage>& queue;
    std::atomic<bool>& writer_failed;
    std::atomic<int>& frames_encoded;
    runtime::GpuSlotPool& execution_slots;
    std::shared_ptr<media::VideoDeviceRuntime> device_runtime;
    TripleBufferArena* triple_arena{nullptr};
    RenderCounters* counters;
    std::vector<chronon3d::telemetry::FrameTelemetry>& telemetry_frames;
    std::uint64_t trace_job_id{0};
};

struct RenderLoopResult {
    PipeExportStatus status;
    double render_graph_eval_ms{0.0};
    double direct_yuv_execute_ms{0.0};
    double queue_wait_ms{0.0};
};

struct RenderLoopOutput {
    RenderLoopResult loop_result;
    std::vector<chronon3d::telemetry::FrameTelemetry> telemetry_frames;
    double render_ms{0.0};
    std::chrono::steady_clock::time_point render_end;
};

void run_writer_thread(const WriterThreadContext& ctx);

[[nodiscard]] RenderLoopResult run_render_loop(const RenderLoopContext& ctx);

/// CLI adapter over media::video::DirectYuvExecutor. PipeExportSession and
/// FfmpegExportOptions stop here and never enter the DirectYUV media core.
[[nodiscard]] RenderLoopOutput run_direct_yuv_loop(
    PipeExportSession& session,
    media::NativeVideoFrameDecoder& decoder,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts);

struct EncoderCloseResult {
    double write_blocked_ms{0.0};
    double native_convert_ms{0.0};
    double native_send_ms{0.0};
    double native_backpressure_ms{0.0};
    std::uint64_t native_cuda_pending_peak{0};
    std::uint64_t native_cuda_backpressure_wait_count{0};
    double native_flush_ms{0.0};
    double native_receive_ms{0.0};
    double native_mux_ms{0.0};
    double native_trailer_ms{0.0};
    double encoder_hwframe_get_buffer_ms{0.0};
    double encoder_surface_acquire_ms{0.0};
    double encoder_nvenc_submit_ms{0.0};
    double encoder_queue_backpressure_wait_ms{0.0};
    double encoder_packet_drain_ms{0.0};
    double direct_yuv_cuda_launch_ms{0.0};
    double direct_yuv_cuda_wait_ms{0.0};
    // Encoder settings actually applied at open() time (post-default
    // resolution). Empty/0 when the encoder is not a hardware NVENC path.
    std::string applied_encoder_preset;
    std::string applied_encoder_rate_control;
    int applied_encoder_async_depth{0};
    // Settings the caller explicitly requested ("engine-default" when never
    // requested). Telemetry pairs these with the applied_* fields above so
    // requested config and resolved config are never conflated.
    std::string requested_encoder_rate_control;
    std::string requested_encoder_preset;
    bool success{true};
};

} // namespace chronon3d::cli
