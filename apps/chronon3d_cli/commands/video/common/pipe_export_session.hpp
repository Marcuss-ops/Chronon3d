#pragma once

#include "pipe_export_types.hpp"
#include "pipe_startup_breakdown.hpp"
#include "video_export_common.hpp"
#include "../../../daemon/daemon_render_concurrency.hpp"

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/core/system_metrics.hpp>
#include <chronon3d/core/triple_buffer_arena.hpp>
#include <chronon3d/core/profiling/render_counter_types.hpp>
#include <chronon3d/runtime/render_preparation.hpp>
#include "gpu_slot_pool.hpp"
#include <chronon3d/media/video/native_video_frame_decoder.hpp>
#include <chronon3d/media/video/video_device_runtime.hpp>
#include <chronon3d/media/video/direct_yuv_program.hpp>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <optional>

namespace chronon3d::cli {

/// CLI-owned session state for DirectYUV. Preparation and one-frame execution
/// belong to the media program; the CLI owns only the shared writer handoff.
struct DirectYuvSession {
    std::shared_ptr<media::video::DirectYuvProgram> program;
    RenderCounters counters;
    std::unique_ptr<assets::AssetResolver> asset_resolver;
    bool required_but_unavailable{false};
};

struct FullGraphSession {
    // B2 (TICKET-100X-PERF-WAVES): 6 in-flight frame slots instead of 3 so the
    // render loop stays ~2 submissions ahead of the GPU. This matches the
    // Vulkan submission ring depth and kills the per-frame batch-drain stall
    // (NVENC was measured at 73% avg utilization, starved between drains).
    static constexpr std::size_t kGpuEncodeSlotCapacity = 6;

    static std::size_t configured_gpu_encode_slot_capacity() noexcept {
        const char* raw = std::getenv("CHRONON_GPUSLOT_DEPTH");
        if (!raw || *raw == '\0') return kGpuEncodeSlotCapacity;
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(raw, &end, 10);
        if (end == raw || *end != '\0' || parsed == 0 || parsed > 64) {
            spdlog::warn("[video] ignoring invalid CHRONON_GPUSLOT_DEPTH={}; using {}",
                         raw, kGpuEncodeSlotCapacity);
            return kGpuEncodeSlotCapacity;
        }
        return static_cast<std::size_t>(parsed);
    }

    std::shared_ptr<SoftwareRenderer> renderer;
    graph::RenderBackend* surface_backend{nullptr};
    runtime::RenderSurfaceRegistry* surface_registry{nullptr};
    runtime::GpuSlotPool execution_slots;
    std::unique_ptr<TripleBufferArena> triple_arena;

    FullGraphSession()
        : execution_slots(configured_gpu_encode_slot_capacity()) {}

    ~FullGraphSession() {
        execution_slots.close();
        if (!surface_backend || !surface_registry) return;
        for (std::size_t i = 0; i < execution_slots.capacity(); ++i) {
            auto& slot = execution_slots.slot(i);
            const auto native = slot.native_surface;
            const auto source = slot.source_surface;
            if (native != runtime::kInvalidRenderSurfaceHandle) {
                (void)surface_backend->release_surface(native);
                (void)surface_registry->release(native);
            }
            if (source != runtime::kInvalidRenderSurfaceHandle && source != native) {
                (void)surface_backend->release_surface(source);
                (void)surface_registry->release(source);
            }
            slot.native_surface = runtime::kInvalidRenderSurfaceHandle;
            slot.source_surface = runtime::kInvalidRenderSurfaceHandle;
        }
    }
};

struct PipeExportSession {
    std::unique_ptr<IVideoEncoder> encoder;

    std::unique_ptr<FullGraphSession> full_graph_session;
    std::unique_ptr<DirectYuvSession> direct_yuv_session;

    std::shared_ptr<::chronon3d::media::NativeVideoFrameDecoder> native_decoder;

