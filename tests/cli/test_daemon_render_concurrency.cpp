// ---------------------------------------------------------------------------
// Daemon render-job concurrency contract.
//
// The IPC RENDER_JOB path takes the shared or exclusive side of
// `DaemonService::m_render_job_mutex` from the same policy that charges VRAM.
// DirectYUV disjoint chunks may overlap; monolithic and FullGraph jobs remain
// exclusive. The DeviceScheduler capacity is resolved from that authority.
//
// `daemon_service_ipc.cpp` carries the matching static_asserts next to the
// mutex, so the contract fails the build if only one side is changed.
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include "daemon/daemon_render_concurrency.hpp"

#include <array>
#include <cstdint>
#include <string>

using chronon3d::cli::derive_vram_working_set;
using chronon3d::cli::estimate_peak_vram_bytes;
using chronon3d::cli::kDefaultFrameRingDepth;
using chronon3d::cli::reconcile_vram_working_set;
using chronon3d::cli::VramReconciliation;
using chronon3d::cli::decide_render_admission;
using chronon3d::cli::kDefaultRenderJobClass;
using chronon3d::cli::kMaxConfigurableRenderJobSlots;
using chronon3d::cli::kMeasuredFullGraphPeakBytes;
using chronon3d::cli::kMeasuredDirectYuvPeakBytes;
using chronon3d::cli::kReferenceDeviceVramBytes;
using chronon3d::cli::kRenderJobExecutionSerialized;
using chronon3d::cli::reference_device_policy_input;
using chronon3d::cli::RenderAdmission;
using chronon3d::cli::RenderJobClass;
using chronon3d::cli::RenderPolicyInput;
using chronon3d::cli::RenderPolicyDecision;
using chronon3d::cli::kSerializedRenderJobSlots;
using chronon3d::cli::kTargetConcurrentRenderJobs;
using chronon3d::cli::resolve_device_requirements;
using chronon3d::cli::resolve_render_job_slots;

// The mutex is held for the whole job: one holder means one slot.
static_assert(kSerializedRenderJobSlots == 1U,
              "a single execution-domain mutex admits exactly one render job");

namespace {

constexpr std::uint64_t kGiB = 1024ULL * 1024ULL * 1024ULL;
// The RECORDED measurement, kept as an independent witness: the header declares
// the peak once (shared by the VRAM estimator and the concurrency policy) and
// the case below pins that declaration to this number, so a silent edit to the
// constant cannot quietly redefine what was measured.
constexpr std::uint64_t kObserved1080pPeak = 8800ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kDevice16GiB = 16ULL * kGiB;

} // namespace

TEST_CASE("render concurrency: an unset override advertises the safe chunk capacity") {
    for (const char* env : std::array<const char*, 2>{nullptr, ""}) {
        const auto resolution = resolve_render_job_slots(env);
        CHECK(resolution.slots == kTargetConcurrentRenderJobs);
        CHECK(resolution.requested == kTargetConcurrentRenderJobs);
        CHECK(resolution.requested_more_than_effective ==
              (kTargetConcurrentRenderJobs > resolution.slots));
    }
}

