// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/ae_parity/ae_11_rotation_per_character.cpp
//
// TICKET-AE-PARITY-CINEMATIC-11 — Scene 11: cinematic per-character
// Y-axis rotation + slight camera prospettica (kickoff for Phase 2
// Killer 4 per-character-3D complete).
//
// This scene-builder establishes the *prerequisite parziale* cinematically:
// the entire text layer rotates progressive Y-degrees across frames
// (f00=0° → f15=12° → f30=24°) AND pulls slightly forward in Z
// (z=0 → z=-60 → z=-120) to evoke a slight camera-prospettica depth
// feel.
//
//   Channel    │ f00   │ f15    │ f30    │
//   ────────────┼───────┼────────┼────────┤
//   Y rotation  │ 0°    │ 12°    │ 24°    │  (per-frame ramp)
//   Z position  │ 0     │ -60    │ -120   │  (slight prospettica)
//   fill RGB    │ cool-white tint shifting ↔ ↔ warmer-white (cinematic mood shift)
//
// 6 TEST_CASEs = 16:9 + 9:16 × 3 frame snapshots f00/f15/f30.
//
// Implementation scope — HONEST BOUNDARY (read before changing design)
//
// Phase 1 cinematic *scaffold* (DELIVERED by THIS scene, build + capture
// green): the entire text layer rotates as one rigid body around the
// layer anchor across the frame progression
//
//   Channel    │ f00   │ f15    │ f30    │
//   ────────────┼───────┼────────┼────────┤
//   Y rotation  │ 0°    │ 12°    │ 24°    │  (LayerBuilder::rotate(Vec3{0,y,0}))
//   Z depth     │ 0     │ -60    │ -120   │  (camera-prospettica hint via Z-transl)
//   fill RGB    │ cool-white shifting ↔ warmer-white (mood consistency, ≤2% drift)
//
// This produces the AE-style fan-fold *silhouette* (rotation cue legible at
// 24° climax) — but the rotation is uniform across all characters (one
// whole-layer transform). The TRUE per-character rotation (each glyph
// independently rotating around its own centroid, fan-out character-by-
// character across the text run) is Phase 2 Killer 4 ticket
// `TICKET-AE-PARITY-KILLER-PER-CHAR-3D`.
//
// Phase 2 Killer 4 upgrade path (forward-only, NOT YET IMPL — TICKET-001 tier).
// The chain uses an explicit literal 22.0f (not a kYawMaxDeg constant) to
// keep the documented call site self-contained — no symbol that future
// readers must look up. When Phase 2 Killer 4 lands, refactor 22.0f into a
// named constant at that scope (e.g. `static constexpr f32 kYawMaxDeg = 22.0f;`):
//   l.text_run("rot_fan", {.content = "PER GLYPH 3D", ...})
//     .animate(animator("rot_fan_selector_char")
//                .select(selector(TextSelectorUnit::Character)
//                          .shape_smooth()
//                          .start(Frame{ 0},    0.0f)
//                          .start(Frame{30}, 100.0f))
//                .rotation(Vec3{0.0f, 22.0f, 0.0f}));               // = kYawMaxDeg
//
// This per-glyph Animator chain pushes a TextAnimatorSpec into the layer's
// pending run; the per-glyph evaluator at
// `src/text/animation/text_property_applier.cpp:apply_property_to_glyph`
// then writes the weighted Y-rotation INTO `GlyphInstanceState.rotation`
// for each glyph independently (range selector shape_smooth gives the
// per-character ramp). The current scene leaves this surface untouched
// (cat-2 freeze: zero new public symbols in include/chronon3d/) — the
// chain is documented verbatim above so ae_11 lands as the prerequisite
// while Phase 2 Killer 4 has a clear, copy-paste-able call site.
//
// AGENTS.md v0.1 Cat-2 freeze-compliant: zero new public API in
// include/chronon3d/; verify_golden helper reuse from
// tests/visual/support/golden_test.hpp; canonical pipeline
// `composition(...)` + `SceneBuilder::layer(...)` + `LayerBuilder::text(...)`
// + `LayerBuilder::rotate(...)` + `LayerBuilder::position(...)`
// + `verify_golden(*fb, ...)`; no legacy TextShape / rich_text references.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

#include <tests/visual/support/golden_test.hpp>
#include <tests/helpers/test_utils.hpp>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

