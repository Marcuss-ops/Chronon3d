#pragma once

#include "pipe_export_types.hpp"
#include "video_export_common.hpp"

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/system_metrics.hpp>
#include <chronon3d/core/triple_buffer_arena.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace chronon3d::cli {

// ── PipeExportSession: intermediate state for the pipeline ───────────────────

struct PipeExportSession {
    // Encoder
    std::unique_ptr<IVideoEncoder> encoder;

    // Renderer
    std::shared_ptr<SoftwareRenderer> renderer;

    // State
    FfmpegExportOptions opts;
    std::string original_output_path;  // P1-B: final path (before .partial suffix)
    SystemMetricsCollector sys_metrics;
    std::string started_at_iso;
    int64_t total_frames{0};
    Frame start_frame{0};
    Frame end_frame{0};
    int canvas_width{0};
    int canvas_height{0};

    // Queue + async writer
    RenderFrameQueue<RenderFramePackage> queue;
    std::atomic<bool> writer_failed{false};
    std::unique_ptr<TripleBufferArena> triple_arena;
    std::unique_ptr<WriterThreadContext> writer_ctx;  // outlives the thread (stored in session)
    std::thread writer_thread;
    std::atomic<uint64_t> writer_encode_us_total{0};
    std::atomic<int> frames_encoded{0};

    // Telemetry
    std::vector<FrameEncoderTelemetryRecord> frame_encoder_telemetry;

    // P0-1 fix(pipe): RenderFrameQueue holds std::mutex + std::condition_variable
    // so it is neither movable nor assignable (copy/move ctors are =delete'd on
    // those types).  Constructing it here in the member-init-list avoids the rot
    // of late-assigning to a default-constructed PipeExportSession in setup.
    // Transitively: PipeExportSession's implicit copy/move ops are deleted (the
    // queue's mutex/cv forbid them), tolerated by unique_ptr-holding + reference-only call sites.
    explicit PipeExportSession(size_t queue_capacity)
        : queue(queue_capacity) {}
};

} // namespace chronon3d::cli
