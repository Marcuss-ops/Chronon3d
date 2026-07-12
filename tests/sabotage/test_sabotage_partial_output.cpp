// ============================================================================
// tests/sabotage/test_sabotage_partial_output.cpp
// ============================================================================
//
// Sabotage scenario #9: render job produced N out of M expected
// frames (partial output). The render job's `commit()` MUST detect
// `produced_frame_count < requested_frame_count` and surface an
// explicit `PartialOutput` Result<> error rather than (a) silently
// committing a truncated timeline (which downstream playback will
// play with a "premature end" sigil that's confusing for users) or
// (b) silently retrying (which masks the underlying rot).
//
// Engine signature (verified-real surface): `RenderJob::commit()`
// exists at `include/chronon3d/timeline/render_job.hpp` (note:
// CORRECTED path — fabricated claim of `scene/render_job.hpp`
// REMOVED; machine-verified via grep 2026-07-12 the real
// canonical path is `timeline/render_job.hpp`). The
// `produced_frame_count` invariant is currently a doc-block note,
// NOT a type-system invariant, per the TICKET-PROJECTION-V1-EXT
// finding on Camera V1 — fixing that forward-point will be part
// of TICKET-SABOTAGE-PRODUCTION-HOOKS Phase 2.
//
// Expected fail-loud path: PartialOutput Result<> error +
// the render job commit MUST NOT silently succeed (the trunc-
// ation is the rot, not a "best-effort" attempt).
//
// Per user spec "Ogni test exit non-zero + identifica comp/layer/node
// + fail-loud":
//   - INFO lines identify comp/layer/node (stub labels).
//   - FAIL_CHECK forces the doctest assertion to fail -> exit 1.
// ============================================================================
#include <doctest/doctest.h>

namespace chaos::sabotage::partial_output {

// Engine-side trigger fingerprint. Implementation forward-point:
// TICKET-SABOTAGE-PRODUCTION-HOOKS — the production hook will
// detect `produced_frame_count < requested_frame_count` on the
// verified-real `RenderJob::commit()` (header at
// include/chronon3d/timeline/render_job.hpp — corrected path)
// to emit PartialOutput.
bool trigger_partial_output_failure() {
    return false;
}

} // namespace chaos::sabotage::partial_output

TEST_CASE(
    "sabotage/partial_output "
    "[comp=RenderJob, layer=RenderLayer, "
    "node=RENDER_JOB_commit_produced_lt_requested]"
) {
    INFO("Comp=RenderJob [verified-real at "
         "include/chronon3d/timeline/render_job.hpp — corrected]")
    INFO("Layer=RenderLayer [PLANNED TICKET-SABOTAGE-PRODUCTION-HOOKS]")
    INFO("Node=RenderJob::commit() produced_frame_count < "
         "requested_frame_count -> PartialOutput")
    INFO("Sabotage scenario: render_job::commit() found that "
         "produced_frame_count < requested_frame_count (N of M "
         "frames) -> PartialOutput")
    INFO("Forward-point: TICKET-SABOTAGE-PRODUCTION-HOOKS")
    FAIL_CHECK(
        "sabotage/partial_output: trigger returns false -- "
        "production fail-loud hook not yet wired "
        "(TICKET-SABOTAGE-PRODUCTION-HOOKS forward-point)"
    );
}
