#pragma once

#include "pipe_export_types.hpp"
#include "pipe_startup_breakdown.hpp"
#include "video_export_common.hpp"

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/core/system_metrics.hpp>
#include <chronon3d/core/triple_buffer_arena.hpp>
#include <chronon3d/core/profiling/render_counter_types.hpp>
#include <chronon3d/runtime/render_preparation.hpp>
#include <chronon3d/runtime/frame_execution_slot_ring.hpp>
#include <chronon3d/media/video/native_video_frame_decoder.hpp>
#include <chronon3d/media/video/video_device_runtime.hpp>
#include <spdlog/spdlog.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <optional>

namespace chronon3d::cli {

/// State owned exclusively by the Direct-YUV execution mode.  Keeping this
/// state together makes the mode boundary explicit: the direct path owns
/// only its program, job-scoped asset resolver and counters, never renderer
/// or worker-owned image-cache resources.
struct DirectYuvSession {
    std::shared_ptr<DirectYuvProgram> program;
    RenderCounters counters;
    std::unique_ptr<assets::AssetResolver> asset_resolver;
    bool required_but_unavailable{false};
};

/// Resources that exist only for FullGraph execution.  A DirectYuvSession
/// never constructs this object, so renderer/Vulkan surface lifetime cannot
/// accidentally leak into the direct execution mode.
struct FullGraphSession {
    std::shared_ptr<SoftwareRenderer> renderer;
    // The pool owns the logical handles and their backend bindings for the
    // complete FullGraph job. Releasing the registry entry alone is not
    // sufficient: Vulkan/CUDA may still own the physical allocation.
    graph::RenderBackend* surface_backend{nullptr};
    runtime::RenderSurfaceRegistry* surface_registry{nullptr};
    runtime::FrameExecutionSlotRing execution_slots;
    std::unique_ptr<TripleBufferArena> triple_arena;

    FullGraphSession()
        : execution_slots(runtime::FrameExecutionSlotRing::kDefaultCapacity) {}

    ~FullGraphSession() {
        execution_slots.close();
        if (!surface_backend || !surface_registry) return;
        for (std::size_t i = 0; i < execution_slots.capacity(); ++i) {
            auto& slot = execution_slots.slot(i);
            for (const auto handle : {slot.native_surface, slot.source_surface}) {
                if (handle != runtime::kInvalidRenderSurfaceHandle) {
                    (void)surface_backend->release_surface(handle);
                    (void)surface_registry->release(handle);
                }
            }
            slot.native_surface = runtime::kInvalidRenderSurfaceHandle;
            slot.source_surface = runtime::kInvalidRenderSurfaceHandle;
        }
    }
};

// ── PipeExportSession: intermediate state for the pipeline ───────────────────

struct PipeExportSession {
    // Encoder
    std::unique_ptr<IVideoEncoder> encoder;

    // Execution-mode state. Exactly one mode is created by the resolver.
    std::unique_ptr<FullGraphSession> full_graph_session;
    std::unique_ptr<DirectYuvSession> direct_yuv_session;

    // Renderer is retained only through FullGraphSession.
    // Kept alive through encoder close: the decoder's CUDA/Vulkan import
    // session may still have external-semaphore work ordered behind the
    // native encoder drain.
    std::shared_ptr<::chronon3d::media::NativeVideoFrameDecoder> native_decoder;

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
    double startup_ms{0.0};
    double input_open_ms{0.0};
    double setup_prepare_ms{0.0};
    StartupBreakdown startup_breakdown;
    PrepareBreakdown prepare_breakdown;

    // Trace correlation: stable per-job id synthesized at session setup and
    // shared by the render loop, the decoder and the writer thread so every
    // stage builds the same Perfetto flow ids (see tracing/trace_ids.hpp).
    std::uint64_t trace_job_id{0};

    // Placement is owned by DeviceScheduler; the reservation remains alive
    // for the complete export session so resources cannot be oversubscribed.
    std::optional<runtime::DeviceReservation> device_reservation;
    runtime::DeviceId device_id{0};

    // Persistent per-device GPU runtime (primary CUDA context + FFmpeg
    // hwdevice) borrowed from the job's VideoRuntimeRegistry. The encoder
    // and the decoder both alias this same context — one owner per device.
    std::shared_ptr<media::VideoDeviceRuntime> device_runtime;

    // Accumulated prepare-barrier sub-timings (preflight + font preflight +
    // warmup), emitted as the `job.prepare` breakdown in the sidecar.
    runtime::RenderPreparationTimings prepare_timings;

    [[nodiscard]] bool direct_yuv_selected() const noexcept {
        return direct_yuv_session && direct_yuv_session->program;
    }

    [[nodiscard]] SoftwareRenderer* renderer_ptr() noexcept {
        return full_graph_session ? full_graph_session->renderer.get() : nullptr;
    }
    [[nodiscard]] const SoftwareRenderer* renderer_ptr() const noexcept {
        return full_graph_session ? full_graph_session->renderer.get() : nullptr;
    }

    // Queue + async writer
    runtime::BoundedChannel<RenderFramePackage> queue;
    std::atomic<bool> writer_failed{false};
    std::unique_ptr<WriterThreadContext> writer_ctx;  // outlives the thread (stored in session)
    std::thread writer_thread;
    std::atomic<uint64_t> writer_encode_us_total{0};
    std::atomic<int> frames_encoded{0};

    // Telemetry — single canonical frame record shared with the render thread.
    std::vector<chronon3d::telemetry::FrameTelemetry> frame_encoder_telemetry;

    // P0-1 fix(pipe): BoundedChannel holds std::mutex + std::condition_variable
    // so it is neither movable nor assignable (copy/move ctors are =delete'd on
    // those types).  Constructing it here in the member-init-list avoids the rot
    // of late-assigning to a default-constructed PipeExportSession in setup.
    // Transitively: PipeExportSession's implicit copy/move ops are deleted (the
    // queue's mutex/cv forbid them), tolerated by unique_ptr-holding + reference-only call sites.
    explicit PipeExportSession(size_t queue_capacity)
        : queue(queue_capacity) {
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
        if (writer_thread.joinable()) {
            writer_thread.join();
        }
        // Drain/tear down the encoder before the backend surface pool and
        // decoder. This ordering is part of the ownership contract: no
        // consumer may outlive the physical surface bindings it reads.
        encoder.reset();
        native_decoder.reset();
        full_graph_session.reset();
    }
};

} // namespace chronon3d::cli
