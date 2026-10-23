#pragma once

#include <chronon3d/runtime/device_scheduler.hpp>
#include <chronon3d/media/video/media_probe.hpp>
#include <chronon3d/media/video/video_device_runtime.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace chronon3d::media {

/// Opaque-to-media, process-persistent render resources owned by a CLI/daemon
/// execution authority. Concrete renderers may attach slot pools and scratch
/// arenas here without making the media runtime depend on renderer internals.
struct PersistentVideoRenderState {
    virtual ~PersistentVideoRenderState() = default;
};

/// Output facts already observed at the export boundary
/// (TICKET-OVERLAY-THROUGHPUT fix 2, dedup). The exporter's
/// `verify_output_contract` probe and SHA-256 are the ONE observation of the
/// freshly published artifact; the receipt builder reuses them via the render
/// receipt input's precomputed facts instead of probing and hashing the same
/// bytes a second time (~300 ms/item, spawn-dominated ffprobe + re-hash).
struct ObservedOutputFacts {
    /// Probe observation; empty when no probe ran (e.g. raw pre-encode lane).
    std::optional<video::MediaProbeInfo> probe;
    /// Hex digest of the published artifact; empty when not computed.
    std::string sha256;
    /// Caller-measured costs, carried so the receipt keeps honest timings.
    double probe_ms{0.0};
    double sha256_ms{0.0};
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
    /// Daemon receipt deferral. When set (the persistent-daemon RENDER_JOB
    /// path), a successful job does NOT write `<output>.receipt.json` inline:
    /// it hands the fully assembled receipt step to this sink instead. The
    /// daemon runs the step after releasing the execution lock and the device
    /// reservation, so receipt verification (probe + hash + full decode) never
    /// occupies the render lane: it is verification of an already-finished
    /// render, not render work. The step is self-contained and the reply still
    /// leaves only after it completed, so the worker's receipt read stays
    /// deterministic. CLI one-shots leave the sink unset and keep the inline
    /// write.
    std::function<void(std::function<void()>)> receipt_deferral;
    /// Fix 2 facts (see ObservedOutputFacts). Written by the exporter at the
    /// verification boundary, consumed by the receipt writer of either path
    /// (plan executor or RenderJob executor).
    ObservedOutputFacts observed_output;
};

} // namespace chronon3d::media
