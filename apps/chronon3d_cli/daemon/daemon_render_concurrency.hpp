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

#include <chronon3d/runtime/device_scheduler.hpp>

namespace chronon3d::cli {

/// Slots the serialized execution domain actually admits.
inline constexpr std::uint32_t kSerializedRenderJobSlots = 1;

/// Peak device memory observed for ONE 1080p FullGraph video job (8.8 GiB). It
/// is the calibration floor of `estimate_peak_vram_bytes` and the per-job peak
/// the concurrency policy charges. Single declaration, because two numbers for
/// one measurement is how admission and the serialization decision drift.
inline constexpr std::uint64_t kMeasuredFullGraphPeakBytes =
    8800ULL * 1024ULL * 1024ULL;

// ── Concurrency POLICY — one decision, three classes, named reasons ─────────
//
// "Serialized" used to be a bare `true`, with the justification in a comment
// and the consequences spread across a mutex, a resolution function and a
// static_assert. That is a decision without an owner: nothing could answer
// "would a chunked render be allowed to overlap?" or "under what measurement
// would that change?", and the only way to revisit it was to re-read the
// comment.
//
// The policy below makes the decision DATA. It is defined on the axis that
// actually matters — WHICH KIND of job is asking — and gated on the measured
// device headroom, so the verdict changes when the numbers change instead of
// when someone remembers to flip a bool.

/// The kind of render job asking to run. It is the axis the policy is defined
/// on: these three cases do not share the same safety argument.
enum class RenderJobClass : std::uint8_t {
    /// The whole timeline of one plan in one job. Two of these are the same
    /// work over the same plan-derived caches and the same frame ring, so they
    /// are ONE execution domain regardless of available memory.
    MonolithicSamePlan,
    /// One deterministic window (start/end frame) of a plan whose other
    /// windows are run by other jobs. Disjoint windows touch disjoint frames,
    /// which is what makes overlap meaningful at all — this is the class the
    /// temporal chunking (I1) produces.
    ChunkedDisjointSamePlan,
    /// A different plan: independent timeline, independent caches.
    DifferentPlan,
};

/// Whether a job may overlap other jobs in the execution domain.
enum class RenderAdmission : std::uint8_t {
    /// Hold the domain exclusively: no other render job may run alongside.
    Serial,
    /// May hold the domain on the shared side: overlap is permitted with other
    /// jobs whose own verdict is also ParallelDisjoint.
    ParallelDisjoint,
};

/// The facts the policy decides on. Every field is measured or passed in;
/// there is deliberately no "typical render" default.
struct RenderPolicyInput {
    RenderJobClass job_class{RenderJobClass::MonolithicSamePlan};
    /// Peak device memory of ONE job of this class (the same estimate
    /// admission charges: `derive_vram_working_set` / the calibration floor).
    std::uint64_t job_peak_vram_bytes{0};
    /// Total device memory.
    std::uint64_t device_total_vram_bytes{0};
    /// Device memory free right now.
    std::uint64_t device_free_vram_bytes{0};
};

/// The verdict plus the reason it was reached. The reason is part of the
/// contract: a serialized job must be able to say WHY it is serialized.
struct RenderPolicyDecision {
    RenderAdmission admission{RenderAdmission::Serial};
    /// Slots this verdict implies (Serial ⇒ 1).
    std::uint32_t admitted_slots{kSerializedRenderJobSlots};
    /// True when the job asked for something other than an exclusive domain.
    bool overlap_requested{false};
    /// How many of this class's peak footprints fit in the device (0 = not even
    /// one). It is the number the verdict was made on, not a restatement of it.
    std::uint32_t peak_footprints_that_fit{0};
    /// Static, human-readable cause. Never a formatted allocation.
    const char* reason{""};
};

/// The class the daemon charges a job as, when the producer does not declare
/// one. No producer declares chunk membership today (RenderingGen submits whole
/// clips on the clip lane and whole plans on the overlay lane), so every job is
/// monolithic — and a monolithic render is Serial by construction, which is why
/// the verdict guard in the RENDER_JOB path never fires in production. It is a
/// named constant rather than an inline literal because it is the ONE place a
/// future chunk producer must change together with the overlap mechanism.
inline constexpr RenderJobClass kDefaultRenderJobClass =
    RenderJobClass::MonolithicSamePlan;

/// The reference deployment the serialization decision was taken on:
/// one 1080p FullGraph render on a 16 GB device (see
/// `estimate_peak_vram_bytes`). A different card re-decides by data.
inline constexpr std::uint64_t kReferenceDeviceVramBytes =
    16ULL * 1024ULL * 1024ULL * 1024ULL;

/// Decide whether a job may overlap. Pure and `constexpr`, so the default
/// decision below is DERIVED from it rather than restated next to it.
///
/// The rules, in order:
///  1. a monolithic same-plan render is one execution domain by construction;
///  2. any other class may overlap only while a second full peak footprint
///     fits on the device AND one peak still fits in the free memory the
///     caller measured (a job that does not fit even alone would fail the
///     render, not the policy);
///  3. otherwise it is serialized, with the arithmetic in `reason`.
[[nodiscard]] constexpr RenderPolicyDecision decide_render_admission(
    const RenderPolicyInput& input) {
    RenderPolicyDecision decision;
    decision.overlap_requested =
        input.job_class != RenderJobClass::MonolithicSamePlan;

    if (input.job_peak_vram_bytes > 0 && input.device_total_vram_bytes > 0) {
        decision.peak_footprints_that_fit = static_cast<std::uint32_t>(
            input.device_total_vram_bytes / input.job_peak_vram_bytes);
    }

    if (input.job_class == RenderJobClass::MonolithicSamePlan) {
        decision.reason =
            "monolithic same-plan render: the whole timeline is one execution domain";
        return decision;
    }
    if (input.job_peak_vram_bytes == 0) {
        decision.reason =
            "no per-job peak was supplied: overlap cannot be charged, so it is refused";
        return decision;
    }
    if (decision.peak_footprints_that_fit < 2U) {
        decision.reason =
            "two peak footprints do not fit on the device: overlap would exhaust VRAM";
        return decision;
    }
    if (input.device_free_vram_bytes < input.job_peak_vram_bytes) {
        decision.reason =
            "one peak footprint does not fit in the free device memory right now";
        return decision;
    }
    decision.admission = RenderAdmission::ParallelDisjoint;
    decision.admitted_slots = decision.peak_footprints_that_fit;
    decision.reason =
        "disjoint work units with room for a second peak footprint on the device";
    return decision;
}

/// The policy input of the reference deployment: the most favourable class (a
/// disjoint chunk of a plan) charged with the MEASURED 1080p FullGraph peak on
/// the reference 16 GB device, with the device otherwise free. Charging the
/// most favourable class means a Serial verdict here is Serial for every
/// class, which is what makes the derived default below conservative by
/// construction.
[[nodiscard]] constexpr RenderPolicyInput reference_device_policy_input() {
    RenderPolicyInput input;
    input.job_class = RenderJobClass::ChunkedDisjointSamePlan;
    input.job_peak_vram_bytes = kMeasuredFullGraphPeakBytes;
    input.device_total_vram_bytes = kReferenceDeviceVramBytes;
    input.device_free_vram_bytes = kReferenceDeviceVramBytes;
    return input;
}

/// Does the execution domain admit more than one render job on the reference
/// device? DERIVED from the policy, never restated: `resolve_render_job_slots`
/// clamps to this and the daemon chooses the lock side from the same verdict.
inline constexpr bool kRenderJobExecutionSerialized =
    decide_render_admission(reference_device_policy_input()).admission ==
    RenderAdmission::Serial;

/// Production target once per-job VRAM pressure allows a second exporter.
inline constexpr std::uint32_t kTargetConcurrentRenderJobs = 2;

/// Upper bound accepted from the environment (mirrors the historical clamp).
inline constexpr std::uint32_t kMaxConfigurableRenderJobSlots = 8;

/// Name of the REQUESTED render-job capacity, NOT a lever: it says how many
/// jobs the operator would like to run, and the resolution below clamps it to
/// what the execution domain admits (`kSerializedRenderJobSlots`). While
/// `kRenderJobExecutionSerialized` is true there is no value of this variable
/// that produces `slots == 2`; the daemon reports the clamp once (see
/// `advertised_render_job_slots`) and carries on with ONE slot. The VRAM A/B is
/// measured with `renderinggen/bin/vram-probe -mode two-runtime-pool`, which
/// owns its daemons and never goes through this mutex — not by exporting this
/// variable. Lifting the serialization is the code change described above.
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

// ── VRAM + DeviceSelectionRequirements — single policy authority (P0-2/4) ──
//
// The daemon estimated 1 GB for 1080p FullGraph while the measured peak is
// ~8.8 GB.  Admission must not promise 2 jobs on a 16 GB card while the
// estimator is off by an order of magnitude.  This single function is the
// ONLY place that builds DeviceSelectionRequirements and the VRAM estimate
// for both the daemon IPC admission and the CLI fallback path, so policy
// cannot drift between them.

//  P0-2: conservative model — 1080p FullGraph was observed at 8.8 GB peak,
//  so the 512 MB + 8*frame (≈1 GB) model is rejected.  FullGraph now
//  scales from that observed point; DirectYUV keeps a smaller but still
//  conservative 1 GB floor.
//
//  The observed point is a named constant (`kMeasuredFullGraphPeakBytes`) so
//  the concurrency policy and this estimator cannot quote two different
//  numbers for the same measurement.
//
//  This is the CALIBRATION FLOOR, not the estimate.  The estimate is
//  `derive_vram_working_set()` below, which scales the job's own compiled
//  footprint and takes this calibration as a floor it can never fall under.
[[nodiscard]] inline std::uint64_t estimate_peak_vram_bytes(
    std::uint32_t width, std::uint32_t height,
    bool direct_yuv, bool native_nvenc) {
    if (!native_nvenc) return 0;
    if (width == 0) width = 1920;
    if (height == 0) height = 1080;
    if (direct_yuv) {
        // DirectYUV: NVDEC + NVENC CUDA pools + one device image cache,
        // but no Vulkan transient ring.  Keep 1 GB floor to avoid admitting
        // many DirectYUV jobs while the true peak is still being measured.
        return 1024ULL * 1024ULL * 1024ULL;
    }
    // FullGraph: observed 8.8 GB at 1920x1080.  Scale linearly with pixel
    // count, clamp to 2 GB minimum so 720p still reserves meaningfully.
    // When a compiled plan is available this path is superseded by the
    // CompiledResourceTable-derived estimate in the media layer.
    const std::uint64_t kObserved1080pPeak = kMeasuredFullGraphPeakBytes;
    constexpr std::uint64_t kRefPixels = 1920ULL * 1080ULL;
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
    const std::uint64_t scaled =
        (kObserved1080pPeak * pixels + kRefPixels - 1) / kRefPixels;
    return std::max<std::uint64_t>(scaled, 2ULL * 1024ULL * 1024ULL * 1024ULL);
}

// ── Derived working set (P0-2) ──────────────────────────────────────────────
//
// The admission estimate must come from what the job actually compiled, not
// from a constant calibrated on one observation.  `ResourcePlanState`
// (`planned_physical_bytes` / `peak_live_bytes`) is the persistent byte
// footprint the compiler certified; the categories below mirror
// `graph::DeviceMemoryPlan` one-for-one (physical slots, frame-slot ring,
// decoder/encoder/atlas/scratch budgets, safety margin) so the CLI and the
// graph-layer admission control express the same model instead of two.
//
// The one thing the compiled footprint does NOT explain is the difference to
// the measured peak: at 1080p the compiled term is a fraction of the observed
// ~8.8 GB, most of which is ring residency, hwframe pools and driver
// fragmentation.  So the derived value is floored by the calibration and the
// remaining gap is exposed (`calibration_gap_bytes`) instead of being hidden.
// Admission may never be decided on the derived term alone while that gap is
// an order of magnitude.

/// Per-slot ring depth charged on top of the compiled footprint.  Mirrors the
/// Vulkan submission ring depth (src/backends/vulkan/vulkan_ring_depths.hpp):
/// raising `CHRONON_GPUSLOT_DEPTH` cannot create submission depth the ring does
/// not have, it only multiplies residency.
inline constexpr std::uint32_t kDefaultFrameRingDepth = 6;

/// Safety margin fraction, mirroring `graph::DeviceMemoryPlan`.
inline constexpr double kVramSafetyFraction = 0.15;

/// Named categories of the estimated device working set of ONE job.  Every
/// non-zero figure is either the job's own compiled footprint or an explicitly
/// passed budget — never a guess about a "typical" render.
struct VramWorkingSet {
    /// `ResourcePlan::planned_physical_bytes` (+ `CompiledResourceTable`).
    std::uint64_t compiled_bytes{0};
    /// `ResourcePlan::peak_live_bytes` — transient peak above the plan.
    std::uint64_t peak_live_bytes{0};
    /// Compiled footprint × frame-slot ring depth.
    std::uint64_t frame_ring_bytes{0};
    std::uint64_t decoder_budget_bytes{0};
    std::uint64_t encoder_budget_bytes{0};
    std::uint64_t atlas_budget_bytes{0};
    std::uint64_t scratch_budget_bytes{0};
    std::uint64_t safety_margin_bytes{0};
    /// Sum of the compiled-derived categories (before the floor).
    std::uint64_t derived_bytes{0};
    /// The measured calibration below which admission must never go.
    std::uint64_t calibration_floor_bytes{0};
    /// What admission is charged: `max(derived_bytes, calibration_floor_bytes)`.
    std::uint64_t estimated_peak_bytes{0};
    /// True when a compiled footprint was supplied and used.
    bool derived_from_compiled{false};

