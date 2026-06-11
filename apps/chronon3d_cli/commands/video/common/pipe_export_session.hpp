#pragma once

#include "pipe_export_helpers.hpp"

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/triple_buffer_arena.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <concurrentqueue/moodycamel/concurrentqueue.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <vector>

namespace chronon3d::cli {

/// Maximum queue depth before back-pressure yields the render thread.
inline constexpr size_t kMaxRenderQueueDepth = 128;

// ── Shared frame package ────────────────────────────────────────────────────

struct RenderFramePackage {
    Frame frame_number{0};
    std::shared_ptr<Framebuffer> framebuffer;
    std::shared_ptr<FramebufferArena> arena;
};

struct FrameEncoderTelemetryRecord {
    Frame frame_number{0};
    double conversion_copy_ms{0.0};
    double encoder_ms{0.0};
    double pipe_write_ms{0.0};
    double native_convert_ms{0.0};
    double native_send_ms{0.0};
    double native_receive_ms{0.0};
    double native_mux_ms{0.0};
};

// ── Boundary models ─────────────────────────────────────────────────────────

struct PipeExportResult {
    int return_code{1};
    bool success{false};
    bool cancelled{false};
    bool render_failed{false};
    bool writer_error{false};
    bool exception_error{false};
    bool encoder_close_failed{false};
    int frames_written{0};
    double wall_time_ms{0.0};
    double render_ms{0.0};
    double encode_ms{0.0};
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

// ── Writer thread ───────────────────────────────────────────────────────────

struct WriterThreadContext {
    moodycamel::ConcurrentQueue<RenderFramePackage>& queue;
    std::atomic<bool>& writer_failed;
    std::atomic<bool>& writer_done;
    TripleBufferArena& triple_arena;
    IVideoEncoder& encoder;
    SoftwareRenderer& renderer;
    std::atomic<uint64_t>& writer_encode_us_total;
    std::vector<FrameEncoderTelemetryRecord>& frame_encoder_telemetry;
};

/// Drain the frame queue, encode each frame, release arena buffers.
/// Sets writer_failed on encoder error.
void run_writer_thread(const WriterThreadContext& ctx);

// ── Render loop ─────────────────────────────────────────────────────────────

struct RenderLoopContext {
    graph::RenderBackend& backend;
    cache::NodeCache& node_cache;
    const RenderSettings& settings;
    const CompositionRegistry& registry;
    media::MediaFrameProvider* video_decoder;
    const Composition& comp;
    Frame start;
    Frame end;
    const FfmpegExportOptions& opts;
    SoftwareRenderer* sw_renderer;
    moodycamel::ConcurrentQueue<RenderFramePackage>& queue;
    std::atomic<bool>& writer_failed;
    TripleBufferArena& triple_arena;
    RenderCounters* counters;
    std::vector<chronon3d::telemetry::FrameTelemetryRecord>& telemetry_frames;
};

struct RenderLoopResult {
    PipeExportStatus status;
    double render_graph_eval_ms{0.0};
    double queue_wait_ms{0.0};
};

/// Iterate over frames, render each one, and enqueue for the writer thread.
/// Handles cancellation, back-pressure, and per-frame telemetry.
[[nodiscard]] RenderLoopResult run_render_loop(const RenderLoopContext& ctx);

// ── PipeExportSession: intermediate state for the pipeline ───────────────────

struct PipeExportSession {
    // Encoder
    std::unique_ptr<IVideoEncoder> encoder;
    std::unique_ptr<FfmpegPipeEncoder> pipe_encoder;  // non-null when !native

    // Renderer
    std::shared_ptr<SoftwareRenderer> renderer;
    SoftwareRenderer* sw_renderer{nullptr};

    // State
    FfmpegExportOptions opts;
    SystemMetricsCollector sys_metrics;
    std::string started_at_iso;
    int64_t total_frames{0};
    Frame start_frame{0};
    Frame end_frame{0};
    int canvas_width{0};
    int canvas_height{0};

    // Queue + async writer
    moodycamel::ConcurrentQueue<RenderFramePackage> queue;
    std::atomic<bool> writer_failed{false};
    std::atomic<bool> writer_done{false};
    std::unique_ptr<TripleBufferArena> triple_arena;
    std::unique_ptr<WriterThreadContext> writer_ctx;  // outlives the thread (stored in session)
    std::thread writer_thread;
    std::atomic<uint64_t> writer_encode_us_total{0};

    // Telemetry
    std::vector<FrameEncoderTelemetryRecord> frame_encoder_telemetry;
};

// ── Pipeline phases (each extracted into its own .cpp file) ─────────────────

/// Phase 1: Create encoder, renderer, arena, queue, writer thread.
[[nodiscard]] std::unique_ptr<PipeExportSession> setup_pipe_export_session(
    const CompositionRegistry& registry,
    const Composition& comp,
    const RenderSettings& settings,
    const FfmpegExportOptions& opts,
    Frame start,
    Frame end);

/// Phase 5: Pre-warm the framebuffer pool (separate from warmup_pipe_renderer).
void warmup_pipe_pool(PipeExportSession& session);

/// Phase 7: Close encoder, gather encoder metrics, log benchmark.
struct EncoderCloseResult {
    double write_blocked_ms{0.0};
    double native_convert_ms{0.0};
    double native_send_ms{0.0};
    double native_receive_ms{0.0};
    double native_mux_ms{0.0};
    double native_trailer_ms{0.0};
    bool success{true};
};
[[nodiscard]] EncoderCloseResult close_pipe_encoder(PipeExportSession& session);

/// Phase 8-9: Collect and record telemetry.
void record_pipe_telemetry(
    const std::string& composition_id,
    PipeExportSession& session,
    const RenderLoopResult& loop_result,
    const EncoderCloseResult& close_result,
    const std::vector<chronon3d::telemetry::FrameTelemetryRecord>& telemetry_frames,
    double wall_time_ms,
    double render_ms,
    double encode_ms);

/// Phase 5: Run the render loop and join the writer thread.
struct RenderLoopOutput {
    RenderLoopResult loop_result;
    std::vector<chronon3d::telemetry::FrameTelemetryRecord> telemetry_frames;
    double render_ms{0.0};
    std::chrono::steady_clock::time_point render_end;
};
[[nodiscard]] RenderLoopOutput run_pipe_export_loop(
    PipeExportSession& session,
    const CompositionRegistry& registry,
    const Composition& comp,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts);

/// Phase 10: Populate the result boundary model.
[[nodiscard]] PipeExportResult make_pipe_export_result(
    const PipeExportSession& session,
    const RenderLoopResult& loop_result,
    const EncoderCloseResult& close_result,
    double render_ms,
    double encode_ms,
    double wall_time_ms);

// ── Main orchestrator ───────────────────────────────────────────────────────

[[nodiscard]] PipeExportResult render_and_encode_ffmpeg_pipe(
    const CompositionRegistry& registry,
    const Composition& comp,
    const std::string& composition_id,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts);

} // namespace chronon3d::cli
