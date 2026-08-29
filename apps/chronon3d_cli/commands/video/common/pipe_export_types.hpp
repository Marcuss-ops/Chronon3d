#pragma once

#include "pipe_export_queue.hpp"
#include "pipe_export_helpers.hpp"
#include "direct_yuv_program.hpp"

#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d::cli {

// ── Boundary model for the final export result ──────────────────────────────

struct PipeExportResult {
    int return_code{1};
    bool success{false};
    bool cancelled{false};
    bool render_failed{false};
    bool writer_error{false};
    bool exception_error{false};
    bool encoder_close_failed{false};
    bool output_published{false};
    /// True ONLY after the full OutputContract verification passed (incl.
    /// SHA-256). Set by make_pipe_export_result, never before.
    bool copy_eligible{false};
    /// SHA-256 hex digest of the published artifact (empty = not computed).
    std::string sha256;
    /// Wall time of the ffprobe subprocess during output verification, in ms.
    double ffprobe_ms{0.0};
    /// Wall time of the SHA-256 digest computation during verification, in ms.
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

// ── Aggregated telemetry for the export pipeline ────────────────────────────

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

// ── Writer thread ───────────────────────────────────────────────────────────

struct WriterThreadContext {
    RenderFrameQueue<RenderFramePackage>& queue;
    std::atomic<bool>& writer_failed;
    TripleBufferArena& triple_arena;
    IVideoEncoder& encoder;
    SoftwareRenderer & renderer;
    std::atomic<uint64_t>& writer_encode_us_total;
    std::atomic<int>& frames_encoded;
    bool require_native_gpu{false};
    FrameInteropRing& interop_ring;
    std::vector<chronon3d::telemetry::FrameTelemetry>& frame_encoder_telemetry;
    /// Trace correlation: stable per-job id mixed with the package frame
    /// number to build the terminating Perfetto flow id for this encode.
    std::uint64_t trace_job_id{0};
};

// ── Render loop ─────────────────────────────────────────────────────────────

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
    SoftwareRenderer* sw_renderer;
    RenderFrameQueue<RenderFramePackage>& queue;
    std::atomic<bool>& writer_failed;
    std::atomic<int>& frames_encoded;
    FrameInteropRing& interop_ring;
    std::array<runtime::RenderSurfaceHandle, FrameInteropRing::kSlotCount>&
        native_encode_surfaces;
    std::array<runtime::RenderSurfaceHandle, FrameInteropRing::kSlotCount>&
        native_source_surfaces;
    TripleBufferArena& triple_arena;
    RenderCounters* counters;
    std::vector<chronon3d::telemetry::FrameTelemetry>& telemetry_frames;
    /// Trace correlation: stable per-job id mixed with the current frame to
    /// build the non-terminating Perfetto flow id for this render.
    std::uint64_t trace_job_id{0};
    std::shared_ptr<DirectYuvProgram> direct_yuv_program;
};

struct RenderLoopResult {
    PipeExportStatus status;
    double render_graph_eval_ms{0.0};
    double queue_wait_ms{0.0};
};

struct RenderLoopOutput {
    RenderLoopResult loop_result;
    std::vector<chronon3d::telemetry::FrameTelemetry> telemetry_frames;
    double render_ms{0.0};
    std::chrono::steady_clock::time_point render_end;
};

/// Drain the frame queue, encode each frame, release arena buffers.
/// Sets writer_failed on encoder error.
void run_writer_thread(const WriterThreadContext& ctx);

/// Iterate over frames, render each one, and enqueue for the writer thread.
/// Handles cancellation, back-pressure, and per-frame telemetry.
[[nodiscard]] RenderLoopResult run_render_loop(const RenderLoopContext& ctx);

// ── Encoder close result ──────────────────────────────────────────────────

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
    bool success{true};
};

} // namespace chronon3d::cli
