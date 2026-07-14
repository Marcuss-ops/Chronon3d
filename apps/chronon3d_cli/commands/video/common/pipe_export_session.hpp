#pragma once

#include "pipe_export_helpers.hpp"

#include <chronon3d/core/cpu_budget.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/triple_buffer_arena.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/core/profiling/counters.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>

namespace chronon3d::cli {

// ── RenderFrameQueue — bounded blocking queue replacing moodycamel::ConcurrentQueue ─
// Wraps std::queue + std::mutex + condition_variables.  Exposes blocking
// push/pop/close for the video pipeline (P1-19: legacy try_dequeue/enqueue
// removed; the public surface is now exclusively the production API).

template <typename T>
class RenderFrameQueue {
public:
    explicit RenderFrameQueue(size_t capacity = 0)
        : capacity_(capacity) {}

    size_t size_approx() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    /// Blocking push.  Returns false if the queue is closed or the token is
    /// cancelled before the item can be enqueued.  On success the item is
    /// moved into the queue; on failure the caller retains ownership.
    bool push(T& item, const CancellationToken* token = nullptr) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (capacity_ > 0) {
            not_full_.wait(lock, [this, token]() {
                if (token && token->is_cancelled()) return true;
                return closed_ || queue_.size() < capacity_;
            });
        }
        if (closed_) return false;
        if (token && token->is_cancelled()) return false;
        queue_.push(std::move(item));
        not_empty_.notify_one();
        return true;
    }

    /// Blocking pop.  Returns false when the queue is closed and empty, or
    /// when the token is cancelled.
    bool pop(T& item, const CancellationToken* token = nullptr) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this, token]() {
            if (token && token->is_cancelled()) return true;
            return closed_ || !queue_.empty();
        });
        if (closed_ && queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return true;
    }

    /// Close the queue, waking all blocked producers and consumers.
    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

private:
    std::queue<T> queue_;
    size_t capacity_{0};
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    bool closed_{false};
};

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
    bool output_published{false};
    int frames_rendered{0};
    int frames_enqueued{0};
    int frames_encoded{0};
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
    RenderFrameQueue<RenderFramePackage>& queue;
    std::atomic<bool>& writer_failed;
    TripleBufferArena& triple_arena;
    IVideoEncoder& encoder;
    SoftwareRenderer & renderer;
    std::atomic<uint64_t>& writer_encode_us_total;
    std::atomic<int>& frames_encoded;
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
    SoftwareRenderer& sw_renderer;  // P1-20 — non-nullable; renderer is mandatory on this path
    RenderFrameQueue<RenderFramePackage>& queue;
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

// ── Pipeline phases (each extracted into its own .cpp file) ─────────────────

/// Phase 1: Create encoder, renderer, arena, queue, writer thread.
[[nodiscard]] std::unique_ptr<PipeExportSession> setup_pipe_export_session(
    const CompositionRegistry& registry,
    const Composition& comp,
    const RenderSettings& settings,
    const FfmpegExportOptions& opts,
    Frame start,
    Frame end,
    const chronon3d::CpuBudget& cpu_budget);

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
    const FfmpegExportOptions& opts,
    const chronon3d::CpuBudget& cpu_budget);

} // namespace chronon3d::cli
