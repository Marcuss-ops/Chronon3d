#pragma once

// ---------------------------------------------------------------------------
// daemon_render_concurrency.hpp — THE authority for "how many video render jobs
// may run at once in this daemon?".
//
// Two numbers used to answer that question independently and disagreed:
//
//   * `DeviceScheduler` capacity was seeded from `CHRONON_RENDER_JOB_SLOTS`
//     (default 2), so the daemon logged `render_job_slots=2`;
//   * the IPC RENDER_JOB path held `DaemonService::m_render_job_mutex` for the
//     whole job, so the execution domain admitted exactly ONE job.
//
// A measured two-clip submission showed zero overlap with a ~310 ms handoff,
// i.e. the advertised capacity was an illusion and any work planned against it
// was planned against a number that does not exist. Both call sites now resolve
// their value here, so the advertised capacity and the mutex contract cannot
// drift apart again.
//
// Why the serialization exists: each video job owns a complete Vulkan/CUDA
// exporter and its frame-transient surface ring. The measured peak VRAM for a
// single 1080p render was ~8.8 GB on a 16 GB device, so a second concurrent
// exporter does not fit — lifting this is a VRAM-safety decision (reduce peak
// transient residency first), not a tuning knob.
//
// To raise the concurrency: reduce per-job peak VRAM, then flip
// `kRenderJobExecutionSerialized` to false together with removing the mutex in
// `daemon_service_ipc.cpp`. The two are asserted to move together by
// `tests/cli/test_daemon_render_concurrency.cpp`.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <stdexcept>
#include <string>

namespace chronon3d::cli {

/// Does the IPC RENDER_JOB path hold an execution-domain mutex for the whole
/// job? Keep this in lockstep with `DaemonService::m_render_job_mutex`.
inline constexpr bool kRenderJobExecutionSerialized = true;

/// Slots the serialized execution domain actually admits.
inline constexpr std::uint32_t kSerializedRenderJobSlots = 1;

/// Production target once per-job VRAM pressure allows a second exporter.
inline constexpr std::uint32_t kTargetConcurrentRenderJobs = 2;

/// Upper bound accepted from the environment (mirrors the historical clamp).
inline constexpr std::uint32_t kMaxConfigurableRenderJobSlots = 8;

/// Escape hatch that lifts the serialization at run time. It exists so the
/// VRAM decision can be A/B measured on a host with headroom without shipping a
/// new contract; it is NOT a substitute for the measurement.
inline constexpr const char* kRenderJobSlotsEnvVar = "CHRONON_RENDER_JOB_SLOTS";

/// Outcome of resolving the requested/effective render-job slots.
struct RenderConcurrencyResolution {
    /// Slots to advertise as DeviceScheduler capacity — never more than the
    /// execution domain can admit.
    std::uint32_t slots{kSerializedRenderJobSlots};
    /// What was requested (env override, else the production target).
    std::uint32_t requested{kTargetConcurrentRenderJobs};
    /// True when the request exceeded the effective limit; the caller must
    /// report this loudly instead of silently clamping.
    bool requested_more_than_effective{false};
};

/// Resolve the render-job slots from an environment value.
///
/// `env_value` is the raw value of `CHRONON_RENDER_JOB_SLOTS` (null/empty means
/// "not set"). Out-of-range or unparsable values fall back to the production
/// target rather than failing the daemon, matching the historical behaviour —
/// but a request above the effective limit is always REPORTED.
[[nodiscard]] inline RenderConcurrencyResolution resolve_render_job_slots(
    const char* env_value) {
    std::uint32_t requested = kTargetConcurrentRenderJobs;
    if (env_value != nullptr && *env_value != '\0') {
        try {
            const int parsed = std::stoi(env_value);
            if (parsed >= 1 &&
                parsed <= static_cast<int>(kMaxConfigurableRenderJobSlots)) {
                requested = static_cast<std::uint32_t>(parsed);
            }
        } catch (const std::exception&) {
            // Unparsable ⇒ keep the production target; nothing to report
            // because no concurrency was promised.
        }
    }

    const std::uint32_t effective =
        kRenderJobExecutionSerialized ? kSerializedRenderJobSlots : requested;

    return RenderConcurrencyResolution{
        .slots = effective,
        .requested = requested,
        .requested_more_than_effective = requested > effective,
    };
}

} // namespace chronon3d::cli
