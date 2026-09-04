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
#include "gpu_slot_pool.hpp"
#include <chronon3d/media/video/native_video_frame_decoder.hpp>
#include <chronon3d/media/video/video_device_runtime.hpp>
#include <chronon3d/media/video/direct_yuv_executor.hpp>
#include <spdlog/spdlog.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <optional>

namespace chronon3d::cli {

/// CLI-owned adapter state for DirectYUV. The executable knows only the
/// media executor contract; DirectYuvProgram remains private to src/media.
struct DirectYuvSession {
    std::shared_ptr<media::video::DirectYuvExecutor> executor;
    RenderCounters counters;
    std::unique_ptr<assets::AssetResolver> asset_resolver;
    bool required_but_unavailable{false};
};

struct FullGraphSession {
    static constexpr std::size_t kGpuEncodeSlotCapacity = 3;

    std::shared_ptr<SoftwareRenderer> renderer;
    graph::RenderBackend* surface_backend{nullptr};
    runtime::RenderSurfaceRegistry* surface_registry{nullptr};
    runtime::GpuSlotPool execution_slots;
    std::unique_ptr<TripleBufferArena> triple_arena;

    FullGraphSession()
        : execution_slots(kGpuEncodeSlotCapacity) {}

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

    std::shared_ptr<media::VideoDeviceRuntime> device_runtime;

    runtime::RenderPreparationTimings prepare_timings;

    [[nodiscard]] bool direct_yuv_selected() const noexcept {
        return direct_yuv_session && direct_yuv_session->executor;
    }

    [[nodiscard]] SoftwareRenderer* renderer_ptr() noexcept {
        return full_graph_session ? full_graph_session->renderer.get() : nullptr;
    }
    [[nodiscard]] const SoftwareRenderer* renderer_ptr() const noexcept {
        return full_graph_session ? full_graph_session->renderer.get() : nullptr;
    }

    runtime::BoundedChannel<RenderFramePackage> queue;
    std::atomic<bool> writer_failed{false};
    std::unique_ptr<WriterThreadContext> writer_ctx;
    std::thread writer_thread;
    std::atomic<uint64_t> writer_encode_us_total{0};
    std::atomic<int> frames_encoded{0};

    std::vector<chronon3d::telemetry::FrameTelemetry> frame_encoder_telemetry;

    explicit PipeExportSession(size_t queue_capacity)
        : queue(queue_capacity) {
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
