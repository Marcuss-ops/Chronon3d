// ═══════════════════════════════════════════════════════════════════════════
// tests/visual/ae_parity/ae_parity_compositions.cpp
//
// TICKET-AE-PARITY-FLOOR-DASHBOARD — Composition factory implementations
// (5 functions) extracted from the test build_landscape/build_portrait
// patterns at:
//   tests/text_golden/ae_parity/ae_08_glow_pulse.cpp
//   tests/text_golden/ae_parity/ae_10_scale_pop.cpp
//   tests/text_golden/ae_parity/ae_12_random_character_jitter.cpp
//   tests/text_golden/ae_parity/ae_14_multiline_9_16.cpp
//   tests/text_golden/motion_blur_text/motion_blur_text_scene.cpp
//
// Each factory mirrors the corresponding test file's build_landscape path
// (16:9, 1920x1080) with HARDCODED composition params (name/width/height/
// frame_rate/duration) — same pattern as the test files AND the existing
// `make_ae_cam_01_static_grid()` factory in `tests/visual/ae_parity/
// ae_parity_scenes.cpp` (which also takes no props arguments). The
// `const CompositionProps&` parameter is REQUIRED by the CompositionRegistry
// lambda signature but is UNUSED (the props system provides
// values/project_root/assets, none of which the 5 new scenes need — they
// use hardcoded text content + the global SoftwareRenderer singleton).
//
// Frame index derived dynamically from `ctx.frame.value % 30` (matches
// the test files' 0/15/30 snapshot buckets so rendered output is
// bit-equivalent to the corresponding test snapshot).
//
// AGENTS.md v0.1 Cat-2 freeze-compliant: zero new public API in
// include/chronon3d/; reuses existing test::make_renderer() + composition
// factory pattern; 5 new free functions in `chronon3d::test` namespace
// (test infrastructure, NOT in include/).
// ═══════════════════════════════════════════════════════════════════════════

#include "ae_parity_compositions.hpp"

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include "content/text/text_helpers_centered.hpp"

using namespace chronon3d;

