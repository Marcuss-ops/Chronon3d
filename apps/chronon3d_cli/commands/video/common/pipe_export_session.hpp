#pragma once

#include "pipe_export_types.hpp"
#include "video_export_common.hpp"

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/system_metrics.hpp>
#include <chronon3d/core/triple_buffer_arena.hpp>
#include <chronon3d/runtime/render_preparation.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <array>

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

    // Job-level timings captured during renderer/backend construction so the
    // frame-timing sidecar can report engine_init_ms vs backend_init_ms.
    double engine_init_ms{0.0};
    double backend_init_ms{0.0};

    // Trace correlation: stable per-job id synthesized at session setup and
    // shared by the render loop, the decoder and the writer thread so every
    // stage builds the same Perfetto flow ids (see tracing/trace_ids.hpp).
    std::uint64_t trace_job_id{0};

    // Accumulated prepare-barrier sub-timings (preflight + font preflight +
    // warmup), emitted as the `job.prepare` breakdown in the sidecar.
    runtime::RenderPreparationTimings prepare_timings;

    // Queue + async writer
    RenderFrameQueue<RenderFramePackage> queue;
    FrameInteropRing interop_ring;
    std::array<runtime::RenderSurfaceHandle, FrameInteropRing::kSlotCount>
        native_encode_surfaces{};
    std::array<runtime::RenderSurfaceHandle, FrameInteropRing::kSlotCount>
        native_source_surfaces{};
    std::atomic<bool> writer_failed{false};
    std::unique_ptr<TripleBufferArena> triple_arena;
    std::unique_ptr<WriterThreadContext> writer_ctx;  // outlives the thread (stored in session)
    std::thread writer_thread;
    std::atomic<uint64_t> writer_encode_us_total{0};
    std::atomic<int> frames_encoded{0};

    // Telemetry — single canonical frame record shared with the render thread.
    std::vector<chronon3d::telemetry::FrameTelemetry> frame_encoder_telemetry;

    // P0-1 fix(pipe): RenderFrameQueue holds std::mutex + std::condition_variable
    // so it is neither movable nor assignable (copy/move ctors are =delete'd on
    // those types).  Constructing it here in the member-init-list avoids the rot
    // of late-assigning to a default-constructed PipeExportSession in setup.
    // Transitively: PipeExportSession's implicit copy/move ops are deleted (the
    // queue's mutex/cv forbid them), tolerated by unique_ptr-holding + reference-only call sites.
    explicit PipeExportSession(size_t queue_capacity)
        : queue(queue_capacity), interop_ring(FrameInteropRing::kSlotCount) {
        native_encode_surfaces.fill(runtime::kInvalidRenderSurfaceHandle);
        native_source_surfaces.fill(runtime::kInvalidRenderSurfaceHandle);
    }

    // ── P1-B safety: never destroy a joinable writer thread ─────────────
    // The warmup/render phases run after the writer thread is started.  If
    // one of them throws (e.g. a backend that cannot execute the node
    // contract, like Vulkan before RenderSurface execution is wired), the
    // exception unwinds through the session.  Closing the queue and joining
    // the thread here keeps std::thread's destructor from calling
    // std::terminate() on a joinable thread, so the error propagates as a
    // structured failure instead of a core dump.  Idempotent on the normal
    // path (run_pipe_export_loop already closes + joins).
    ~PipeExportSession() {
        queue.close();
        interop_ring.close();
        if (writer_thread.joinable()) {
            writer_thread.join();
        }
    }
};

} // namespace chronon3d::cli