    FfmpegExportOptions opts;
    std::optional<media::VideoExecutionPlan> execution_plan;
    std::string original_output_path;
    SystemMetricsCollector sys_metrics;
    std::string started_at_iso;
    int64_t total_frames{0};
    Frame start_frame{0};
    Frame end_frame{0};
    int canvas_width{0};
    int canvas_height{0};

    double engine_init_ms{0.0};
    double backend_init_ms{0.0};
    double startup_ms{0.0};
    double input_open_ms{0.0};
    double setup_prepare_ms{0.0};
    StartupBreakdown startup_breakdown;
    PrepareBreakdown prepare_breakdown;

    std::uint64_t trace_job_id{0};

    std::optional<runtime::DeviceReservation> device_reservation;
    runtime::DeviceId device_id{0};

    /// P0-2: the device working set admission charged for THIS job.  Kept on
    /// the session so the compiled footprint that only appears after renderer
    /// preparation can be reconciled against it (see
    /// `reconcile_vram_working_set`) instead of the estimate vanishing into a
    /// log line.  Default-constructed (`estimated_peak_bytes == 0`) on the
    /// paths where the daemon already holds the reservation.
    VramWorkingSet vram_working_set;

    std::shared_ptr<media::VideoDeviceRuntime> device_runtime;

    // Z7: the device counter is cumulative, so capture explicit window
    // boundaries instead of making the report infer where prewarm ended.
    std::uint64_t cuda_host_upload_bytes_before_setup{0};
    std::uint64_t cuda_host_upload_bytes_at_steady_start{0};
    bool cuda_host_upload_steady_window_started{false};

    /// Per-job wall anchor for exclusive-wall timing. CLI single-shot
    /// renders anchor at process start; a persistent daemon RENDER_JOB
    /// anchors at the request's own arrival (see
    /// media::VideoJobExecutionContext::job_started_at). Exclusive-wall
    /// startup/process timings are measured from this point, never from
    /// `process_start_time()` alone, so a long-lived daemon cannot report
    /// its uptime as per-job startup.
    std::chrono::steady_clock::time_point job_started_at{};
    /// True when this export runs inside a persistent daemon (an explicit
    /// job anchor was supplied): the sidecar measurement_kind is then
    /// "warm_process" instead of the CLI default "cold_process".
    bool warm_process{false};

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

    // SPSC render→writer handoff: fixed-capacity lock-free ring (Wave 3).
    // The render/DirectYUV loop is the single producer; the writer thread is
    // the single consumer. See pipe_export_queue.hpp for the lifecycle
    // (Open/Closed/Failed) and backpressure semantics.
    RenderFrameQueue queue;
    PipelineDepthMetrics pipeline_metrics;
    std::atomic<bool> writer_failed{false};
    std::unique_ptr<WriterThreadContext> writer_ctx;
    std::thread writer_thread;
    std::atomic<uint64_t> writer_encode_us_total{0};
    std::atomic<int> frames_encoded{0};
    // Published once at render-loop start and shared with the writer so both
    // sides use the same steady-clock origin for per-frame wall telemetry.
    std::atomic<std::int64_t> telemetry_anchor_ns{0};

    std::vector<chronon3d::telemetry::FrameTelemetry> frame_encoder_telemetry;

    // queue_capacity is retained for source compatibility and its non-zero
    // contract (F8.1 — the video pipeline REQUIRES bounded backpressure: an
    // unbounded queue would buffer whole frames until OOM under encoder
    // stall). The SPSC ring and CPU framebuffer arena share the single
    // kCpuVideoPipelineDepth authority.
    explicit PipeExportSession(size_t queue_capacity) {
        if (queue_capacity == 0) {
            throw std::invalid_argument(
                "PipeExportSession requires a bounded queue capacity (> 0)");
        }
        static_assert(RenderFrameQueue::kCapacity == kCpuVideoPipelineDepth,
                      "SPSC ring must use the bounded CPU pipeline depth");
    }

    ~PipeExportSession() {
        queue.close();
        if (writer_thread.joinable()) {
            writer_thread.join();
        }
        encoder.reset();
        native_decoder.reset();
        full_graph_session.reset();
    }
};

} // namespace chronon3d::cli
