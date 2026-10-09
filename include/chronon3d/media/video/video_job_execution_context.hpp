#pragma once

#include <chronon3d/runtime/device_scheduler.hpp>
#include <chronon3d/media/video/video_device_runtime.hpp>

#include <chrono>
#include <memory>
#include <optional>

namespace chronon3d::media {

/// Opaque-to-media, process-persistent render resources owned by a CLI/daemon
/// execution authority. Concrete renderers may attach slot pools and scratch
/// arenas here without making the media runtime depend on renderer internals.
struct PersistentVideoRenderState {
    virtual ~PersistentVideoRenderState() = default;
};

/// Host-selected placement and persistent per-device video runtime.
/// Export setup consumes the reservation; it never performs a second reserve.
struct VideoJobExecutionContext {
    // Borrowed daemon-owned scheduler. The daemon reserves the device before
    // dispatch and the video exporter must use that same scheduler for its
    // encoder/runtime reservation; it must never rediscover a second device
    // authority inside the export path.
    runtime::DeviceScheduler* device_scheduler{nullptr};
    std::optional<runtime::DeviceReservation> reservation;
    std::shared_ptr<VideoRuntimeRegistry> video_runtimes;
    std::shared_ptr<PersistentVideoRenderState> persistent_render_state;
    runtime::DeviceId device_id{0};
    std::uint32_t physical_device_index{0};
    std::int32_t cuda_device_ordinal{-1};
    /// Wall anchor of the render request inside a persistent daemon: the
    /// exclusive-wall startup/process timings are measured from here so a
    /// long-lived process never reports "startup = daemon uptime". CLI
    /// single-shot invocations leave it at the default epoch and keep the
    /// process-start anchor.
    std::chrono::steady_clock::time_point job_started_at{};
    double daemon_admission_wait_ms{0.0};
};

} // namespace chronon3d::media
