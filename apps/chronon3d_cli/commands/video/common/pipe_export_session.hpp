#pragma once

#include "pipe_export_helpers.hpp"

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/triple_buffer_arena.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <concurrentqueue/moodycamel/concurrentqueue.h>

#include <atomic>
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
