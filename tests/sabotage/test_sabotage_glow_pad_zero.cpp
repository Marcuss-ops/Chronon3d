// ============================================================================
// tests/sabotage/test_sabotage_glow_pad_zero.cpp
// ============================================================================
//
// Sabotage scenario #4: glow_pad property == 0 on a text run that
// requested a non-zero glow (e.g. `AnimTypewriterGlow` with
// glow_intensity > 0 + glow_color set). The FU04 rot-pattern
// `glow_pad_value == 0` causes the glow halo to clip against the
// composition's text bbox, producing a hard-edged glow that breaks
// AA blending.
//
// Engine signature (genuinely-not-yet-exposed surface — NO verified
// real symbol exists): machine-verified grep 2026-07-12 for the
// keywords `glow_pad` + `pad_value` + `GlowPad` returned 0 hits in
// `include/ + src/ + content/ + apps/`. The closest existing header
// is `include/chronon3d/effects/glow_pipeline.hpp` (which exists)
// but its members do NOT currently expose a `glow_pad` or
// `pad_value` field. The Phase 2 fail-loud HOOK signature is
// therefore FULLY PLANNED (TICKET-SABOTAGE-PRODUCTION-HOOKS) —
// the production hook EXPOSES a new `glow_pad_value` member to
// enable the trigger-glow_pad_zero-failure test to fire.
//
// Expected fail-loud path: ZeroGlowPad Result<> error + lock to fail
// the layout so the tile is INDETERMINATE rather than committed as
// AA-broken output.
//
// Per user spec "Ogni test exit non-zero + identifica comp/layer/node
// + fail-loud":
//   - INFO lines identify comp/layer/node (stub labels).
//   - FAIL_CHECK forces the doctest assertion to fail -> exit 1.
// ============================================================================
#include <doctest/doctest.h>

namespace chaos::sabotage::glow_pad_zero {

// Engine-side trigger fingerprint. Implementation forward-point:
// TICKET-SABOTAGE-PRODUCTION-HOOKS — FULLY PLANNED.
// (Machine-verified 2026-07-12: `glow_pad` symbol has 0 grep hits
// in the codebase; the production hook must FIRST expose
// `glow_pad_value` as a new member on the glow pipeline, which
// is a separate TICKET-SABOTAGE-GLOW-PAD-EXPOSURE forward-point.)
bool trigger_glow_pad_zero_failure() {
    return false;
}

} // namespace chaos::sabotage::glow_pad_zero

TEST_CASE(
    "sabotage/glow_pad_zero "
    "[comp=TextComposition, layer=GlowPipeline, "
    "node=glow_pad_value_eq_zero_PLANNED]"
) {
    INFO("Comp=TextComposition")
    INFO("Layer=GlowPipeline [no verified `glow_pad` symbol in codebase]")
    INFO("Node=glow_pad_value==0 BUT glow_intensity>0 [PLANNED "
         "TICKET-SABOTAGE-PRODUCTION-HOOKS + "
         "TICKET-SABOTAGE-GLOW-PAD-EXPOSURE]")
    INFO("Sabotage scenario: glow_pad_value(text_run) == 0 BUT "
         "text_run.glow_intensity > 0 -> ZeroGlowPad")
    INFO("Forward-point: TICKET-SABOTAGE-PRODUCTION-HOOKS")
    FAIL_CHECK(
        "sabotage/glow_pad_zero: trigger returns false -- production "
        "fail-loud hook not yet wired "
        "(TICKET-SABOTAGE-PRODUCTION-HOOKS forward-point)"
    );
}
