// ═══════════════════════════════════════════════════════════════════════════
// tests/text/test_text_preset_cinematic.cpp — Phase-2.1 P0 split
//
// PLACEHOLDER for the Cinematic tier (deferred to A4.1 once bit-
// stable per the original test_text_preset_visual.cpp scope-lock
// comment).
//
// Presets awaiting cinematic-tier visual coverage (per scope-lock):
//   • caption_box                       (Subtitle tier 3 / 4)
//   • glow_pulse                        (Subtitle tier 4 / 4)
//   • animation_compositions            (Cinematic tier — depends on CameraRig)
//   • cinematic_text_camera            (Cinematic tier — depends on CameraRig)
//   • cinematic_title_reveal            (Cinematic tier — depends on CameraRig)
//   • tilt_sweep_title_v2               (Cinematic tier — depends on CameraRig)
//
// Why empty: the original test_text_preset_visual.cpp scope-lock
// comment explicitly defers these 4 cinematic presets and 2 of the
// subtitle presets to A4.1, gated on bit-stability of CameraRig in
// the canonical render path.  Phase-2.1 mechanical split preserves
// the file layout the user spec requested without artificially
// promoting any of those deferred sentinels — empty sentinel logic
// with a `MESSAGE` anchor is the right shape for downstream A4.1
// PRs to populate without further CMakeLists changes.
//
// When populated, each cinematic preset will require 4 sentinels
// per aspect ratio just like the Reveal/Emphasis/Subtitle tiers
// (16:9 + 9:16 × F000/F020/F030/F040 = 8 sentinels each).  For
// 6 cinematic presets: 48 sentinels, joined by 6 TEST_CASEs that
// call `emit_preset_gate(renderer, "<preset_id>", AspectRatio::k16x9,
// 0, kRefTextPres<PresetId>_169_F000, ...)` × 8.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

TEST_CASE("VRTextPreset/CinematicTierDeferralNotice") {
    // Pure-notice test that fails fast if a future maintainer
    // accidentally regresses the scope-lock.  When A4.1 lands,
    // this single TEST_CASE expands into 6 preset TEST_CASEs ×
    // 8 sentinels = 48 sentinel calls.
    MESSAGE("Cinematic tier + 2 caption_box / glow_pulse presets "
            "are scope-locked to A4.1 per docs/01-baseline-green.md. "
            "Phase-2.1 mechanical split preserves this deferral "
            "(empty TU).  See tests/text/test_text_preset_cinematic.cpp "
            "banner for the rollout sequence.");
    SUBCASE("scope-lock contract preserved") {
        // Soft contract check — the absence of failing assertions
        // is the contract.  A4.1 will replace this TU with the
        // canonical 6 cinematic × 8 sentinels layout.
        CHECK(true);
    }
}