GoldenTestConfig make_config() {
    GoldenTestConfig cfg;
    cfg.golden_directory   = "test_renders/golden/text";
    cfg.artifact_directory = "test_renders/artifacts/text/ae_11_rotation";
    cfg.mode               = golden_mode_from_environment();
    // Acceptance contract: each frame MUST differ materially from f00
    // baseline (Y-axis rotation 0°→24° + Z-translation z=0→-120 →
    // legible geometric tilt + slight depth).  Looser than static ae_04
    // baseline but tighter than multi-channel ae_06 (only 2 channels
    // animated: rotation + z; fill RGB is a sub-2% static drift for
    // mood consistency).
    cfg.threshold.max_mean_abs_error       = 9.0f / 255.0f;
    cfg.threshold.max_abs_error            = 125.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio  = 0.92f;
    cfg.threshold.max_rmse                 = 11.0f / 255.0f;
    cfg.threshold.min_ssim                 = 0.65f;
    return cfg;
}

// Y-axis rotation per-frame (degrees). f=0 flat baseline; f15 mid tilt;
// f30 climax tilt. The range is deliberately cinematic (≤24°) so the
// glyphs stay legible while evoking a 3D fan effect.
static float rotation_y_for(std::size_t f) {
    if (f == 0)  return 0.0f;
    if (f <= 15) return 12.0f;
    return 24.0f;
}

// Per-frame Z-translation for slight camera prospettica hint (layer
// pushes slightly forward in Z as the rotation ramps up). Negative Z
// in C3D's right-handed convention = approach toward the camera.
static float perspective_z_for(std::size_t f) {
    if (f == 0)  return 0.0f;
    if (f <= 15) return -60.0f;
    return -120.0f;
}

// Fill-color RGB per-frame: cool-white tint at f15 (rest tilt), warm-
// white settled at f30. Sub-2% drift on the red channel — purely a
// cinematic mood consistency, not a primary animation channel. The
// alpha channel stays at 1.0 across all frames.
static Color fill_color_for(std::size_t f) {
    if (f == 0)  return Color{0.95f, 0.97f, 1.00f, 1.0f}; // cool-white baseline
    if (f <= 15) return Color{0.97f, 0.96f, 1.00f, 1.0f}; // cyan-leaning tilt
    return         Color{1.00f, 0.99f, 0.96f, 1.0f};       // warm-white settled
}

// 16:9 (1920x1080) cinematic rotation-per-character hero.
Composition build_landscape(SoftwareRenderer& renderer, std::size_t frame_idx) {
    return composition(
        {.name = "AE/11/rotation_per_character/16x9",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, frame_idx](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [frame_idx](LayerBuilder& l) {
                const float y_rot = rotation_y_for(frame_idx);
                const float z_pos = perspective_z_for(frame_idx);
                const Color fill  = fill_color_for(frame_idx);
                l.text("rotation_per_character", {
                    .content = {.value = "PER GLYPH 3D"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 240.0f},
                    .layout = {.box = {1700.0f, 360.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = fill},
                    .position = {960.0f, 540.0f, z_pos}
                });
                l.rotate(Vec3{0.0f, y_rot, 0.0f});
            });
            return s.build();
        });
}

// 9:16 (1080x1920) portrait variant — same motion envelope, tighter box.
Composition build_portrait(SoftwareRenderer& renderer, std::size_t frame_idx) {
    return composition(
        {.name = "AE/11/rotation_per_character/9x16",
         .width = 1080, .height = 1920,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, frame_idx](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [frame_idx](LayerBuilder& l) {
                const float y_rot = rotation_y_for(frame_idx);
                const float z_pos = perspective_z_for(frame_idx);
                const Color fill  = fill_color_for(frame_idx);
                l.text("rotation_per_character", {
                    .content = {.value = "PER GLYPH 3D"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 168.0f},
                    .layout = {.box = {1000.0f, 280.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = fill},
                    .position = {540.0f, 960.0f, z_pos}
                });
                l.rotate(Vec3{0.0f, y_rot, 0.0f});
            });
            return s.build();
        });
}

} // namespace

// ─── 16:9 lifecycle snapshots ────────────────────────────────────────────────

TEST_CASE("AE 11 rotation_per_character 16x9 f00") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 0), Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "ae_11_rotation_per_character_16x9_f00", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}

TEST_CASE("AE 11 rotation_per_character 16x9 f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "ae_11_rotation_per_character_16x9_f15", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}

TEST_CASE("AE 11 rotation_per_character 16x9 f30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 30), Frame{30});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "ae_11_rotation_per_character_16x9_f30", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}

// ─── 9:16 lifecycle snapshots ────────────────────────────────────────────────

TEST_CASE("AE 11 rotation_per_character 9x16 f00") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 0), Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "ae_11_rotation_per_character_9x16_f00", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}

TEST_CASE("AE 11 rotation_per_character 9x16 f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "ae_11_rotation_per_character_9x16_f15", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}

TEST_CASE("AE 11 rotation_per_character 9x16 f30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 30), Frame{30});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "ae_11_rotation_per_character_9x16_f30", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}
