#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// tests/visual/ae_parity/ae_parity_compositions.hpp
//
// TICKET-AE-PARITY-FLOOR-DASHBOARD — Composition factories for the 5 new
// cinematic scenes (ae_08_glow_pulse + ae_10_scale_pop +
// ae_12_random_character_jitter + ae_14_multiline_9_16 + motion_blur_text)
// landed in commits 3ddbbdff/45be2b40 as TEST CASES
// (tests/text_golden/ae_parity/ae_0{8,10,12,14}_*.cpp +
//  tests/text_golden/motion_blur_text/motion_blur_text_scene.cpp).
//
// The test files use captured `frame_idx` to set per-frame animation state
// (opacity / scale / position / blur). For the registered composition path,
// the frame index is derived dynamically from `ctx.frame.integral() % 30` inside
// the runtime lambda (CompositionProps has no frame field; the actual frame
// is delivered per-render via FrameContext).  This matches the test files'
// 0/15/30 snapshot buckets so the rendered output is bit-equivalent to the
// corresponding test snapshot.
//
// Each factory takes `const CompositionProps&` and returns a `Composition`
// configured at 16:9 (1920x1080) landscape — same as the canonical
// test build_landscape path.  Use `--frame 0|15|30` on the CLI to pick
// the snapshot bucket.
//
// AGENTS.md v0.1 Cat-2 freeze-compliant: zero new public API in
// include/chronon3d/; the factory functions are TEST infrastructure
// (in tests/visual/ae_parity/, NOT in include/), reused by the CLI for
// composition registration; the new file is compiled into the chronon3d_cli
// target via apps/chronon3d_cli/CMakeLists.txt (same pattern as
// tests/visual/ae_parity/ae_parity_scenes.cpp already used).
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_props.hpp>

namespace chronon3d::test {

Composition make_ae_08_glow_pulse(const CompositionProps& props);
Composition make_ae_10_scale_pop(const CompositionProps& props);
Composition make_ae_12_random_character_jitter(const CompositionProps& props);
Composition make_ae_14_multiline_landscape(const CompositionProps& props);
Composition make_motion_blur_text(const CompositionProps& props);

} // namespace chronon3d::test
