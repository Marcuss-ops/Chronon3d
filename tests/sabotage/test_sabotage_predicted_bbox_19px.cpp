// ============================================================================
// tests/sabotage/test_sabotage_predicted_bbox_19px.cpp
// ============================================================================
//
// Sabotage scenario #3: text-visibility audit returns
// `predicted_bbox == 19px` (or any value < the canonical 20px FU04
// floor). The FU04 visibility pipeline contractually requires
// predicted_bbox >= 20px to gate tile_pruning; below the floor, the
// text is at risk of being pruned even when the world_ink_bbox is
// non-empty, causing "invisible-but-rendered" rot.
//
// Engine signature (verified-real surface): `predicted_bbox` member
// exists at `include/chronon3d/text/text_visibility_audit.hpp`
// (canonical), plus 4 additional surfaces:
//   include/chronon3d/backends/software/shape_processor.hpp
//   include/chronon3d/backends/software/software_render_session.hpp
//   include/chronon3d/render_graph/preflight/preflight_render_graph.hpp
//   include/chronon3d/render_graph/nodes/mask_node.hpp
// (machine-verified via grep 2026-07-12). The Phase 2 fail-loud
// HOOK signature is PLANNED (TICKET-SABOTAGE-PRODUCTION-HOOKS):
// the trigger reads the verified-real `predicted_bbox` member surface
// at TICKET-TEXT-VISIBILITY-PIPELINE FU04 and emits
// BelowVisibilityFloor Result<> on violation.
//
// Expected fail-loud path: BelowVisibilityFloor Result<> error +
// lock the audit to PASS/FAIL cascade (not INDETERMINATE).
//
// Per user spec "Ogni test exit non-zero + identifica comp/layer/node
// + fail-loud":
//   - INFO lines identify comp/layer/node (stub labels).
//   - FAIL_CHECK forces the doctest assertion to fail -> exit 1.
// ============================================================================
#include <doctest/doctest.h>

namespace chaos::sabotage::predicted_bbox_19px {

// Engine-side trigger fingerprint. Returns true if the production
// fail-loud path fires correctly when predicted_bbox drops below
// the FU04 visibility floor (< 20px). Implementation forward-point:
// TICKET-SABOTAGE-PRODUCTION-HOOKS — the production hook is PLANNED
// (no real `trigger_predicted_bbox_19px_failure()` symbol exists
// yet; the production-hook will read the verified-real
// `predicted_bbox` member at include/chronon3d/text/
// text_visibility_audit.hpp per TICKET-TEXT-VISIBILITY-PIPELINE
// FU04 closure lineage).
bool trigger_predicted_bbox_19px_failure() {
    return false;
}

} // namespace chaos::sabotage::predicted_bbox_19px

TEST_CASE(
    "sabotage/predicted_bbox_19px "
    "[comp=TextVisibilityAudit, layer=VisibilityAudit, "
    "node=predicted_bbox_lt_20px_FU04_violation]"
) {
    INFO("Comp=TextVisibilityAudit")
    INFO("Layer=VisibilityAudit [PLANNED TICKET-SABOTAGE-PRODUCTION-HOOKS]")
    INFO("Node=predicted_bbox < 20px FU04 violation "
         "[verified-real at text_visibility_audit.hpp]")
    INFO("Sabotage scenario: predicted_bbox(text_run) < 20px "
         "(FU04 violation) -> BelowVisibilityFloor")
    INFO("Forward-point: TICKET-SABOTAGE-PRODUCTION-HOOKS")
    FAIL_CHECK(
        "sabotage/predicted_bbox_19px: trigger returns false -- "
        "production fail-loud hook not yet wired "
        "(TICKET-SABOTAGE-PRODUCTION-HOOKS forward-point)"
    );
}