TEST_CASE("render concurrency: an override within the configured bound is honoured") {
    for (const char* env : {"2", "3", "8"}) {
        const auto resolution = resolve_render_job_slots(env);
        CHECK(resolution.slots == static_cast<std::uint32_t>(std::stoi(env)));
        CHECK_FALSE(resolution.requested_more_than_effective);
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
        CHECK(resolution.slots == kTargetConcurrentRenderJobs);
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

// ---------------------------------------------------------------------------
// Concurrency POLICY (S4).
//
// "Serialized" is not a bool with a comment any more, it is the verdict of
// decide_render_admission() over a measured device. These cases pin three
// things: which class may NEVER overlap (whatever the card), that the reference
// 16 GB device refuses overlap for EVERY class with the arithmetic as the
// reason, and that the refusal is a consequence of the numbers rather than a
// hardcoded answer — a device with room flips the verdict.
// ---------------------------------------------------------------------------

TEST_CASE("concurrency policy: a monolithic render is one execution domain on every device") {
    const std::array<std::uint64_t, 4> devices{
        8ULL * kGiB, kDevice16GiB, 48ULL * kGiB, 80ULL * kGiB};
    for (const auto total : devices) {
        const auto decision = decide_render_admission(RenderPolicyInput{
            .job_class = RenderJobClass::MonolithicSamePlan,
            .job_peak_vram_bytes = kMeasuredFullGraphPeakBytes,
            .device_total_vram_bytes = total,
            .device_free_vram_bytes = total,
        });
        CHECK(decision.admission == RenderAdmission::Serial);
        CHECK(decision.admitted_slots == kSerializedRenderJobSlots);
        CHECK_FALSE(decision.overlap_requested);
        CHECK(std::string(decision.reason).find("monolithic") != std::string::npos);
    }
}

TEST_CASE("concurrency policy: the reference device refuses overlap for every class, with the reason") {
    for (const auto job_class : {RenderJobClass::MonolithicSamePlan,
                                 RenderJobClass::ChunkedDisjointSamePlan,
                                 RenderJobClass::DifferentPlan}) {
        const auto decision = decide_render_admission(RenderPolicyInput{
            .job_class = job_class,
            .job_peak_vram_bytes = kMeasuredFullGraphPeakBytes,
            .device_total_vram_bytes = kReferenceDeviceVramBytes,
            .device_free_vram_bytes = kReferenceDeviceVramBytes,
        });
        CHECK(decision.admission == RenderAdmission::Serial);
        CHECK(decision.peak_footprints_that_fit == 1U);
        // Every refusal names its cause; the classes that were REFUSED ON
        // MEMORY must name the memory, while a monolithic render is refused
        // for a reason no card can change.
        CHECK_FALSE(std::string(decision.reason).empty());
        if (job_class != RenderJobClass::MonolithicSamePlan) {
            CHECK(std::string(decision.reason).find("VRAM") != std::string::npos);
        } else {
            CHECK(std::string(decision.reason).find("execution domain") != std::string::npos);
        }
    }
}

TEST_CASE("concurrency policy: measured DirectYUV chunks overlap on the reference device") {
    const auto decision = decide_render_admission(RenderPolicyInput{
        .job_class = RenderJobClass::ChunkedDisjointSamePlan,
        .job_peak_vram_bytes = kMeasuredDirectYuvPeakBytes,
        .device_total_vram_bytes = kReferenceDeviceVramBytes,
        .device_free_vram_bytes = kReferenceDeviceVramBytes,
    });
    CHECK(decision.admission == RenderAdmission::ParallelDisjoint);
    CHECK(decision.admitted_slots >= 2U);
    CHECK(decision.peak_footprints_that_fit >= 2U);
}

TEST_CASE("concurrency policy: a device with room flips the verdict for disjoint work only") {
    const std::uint64_t big_device = 8ULL * kMeasuredFullGraphPeakBytes;
    const auto chunked = decide_render_admission(RenderPolicyInput{
        .job_class = RenderJobClass::ChunkedDisjointSamePlan,
        .job_peak_vram_bytes = kMeasuredFullGraphPeakBytes,
        .device_total_vram_bytes = big_device,
        .device_free_vram_bytes = big_device,
    });
    CHECK(chunked.admission == RenderAdmission::ParallelDisjoint);
    CHECK(chunked.admitted_slots >= 2U);
    CHECK(chunked.overlap_requested);

    const auto different = decide_render_admission(RenderPolicyInput{
        .job_class = RenderJobClass::DifferentPlan,
        .job_peak_vram_bytes = kMeasuredFullGraphPeakBytes,
        .device_total_vram_bytes = big_device,
        .device_free_vram_bytes = big_device,
    });
    CHECK(different.admission == RenderAdmission::ParallelDisjoint);

    // ... and a monolithic render on the SAME device stays exclusive: the
    // card is not what makes a whole timeline splittable.
    const auto monolithic = decide_render_admission(RenderPolicyInput{
        .job_class = RenderJobClass::MonolithicSamePlan,
        .job_peak_vram_bytes = kMeasuredFullGraphPeakBytes,
        .device_total_vram_bytes = big_device,
        .device_free_vram_bytes = big_device,
    });
    CHECK(monolithic.admission == RenderAdmission::Serial);
}

TEST_CASE("concurrency policy: overlap is refused when the free memory cannot hold one job") {
    // Fitting twice on paper is not enough: the free memory the caller measured
    // must hold one full peak right now.
    const auto decision = decide_render_admission(RenderPolicyInput{
        .job_class = RenderJobClass::ChunkedDisjointSamePlan,
        .job_peak_vram_bytes = kMeasuredFullGraphPeakBytes,
        .device_total_vram_bytes = 4ULL * kMeasuredFullGraphPeakBytes,
        .device_free_vram_bytes = kMeasuredFullGraphPeakBytes / 2ULL,
    });
    CHECK(decision.admission == RenderAdmission::Serial);
    CHECK(std::string(decision.reason).find("free device memory") != std::string::npos);
}

TEST_CASE("concurrency policy: an unknown peak is refused, never assumed small") {
    const auto decision = decide_render_admission(RenderPolicyInput{
        .job_class = RenderJobClass::ChunkedDisjointSamePlan,
        .job_peak_vram_bytes = 0,
        .device_total_vram_bytes = kReferenceDeviceVramBytes,
        .device_free_vram_bytes = kReferenceDeviceVramBytes,
    });
    CHECK(decision.admission == RenderAdmission::Serial);
    CHECK(decision.peak_footprints_that_fit == 0U);
}

TEST_CASE("concurrency policy: the derived execution-domain flag agrees with the policy") {
    // One authority: the flag the slot resolution clamps with is the verdict of
    // the policy over the reference device, so a change to one is a change to
    // the other.
    const bool policy_serial = decide_render_admission(reference_device_policy_input()).admission ==
                               RenderAdmission::Serial;
    CHECK(kRenderJobExecutionSerialized == policy_serial);
    CHECK(kDefaultRenderJobClass == RenderJobClass::MonolithicSamePlan);
}

// ---------------------------------------------------------------------------
// VRAM admission truth (P0-2).
//
// The admission estimator used to return ~1 GB for a 1080p FullGraph while the
// measured peak was ~8.8 GB — an order of magnitude low. Behind the mutex that
// was invisible; the moment the mutex comes off the DeviceScheduler would admit
// jobs against a working set that does not exist. These cases pin the estimate
// to the observed peak and pin that a 16 GB device cannot hold two of them.
// ---------------------------------------------------------------------------

namespace {

} // namespace

TEST_CASE("vram estimate: 1080p FullGraph reflects the observed peak, not the rejected 1 GB model") {
    // The single declared peak must still be the measured one.
    CHECK(kMeasuredFullGraphPeakBytes == kObserved1080pPeak);
    const auto vram = estimate_peak_vram_bytes(1920, 1080, false, true);
    CHECK(vram >= kObserved1080pPeak);
    // The rejected model clamped to 1 GB; anything near it means admission is
    // back to promising memory the device does not have.
    CHECK(vram > 4ULL * kGiB);
}

TEST_CASE("vram estimate: a 16 GB device cannot admit two 1080p FullGraph jobs") {
    const auto per_job = estimate_peak_vram_bytes(1920, 1080, false, true);
    CHECK(per_job * 2ULL > kDevice16GiB);
}

TEST_CASE("vram estimate: scales with pixels, keeps a floor and claims nothing without native NVENC") {
    const auto hd = estimate_peak_vram_bytes(1920, 1080, false, true);
    CHECK(estimate_peak_vram_bytes(3840, 2160, false, true) > hd);
    CHECK(estimate_peak_vram_bytes(1280, 720, false, true) < hd);
    // Unknown dimensions fall back to the reference size instead of 0.
    CHECK(estimate_peak_vram_bytes(0, 0, false, true) == hd);
    // Small resolutions still reserve meaningfully (2 GB floor).
    CHECK(estimate_peak_vram_bytes(640, 360, false, true) >= 2ULL * kGiB);
    // A pure software job claims no VRAM at all.
    CHECK(estimate_peak_vram_bytes(1920, 1080, false, false) == 0U);
    CHECK(estimate_peak_vram_bytes(1920, 1080, true, false) == 0U);
    // DirectYUV keeps its own conservative floor.
    CHECK(estimate_peak_vram_bytes(1920, 1080, true, true) >= 1ULL * kGiB);
}

// ---------------------------------------------------------------------------
// Compiled-footprint working set (P0-2).
//
// The estimate must come from what the job compiled, not from a constant
// calibrated on a single observation.  These cases pin the composition model:
// the compiled term is used when a footprint exists, the measured calibration
// stays as a floor the charge can never fall under, and the still-unexplained
// gap is reported instead of hidden.
// ---------------------------------------------------------------------------

TEST_CASE("vram working set: without a compiled footprint the calibration is the estimate") {
    const auto ws = derive_vram_working_set(0, 0, 1920, 1080, false, true);
    CHECK_FALSE(ws.derived_from_compiled);
    CHECK(ws.derived_bytes == 0U);
    CHECK(ws.estimated_peak_bytes == estimate_peak_vram_bytes(1920, 1080, false, true));
    // Nothing compiled ⇒ the whole charge is unexplained by the compiled model.
    CHECK(ws.calibration_gap_bytes() == ws.calibration_floor_bytes);
    CHECK(ws.calibration_gap_ratio() == 1.0);
}

TEST_CASE("vram working set: the compiled footprint is used and the calibration floors it") {
    constexpr std::uint64_t kFootprint = 40ULL * 1024ULL * 1024ULL;  // 40 MB plan
    const auto ws = derive_vram_working_set(kFootprint, 0, 1920, 1080, false, true);
    REQUIRE(ws.derived_from_compiled);
    CHECK(ws.compiled_bytes == kFootprint);
    // One ring depth per compiled footprint, mirroring the submission ring.
    CHECK(ws.frame_ring_bytes == kFootprint * kDefaultFrameRingDepth);
    CHECK(ws.safety_margin_bytes > 0U);
    CHECK(ws.estimated_peak_bytes == ws.calibration_floor_bytes);
    // The compiled plan explains only a fraction of the measured peak: that
    // unexplained remainder is what the gap reports.
    CHECK(ws.calibration_gap_bytes() > 6ULL * kGiB);
    CHECK(ws.calibration_gap_ratio() > 0.9);
}

TEST_CASE("vram working set: a footprint larger than the calibration becomes the charge") {
    constexpr std::uint64_t kHugeFootprint = 2ULL * kGiB;
    const auto ws = derive_vram_working_set(kHugeFootprint, 0, 1920, 1080, false, true);
    REQUIRE(ws.derived_from_compiled);
    CHECK(ws.derived_bytes > ws.calibration_floor_bytes);
    CHECK(ws.estimated_peak_bytes == ws.derived_bytes);
    CHECK(ws.calibration_gap_bytes() == 0U);
    // Monotonic: more compiled bytes can only ever raise the charge.
    const auto bigger = derive_vram_working_set(kHugeFootprint * 2ULL, 0, 1920, 1080, false, true);
    CHECK(bigger.estimated_peak_bytes > ws.estimated_peak_bytes);
}

TEST_CASE("vram working set: a software job has no calibration but still owns its compiled plan") {
    // No native NVENC: the video calibration does not describe this lane.
    const auto compiled = derive_vram_working_set(40ULL * 1024ULL * 1024ULL, 0, 1920, 1080, false, false);
    CHECK(compiled.calibration_floor_bytes == 0U);
    CHECK(compiled.derived_from_compiled);
    CHECK(compiled.estimated_peak_bytes == compiled.derived_bytes);
    // Nothing compiled and no hardware lane: the job claims no device memory.
    const auto nothing = derive_vram_working_set(0, 0, 1920, 1080, false, false);
    CHECK(nothing.estimated_peak_bytes == 0U);
    CHECK_FALSE(nothing.derived_from_compiled);
}

TEST_CASE("vram reconciliation: under-admission is detected, everything else is not") {
    constexpr std::uint64_t kFootprint = 40ULL * 1024ULL * 1024ULL;
    const auto ws = derive_vram_working_set(kFootprint, 0, 1920, 1080, false, true);

    // No compiled footprint yet: the calibration is all there is to reconcile.
    CHECK(reconcile_vram_working_set(derive_vram_working_set(0, 0, 1920, 1080, false, true),
                                     8ULL * kGiB)
              .verdict == VramReconciliation::CalibrationOnly);
    // Not admission-controlled: nothing to compare against.
    CHECK(reconcile_vram_working_set(ws, 0).verdict == VramReconciliation::CalibrationOnly);
    // The charge covers the compiled model.
    const auto consistent =
        reconcile_vram_working_set(ws, ws.estimated_peak_bytes);
    CHECK(consistent.verdict == VramReconciliation::Consistent);
    CHECK_FALSE(consistent.fail_loud());
    // The job compiled to more than it was admitted with.
    const auto under = reconcile_vram_working_set(ws, ws.derived_bytes - 1U);
    CHECK(under.verdict == VramReconciliation::UnderCharged);
    CHECK(under.fail_loud());
    CHECK(std::string{under.verdict_name()} == "UNDER-CHARGED");
}

// ---------------------------------------------------------------------------
// Single DeviceSelectionRequirements policy authority (P0-4).
//
// The daemon and the CLI fallback used to build the requirements from two
// different policies, so a request could be admitted by one path and refused by
// the other. Both now call resolve_device_requirements(); these cases pin the
// policy that function expresses.
// ---------------------------------------------------------------------------

TEST_CASE("device requirements: one authority pins codec, interop and VRAM policy") {
    const auto req = resolve_device_requirements(1920, 1080, false, true, "auto", "");
    CHECK(req.cuda);
    CHECK(req.nvenc);
    CHECK(req.nvdec);
    CHECK(req.nv12);
    CHECK(req.vulkan_interop);
    CHECK(req.h264);
    CHECK_FALSE(req.hevc);
    CHECK_FALSE(req.av1);
    CHECK(req.resources.vram_bytes == estimate_peak_vram_bytes(1920, 1080, false, true));
    CHECK(req.resources.render_job_slots == kSerializedRenderJobSlots);
    CHECK(req.resources.nvenc_sessions == 1U);
    CHECK(req.resources.nvdec_sessions == 1U);
}

TEST_CASE("device requirements: codec selection follows the requested codec only") {
    for (const char* codec : {"hevc", "h265", "libx265", "hevc_nvenc"}) {
        const auto req = resolve_device_requirements(1920, 1080, false, true, codec, "");
        CHECK(req.hevc);
        CHECK_FALSE(req.h264);
        CHECK_FALSE(req.av1);
    }
    const auto av1 = resolve_device_requirements(1920, 1080, false, true, "av1_nvenc", "");
    CHECK(av1.av1);
    CHECK_FALSE(av1.h264);
    CHECK_FALSE(av1.hevc);
    // An unknown codec claims no encoder capability rather than guessing one.
    const auto unknown = resolve_device_requirements(1920, 1080, false, true, "vp9", "");
    CHECK_FALSE(unknown.h264);
    CHECK_FALSE(unknown.hevc);
    CHECK_FALSE(unknown.av1);
}

TEST_CASE("device requirements: require_direct_yuv drops Vulkan interop from the same request") {
    const auto direct =
        resolve_device_requirements(1920, 1080, true, true, "h264", "require_direct_yuv");
    CHECK_FALSE(direct.vulkan_interop);
    CHECK(direct.cuda);
    CHECK(direct.nvenc);
    CHECK(direct.resources.vram_bytes == estimate_peak_vram_bytes(1920, 1080, true, true));
}

TEST_CASE("device requirements: a software job claims no hardware and no VRAM") {
    const auto software = resolve_device_requirements(1920, 1080, false, false, "h264", "");
    CHECK_FALSE(software.cuda);
    CHECK_FALSE(software.nvenc);
    CHECK_FALSE(software.nvdec);
    CHECK_FALSE(software.nv12);
    CHECK_FALSE(software.vulkan_interop);
    CHECK_FALSE(software.h264);
    CHECK(software.resources.nvenc_sessions == 0U);
    CHECK(software.resources.vram_bytes == 0U);
}

TEST_CASE("device requirements: the direct and FullGraph policies differ only by the recorded lane") {
    // Same sizes and same codec: the two lanes must not silently diverge in a
    // field the scheduler consumes.
    const auto full = resolve_device_requirements(1920, 1080, false, true, "h264", "");
    const auto direct =
        resolve_device_requirements(1920, 1080, true, true, "h264", "require_direct_yuv");
    CHECK(full.cuda == direct.cuda);
    CHECK(full.nvenc == direct.nvenc);
    CHECK(full.nvdec == direct.nvdec);
    CHECK(full.h264 == direct.h264);
    CHECK(full.vulkan_interop != direct.vulkan_interop);
    CHECK(full.resources.vram_bytes != direct.resources.vram_bytes);
}