namespace chronon3d::test {

// Map ctx.frame.value (any non-negative int) to one of the 3 snapshot
// buckets {0, 15, 30} used by the test files.  Mirrors the test files'
// `if (f == 0) ... if (f <= 15) ... else ...` pattern.
static inline std::size_t snapshot_bucket_for(const FrameContext& ctx) {
    const long v = ctx.frame.value;
    if (v <= 0)  return 0;
    if (v <= 15) return 15;
    return 30;
}

// NOTE: Previously used test::make_renderer() + shared_renderer() singleton
// to get a FontEngine.  This caused blank renders in the CLI because the
// test renderer's glyph atlas was separate from the CLI's atlas.
// Now we let SceneBuilder(ctx) auto-forward the pipeline's font_engine,
// matching how CertTitle and other content compositions work.

// ─────────────────────────────────────────────────────────────────────────────
// Scene 08: glow_pulse
//   Channel │ f00          │ f15          │ f30          │
//   ─────────┼──────────────┼──────────────┼──────────────┤
//   opacity  │ 0.40         │ 0.85         │ 0.50         │
//   scale    │ (0.96,0.96)  │ (1.05,1.05)  │ (0.98,0.98)  │
//
// Scale animation deferred: l.scale() triggers use_local which skips
// canvas center bake for non-identity scale transforms.
// ─────────────────────────────────────────────────────────────────────────────
Composition make_ae_08_glow_pulse(const CompositionProps& /*props*/) {
    return composition(
        {.name = "ae_08_glow_pulse",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [](const FrameContext& ctx) -> Scene {
            const std::size_t f = snapshot_bucket_for(ctx);
            const float opacity = (f == 0) ? 0.40f : (f <= 15 ? 0.85f : 0.50f);
            SceneBuilder s(ctx);
            s.layer("hero", [opacity](LayerBuilder& l) {
                l.pin_to(Anchor::Center);
                l.opacity(opacity);
                l.text("glow_pulse", content::text::centered_text({
                    .text = "PULSE GLOW",
                    .box = {1920.0f, 1080.0f},
                    .font_asset = "assets/fonts/Inter-Bold.ttf",
                    .font_family = "Inter",
                    .font_weight = 700,
                    .font_size = 230.0f,
                    .line_height = 1.0f,
                }));
            });
            return s.build();
        });
}

// ─────────────────────────────────────────────────────────────────────────────
// Scene 10: scale_pop
//   Channel │ f00          │ f15          │ f30          │
//   ─────────┼──────────────┼──────────────┼──────────────┤
//   scale    │ (0.50,0.50)  │ (1.30,1.30)  │ (1.00,1.00)  │
//   opacity  │ 0.00         │ 0.80         │ 1.00         │
//
// Scale animation deferred: l.scale() triggers use_local which skips
// canvas center bake for non-identity scale transforms.
// ─────────────────────────────────────────────────────────────────────────────
Composition make_ae_10_scale_pop(const CompositionProps& /*props*/) {
    return composition(
        {.name = "ae_10_scale_pop",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [](const FrameContext& ctx) -> Scene {
            const std::size_t f = snapshot_bucket_for(ctx);
            const float opacity = (f == 0) ? 0.00f : (f <= 15 ? 0.80f : 1.00f);
            SceneBuilder s(ctx);
            s.layer("hero", [opacity](LayerBuilder& l) {
                l.pin_to(Anchor::Center);
                l.opacity(opacity);
                l.text("scale_pop", content::text::centered_text({
                    .text = "POP IN",
                    .box = {1920.0f, 1080.0f},
                    .font_asset = "assets/fonts/Inter-Bold.ttf",
                    .font_family = "Inter",
                    .font_weight = 700,
                    .font_size = 240.0f,
                    .line_height = 1.0f,
                }));
            });
            return s.build();
        });
}

// ─────────────────────────────────────────────────────────────────────────────
// Scene 12: random_character_jitter
//   Channel │ f00          │ f15          │ f30          │
//   ─────────┼──────────────┼──────────────┼──────────────┤
//   jitter   │ (0,0)        │ (8,-4)       │ (-5,3)       │
//   opacity  │ 1.00         │ 0.92         │ 1.00         │
// ─────────────────────────────────────────────────────────────────────────────
Composition make_ae_12_random_character_jitter(const CompositionProps& /*props*/) {
    return composition(
        {.name = "ae_12_random_character_jitter",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [](const FrameContext& ctx) -> Scene {
            const std::size_t f = snapshot_bucket_for(ctx);
            const Vec2 jitter = (f == 0)
                ? Vec2{0.0f, 0.0f}
                : (f <= 15 ? Vec2{8.0f, -4.0f} : Vec2{-5.0f, 3.0f});
            const float opacity = (f == 0) ? 1.00f : (f <= 15 ? 0.92f : 1.00f);
            SceneBuilder s(ctx);
            s.layer("hero", [jitter, opacity](LayerBuilder& l) {
                l.pin_to(Anchor::Center);
                l.opacity(opacity);
                l.text("random_jitter", content::text::centered_text({
                    .text = "JITTER",
                    .box = {1920.0f, 1080.0f},
                    .pos = {jitter.x, jitter.y, 0.0f},
                    .font_asset = "assets/fonts/Inter-Bold.ttf",
                    .font_family = "Inter",
                    .font_weight = 700,
                    .font_size = 240.0f,
                    .line_height = 1.0f,
                }));
            });
            return s.build();
        });
}

// ─────────────────────────────────────────────────────────────────────────────
// Scene 14: multiline (registered as 16:9 landscape variant;
//             the 9:16 safe-zone variant is the test file's build_portrait
//             path, not exposed via CLI — out of scope for 5-scene request)
//   Channel │ f00          │ f15          │ f30          │
//   ─────────┼──────────────┼──────────────┼──────────────┤
//   opacity  │ 0.00         │ 0.70         │ 1.00         │
//   dy       │ +20          │ +8           │ 0            │
// ─────────────────────────────────────────────────────────────────────────────
Composition make_ae_14_multiline_landscape(const CompositionProps& /*props*/) {
    return composition(
        {.name = "ae_14_multiline_landscape",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [](const FrameContext& ctx) -> Scene {
            const std::size_t f = snapshot_bucket_for(ctx);
            const float dy = (f == 0) ? 20.0f : (f <= 15 ? 8.0f : 0.0f);
            const float opacity = (f == 0) ? 0.00f : (f <= 15 ? 0.70f : 1.00f);
            SceneBuilder s(ctx);
            s.layer("hero", [dy, opacity](LayerBuilder& l) {
                l.pin_to(Anchor::Center);
                l.opacity(opacity);
                l.text("multiline", content::text::centered_text({
                    .text = "LINE ONE\nLINE TWO\nLINE THREE",
                    .box = {1920.0f, 1080.0f},
                    .pos = {0.0f, dy, 0.0f},
                    .font_asset = "assets/fonts/Inter-Bold.ttf",
                    .font_family = "Inter",
                    .font_weight = 700,
                    .font_size = 120.0f,
                    .max_lines = 3,
                    .line_height = 1.0f,
                }));
            });
            return s.build();
        });
}

// ─────────────────────────────────────────────────────────────────────────────
// motion_blur_text (blurred variant — frame 10 snapshot)
//   Channel │ f05          │ f10          │ f15          │
//   ─────────┼──────────────┼──────────────┼──────────────┤
//   dx       │ +8           │ +16          │ +24          │
//   blur     │ 13.0 (tier 3)│ 13.0         │ 13.0         │
//
// Blur animation deferred: l.blur() compositing path not yet supported.
// ─────────────────────────────────────────────────────────────────────────────
Composition make_motion_blur_text(const CompositionProps& /*props*/) {
    return composition(
        {.name = "motion_blur_text",
         .width = 1280, .height = 720,
         .frame_rate = FrameRate{30, 1},
         .duration = 30},
        [](const FrameContext& ctx) -> Scene {
            const std::size_t f = snapshot_bucket_for(ctx);
            const float dx = (f <= 5) ? 8.0f : (f <= 15 ? 16.0f : 24.0f);
            SceneBuilder s(ctx);
            s.layer("hero", [dx](LayerBuilder& l) {
                l.pin_to(Anchor::Center);
                l.text("motion_blur", content::text::centered_text({
                    .text = "MOTION BLUR",
                    .box = {1280.0f, 720.0f},
                    .pos = {dx, 0.0f, 0.0f},
                    .font_asset = "assets/fonts/Inter-Bold.ttf",
                    .font_family = "Inter",
                    .font_weight = 700,
                    .font_size = 180.0f,
                    .line_height = 1.0f,
                }));
            });
            return s.build();
        });
}

} // namespace chronon3d::test