    /// How far the compiled model still is from the measured reality.  This is
    /// the number that has to shrink before the calibration floor can be
    /// dropped: it is precisely the part of the working set the compiled plan
    /// does not account for.
    [[nodiscard]] std::uint64_t calibration_gap_bytes() const noexcept {
        return calibration_floor_bytes > derived_bytes
                   ? calibration_floor_bytes - derived_bytes
                   : 0U;
    }

    /// Fraction of the charged estimate that is still unexplained (0..1).
    [[nodiscard]] double calibration_gap_ratio() const noexcept {
        if (estimated_peak_bytes == 0) return 0.0;
        return static_cast<double>(calibration_gap_bytes()) /
               static_cast<double>(estimated_peak_bytes);
    }
};

/// Compose the estimated device working set of ONE job.
///
/// `planned_physical_bytes` / `peak_live_bytes` come from the job's compiled
/// plan; the codec/atlas/scratch budgets come from whichever authority owns
/// those pools.  When no compiled footprint is supplied (`0`) the result is the
/// calibration only, which is what a caller that has not compiled yet can
/// honestly claim — `derived_from_compiled` records which one happened.
[[nodiscard]] inline VramWorkingSet derive_vram_working_set(
    std::uint64_t planned_physical_bytes,
    std::uint64_t peak_live_bytes,
    std::uint32_t width,
    std::uint32_t height,
    bool direct_yuv,
    bool native_nvenc,
    std::uint64_t decoder_budget = 0,
    std::uint64_t encoder_budget = 0,
    std::uint64_t atlas_budget = 0,
    std::uint64_t scratch_budget = 0,
    std::uint32_t frame_ring_depth = kDefaultFrameRingDepth) {
    VramWorkingSet ws;
    ws.peak_live_bytes = peak_live_bytes;

    // The video calibration describes the native-NVENC video lane only.  A
    // software / still job has no such measurement behind it.
    if (native_nvenc) {
        ws.calibration_floor_bytes =
            estimate_peak_vram_bytes(width, height, direct_yuv, native_nvenc);
    }

    if (planned_physical_bytes != 0) {
        ws.derived_from_compiled = true;
        ws.compiled_bytes = planned_physical_bytes;
        ws.frame_ring_bytes = planned_physical_bytes * frame_ring_depth;
        ws.decoder_budget_bytes = decoder_budget;
        ws.encoder_budget_bytes = encoder_budget;
        ws.atlas_budget_bytes = atlas_budget;
        ws.scratch_budget_bytes = scratch_budget;

        const std::uint64_t base = ws.compiled_bytes + ws.peak_live_bytes +
                                   ws.frame_ring_bytes + ws.decoder_budget_bytes +
                                   ws.encoder_budget_bytes + ws.atlas_budget_bytes +
                                   ws.scratch_budget_bytes;
        ws.safety_margin_bytes = static_cast<std::uint64_t>(
            static_cast<double>(base) * kVramSafetyFraction);
        ws.derived_bytes = base + ws.safety_margin_bytes;
    } else if (!native_nvenc) {
        // Nothing compiled and no video lane: the job claims no device memory.
        return ws;
    }

    // Admission is charged the larger of the compiled model and the measured
    // calibration: the compiled term may not under-promise against reality.
    ws.estimated_peak_bytes = ws.derived_bytes > ws.calibration_floor_bytes
                                  ? ws.derived_bytes
                                  : ws.calibration_floor_bytes;
    return ws;
}

/// Reconciliation between the compiled model and what admission actually
/// charged.  This is the `estimated_peak_vram` vs compiled/observed gate: a job
/// that compiles to MORE than it was admitted with is a real defect (admission
/// promised memory the job cannot fit in) and must be loud, not silent.
enum class VramReconciliation : std::uint8_t {
    /// No compiled footprint supplied yet — the calibration IS the estimate.
    CalibrationOnly,
    /// The compiled model does not exceed what admission charged.
    Consistent,
    /// The compiled footprint exceeds the charged estimate: under-admission.
    UnderCharged,
};

/// Outcome of reconciling a job's compiled working set against its admission.
struct VramReconciliationResult {
    VramWorkingSet working_set;
    VramReconciliation verdict{VramReconciliation::CalibrationOnly};
    /// What admission reserved for this job (0 = not admission-controlled).
    std::uint64_t admitted_bytes{0};

