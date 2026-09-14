// ---------------------------------------------------------------------------
// Daemon render-job concurrency contract.
//
// The IPC RENDER_JOB path holds `DaemonService::m_render_job_mutex` for the
// whole job, so the execution domain admits exactly one video render at a time.
// The DeviceScheduler capacity that the daemon advertises is resolved from the
// same authority (daemon_render_concurrency.hpp); these cases pin that the
// advertised number can never exceed the serialized limit and that an override
// asking for more is reported rather than quietly ignored.
//
// `daemon_service_ipc.cpp` carries the matching static_asserts next to the
// mutex, so the contract fails the build if only one side is changed.
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include "daemon/daemon_render_concurrency.hpp"

#include <array>
#include <cstdint>

using chronon3d::cli::kMaxConfigurableRenderJobSlots;
using chronon3d::cli::kRenderJobExecutionSerialized;
using chronon3d::cli::kSerializedRenderJobSlots;
using chronon3d::cli::kTargetConcurrentRenderJobs;
using chronon3d::cli::resolve_render_job_slots;

// The mutex is held for the whole job: one holder means one slot.
static_assert(kSerializedRenderJobSlots == 1U,
              "a single execution-domain mutex admits exactly one render job");

TEST_CASE("render concurrency: an unset override advertises the serialized limit") {
    for (const char* env : std::array<const char*, 2>{nullptr, ""}) {
        const auto resolution = resolve_render_job_slots(env);
        CHECK(resolution.slots == kSerializedRenderJobSlots);
        CHECK(resolution.requested == kTargetConcurrentRenderJobs);
        CHECK(resolution.requested_more_than_effective ==
              (kTargetConcurrentRenderJobs > kSerializedRenderJobSlots));
    }
}

TEST_CASE("render concurrency: an override above the serialized limit is clamped and reported") {
    // The production target (2) and any higher request cannot be honoured while
    // the execution domain is serialized: the daemon must advertise 1 and say so.
    for (const char* env : {"2", "3", "8"}) {
        const auto resolution = resolve_render_job_slots(env);
        CHECK(resolution.slots == kSerializedRenderJobSlots);
        CHECK(resolution.requested > resolution.slots);
        CHECK(resolution.requested_more_than_effective);
    }
}

TEST_CASE("render concurrency: requesting the serializeable limit is not reported") {
    const auto resolution = resolve_render_job_slots("1");
    CHECK(resolution.slots == kSerializedRenderJobSlots);
    CHECK(resolution.requested == 1U);
    CHECK_FALSE(resolution.requested_more_than_effective);
}

TEST_CASE("render concurrency: out-of-range and unparsable values fall back to the target") {
    for (const char* env : {"0", "-3", "9", "999", "abc", "  ", "2x"}) {
        const auto resolution = resolve_render_job_slots(env);
        CHECK(resolution.slots == kSerializedRenderJobSlots);
        CHECK(resolution.requested == kTargetConcurrentRenderJobs);
    }
}

TEST_CASE("render concurrency: the advertised capacity is bounded by the execution domain") {
    // Whichever way the contract is configured, capacity must never exceed what
    // the execution domain admits — that mismatch was the original defect.
    const std::uint32_t execution_limit =
        kRenderJobExecutionSerialized ? kSerializedRenderJobSlots
                                      : kMaxConfigurableRenderJobSlots;
    for (const char* env : std::array<const char*, 4>{nullptr, "1", "2", "8"}) {
        CHECK(resolve_render_job_slots(env).slots <= execution_limit);
    }
}