    [[nodiscard]] bool fail_loud() const noexcept {
        return verdict == VramReconciliation::UnderCharged;
    }

    [[nodiscard]] const char* verdict_name() const noexcept {
        switch (verdict) {
            case VramReconciliation::Consistent:    return "consistent";
            case VramReconciliation::UnderCharged:  return "UNDER-CHARGED";
            case VramReconciliation::CalibrationOnly: break;
        }
        return "calibration-only";
    }
};

/// Compare the compiled working set of a job against what admission charged it.
/// One-sided by construction: it can only detect under-admission, because the
/// calibration floor means the charge is normally far ABOVE the compiled term.
/// The gap it reports is what has to shrink before the floor can be dropped.
[[nodiscard]] inline VramReconciliationResult reconcile_vram_working_set(
    const VramWorkingSet& working_set, std::uint64_t admitted_bytes) {
    VramReconciliationResult result;
    result.working_set = working_set;
    result.admitted_bytes = admitted_bytes;
    if (!working_set.derived_from_compiled || admitted_bytes == 0) {
        // `admitted_bytes == 0` means this job was never admission-controlled
        // (no scheduler charge), so there is nothing to reconcile against.
        result.verdict = VramReconciliation::CalibrationOnly;
    } else if (working_set.derived_bytes > admitted_bytes) {
        result.verdict = VramReconciliation::UnderCharged;
    } else {
        result.verdict = VramReconciliation::Consistent;
    }
    return result;
}

/// Single authority for building DeviceSelectionRequirements.  Both
/// `daemon_service_ipc.cpp` (daemon admission) and
/// `pipe_export_pipeline_setup.cpp` (CLI fallback) must call this — no
/// second ad-hoc builder is allowed.
[[nodiscard]] inline chronon3d::runtime::DeviceSelectionRequirements
resolve_device_requirements(
    std::uint32_t width, std::uint32_t height,
    bool direct_yuv, bool native_nvenc,
    const std::string& codec,
    const std::string& hot_path_mode) {
    const std::uint64_t vram = estimate_peak_vram_bytes(width, height, direct_yuv, native_nvenc);
    chronon3d::runtime::DeviceSelectionRequirements req;
    req.resources.compute_units = native_nvenc ? 0.5f : 0.0f;
    req.cuda = native_nvenc;
    req.vulkan_interop = native_nvenc && hot_path_mode != "require_direct_yuv";
    req.nvenc = native_nvenc;
    req.nv12 = native_nvenc;
    req.resources.nvenc_sessions = native_nvenc ? 1U : 0U;
    req.resources.render_job_slots = 1U;
    req.resources.vram_bytes = vram;
    req.resources.pcie_bandwidth = native_nvenc ? 0.25f : 0.0f;
    req.h264 = native_nvenc && (codec == "auto" || codec == "h264" || codec == "libx264" || codec == "h264_nvenc");
    req.hevc = native_nvenc && (codec == "hevc" || codec == "h265" || codec == "libx265" || codec == "hevc_nvenc");
    req.av1  = native_nvenc && (codec == "av1"  || codec == "libsvtav1" || codec == "av1_nvenc");
    // nvdec is implied by native_nvenc for now
    req.nvdec = native_nvenc;
    req.resources.nvdec_sessions = native_nvenc ? 1U : 0U;
    return req;
}

} // namespace chronon3d::cli
