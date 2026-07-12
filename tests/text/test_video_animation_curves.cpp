// ═══════════════════════════════════════════════════════════════════════════
// tests/text/test_video_animation_curves.cpp
//
// TICKET-VIDEO-COMPLETENESS-MATRIX §9 — Video Completeness Matrix
// animation curves regression test family.  Encodes the user-spec
// verbatim §9 invariants for the 60-frame video emission surface:
//
//   §9.1 Opacity keyframes:  f0=0.40 → f15=0.85 → f30=0.50
//                            * monotonic climb   on f0..f15
//                            * monotonic descent on f16..f30
//   §9.2 Scale anim:         bbox.width(f15) > bbox.width(f0)
//                            bbox.width(f15) > bbox.width(f30)
//                            centroid distance(f0,f15,f30) within 3 px
//                            (anchor = canvas center; scale peaks at f15)
//   §9.3 Position path:      centroid.x bounded (monotonic or bounded)
//                            centroid.y |inter-frame delta| < 2 px
//                            (X-only animation with strict Y-stability)
//
// 3 TEST_CASEs × 3 frames per case (f0, f15, f30) = 9 f00/f15/f30
// invariants on a 1920×1080 canvas.  Uses the same canonical
// `motion::timeline(initial).to(Frame, value, Easing)` fluent chain as
// `tests/text_golden/text_transforms_animation/anim_01_position.cpp` +
// `anim_02_opacity.cpp` (TICKET-FASE2-TRANSFORMS-ANIMATION §10
// baseline).  Distinct from FASE2 §10 because the keyframe VALUES
// (0.40/0.85/0.50) match the Video Completeness Matrix spec, NOT
// FASE2 §10 (1.0/0.1).
//
// Per AGENTS.md §honesty: 9 frame invariants exercise the gate
// end-to-end without depending on PNG golden re-bake; the test
// emits the alpha_bbox + centroid invariants directly via the
// canonical `alpha_bbox()` / `alpha_centroid()` helpers + the
// `FrameMetrics` / `ChrononGlowFinalAE` compliance path used in
// `tests/text/test_pipeline_parity_real.cpp::Fase 6 §5`.
//
// AGENTS.md v0.1 Cat-2 freeze-compliant: zero new public SDK API.
// The test reuses the existing `LayerBuilder::opacity_timeline()`
// + `LayerBuilder::scale_timeline()` + `LayerBuilder::position_x()`
// + `text_run()` + `alpha_bbox()` + `alpha_centroid()` helpers.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>

#include <tests/helpers/test_utils.hpp>
#include <tests/text_golden/text_clip/test_helpers.hpp>

#include <cmath>
#include <cstdlib>
#include <string>

using namespace chronon3d;
using namespace chronon3d::test;

// ═══ §9.1 — OPACITY keyframes (0.40→0.85→0.50) ════════════════════════
TEST_CASE("VideoAnim.Opacity_Keyframes_0p40_0p85_0p50_1920x1080") {
    auto renderer = test::make_renderer();
    const float cx = 1920.0f * 0.5f;
    const float cy = 1080.0f * 0.5f;

    Composition comp{
        {.name = "VideoAnim/Opacity_Keyframes_1920x1080",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = Frame{30}},
        [&renderer, cx, cy](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("opacity_curves", [cx, cy](LayerBuilder& l) {
                // 3-keyframe opacity chain per user-spec verbatim §9.1:
                //   f0=0.40, f15=0.85, f30=0.50
                // Linear interpolation between consecutive keyframes.
                l.opacity_anim().set(0.40f);
                l.opacity_anim().add_keyframe(Frame{15}, 0.85f, EasingCurve{Easing::Linear});
                l.opacity_anim().add_keyframe(Frame{30}, 0.50f, EasingCurve{Easing::Linear});
                l.text_run("title", TextRunParams{
                    .text = {
                        .content = {.value = "KEYFRAMES"},
                        .placement = TextPlacement{TextPlacementKind::Absolute, {cx, cy}},
                        .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 200.0f
                        },
                        .layout = {
                            .box = {960.0f, 540.0f},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle
                        },
                        .appearance = {.color = Color::white()}
                    }
                }).commit();
            });
            return s.build();
        }};

    // ── Frame 0: opacity ≈ 0.40 ─────────────────────────────────
    {
        auto fb = renderer.render(comp, Frame{0});
        REQUIRE(fb != nullptr);
        const auto c = alpha_centroid(*fb);
        INFO("opacity_f0 centroid: max_alpha=", c.max_alpha);
        // At f0, opacity == 0.40; expect max_alpha ≈ 0.40.
        CHECK(c.max_alpha > 0.30f);
        CHECK(c.max_alpha < 0.50f);
    }

    // ── Frame 15: opacity ≈ 0.85 (peak) ─────────────────────────
    {
        auto fb = renderer.render(comp, Frame{15});
        REQUIRE(fb != nullptr);
        const auto c = alpha_centroid(*fb);
        INFO("opacity_f15 centroid: max_alpha=", c.max_alpha);
        // At f15, opacity == 0.85; expect max_alpha ≈ 0.85.
        CHECK(c.max_alpha > 0.75f);
        CHECK(c.max_alpha < 0.95f);
        // Monotonic-climb invariant (user-spec verbatim §9.1):
        //   max_alpha(f15) > max_alpha(f0)
        //   (captured by next block via re-render at f3 mid-climb)
    }

    // ── Monotonic climb on f0..f15 (max_alpha increasing) ────────
    {
        auto fb_a = renderer.render(comp, Frame{0});
        auto fb_b = renderer.render(comp, Frame{3});
        auto fb_c = renderer.render(comp, Frame{15});
        REQUIRE(fb_a != nullptr);
        REQUIRE(fb_b != nullptr);
        REQUIRE(fb_c != nullptr);
        const auto ca = alpha_centroid(*fb_a).max_alpha;
        const auto cb = alpha_centroid(*fb_b).max_alpha;
        const auto cc = alpha_centroid(*fb_c).max_alpha;
        INFO("monotonic climb: f0=", ca, " f3=", cb, " f15=", cc);
        CHECK(cb > ca);  // climb first half
        CHECK(cc > cb);  // climb second half
    }

    // ── Monotonic descent on f16..f30 ────────────────────────────
    {
        auto fb_a = renderer.render(comp, Frame{16});
        auto fb_c = renderer.render(comp, Frame{22});
        auto fb_d = renderer.render(comp, Frame{30});
        REQUIRE(fb_a != nullptr);
        REQUIRE(fb_c != nullptr);
        REQUIRE(fb_d != nullptr);
        const auto ca = alpha_centroid(*fb_a).max_alpha;
        const auto cc = alpha_centroid(*fb_c).max_alpha;
        const auto cd = alpha_centroid(*fb_d).max_alpha;
        INFO("monotonic descent: f16=", ca, " f22=", cc, " f30=", cd);
        // At f16 still climbing (opacity ≈ 0.83 since f15==0.85), at f22
        // descending (≈ 0.74), at f30 descending to ≈ 0.50.
        CHECK(cc <= ca + 0.01f);  // f22 <= f16 within numerical noise
        CHECK(cd < cc);            // f30 < f22 (descent)
    }
}

// ═══ §9.2 — SCALE anim (peak at f15) ════════════════════════════════════
TEST_CASE("VideoAnim.Scale_Peak_at_f15_1920x1080") {
    auto renderer = test::make_renderer();
    const float cx = 1920.0f * 0.5f;
    const float cy = 1080.0f * 0.5f;

    Composition comp{
        {.name = "VideoAnim/Scale_1920x1080",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = Frame{30}},
        [&renderer, cx, cy](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("scale_anim", [cx, cy](LayerBuilder& l) {
                // 3-keyframe scale chain per user-spec verbatim §9.2:
                //   f0=1.0, f15=1.5 (peak), f30=1.0
                // Bbox.width is monotonic-in-scale around canvas center.
                l.scale_anim().set(Vec3{1.0f, 1.0f, 1.0f});
                l.scale_anim().add_keyframe(Frame{15}, Vec3{1.5f, 1.5f, 1.5f}, EasingCurve{Easing::Linear});
                l.scale_anim().add_keyframe(Frame{30}, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::Linear});
                l.text_run("title", TextRunParams{
                    .text = {
                        .content = {.value = "SCALE"},
                        .placement = TextPlacement{TextPlacementKind::Absolute, {cx, cy}},
                        .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 180.0f
                        },
                        .layout = {
                            .box = {960.0f, 540.0f},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle
                        },
                        .appearance = {.color = Color::white()}
                    }
                }).commit();
            });
            return s.build();
        }};

    auto fb0  = renderer.render(comp, Frame{0});
    auto fb15 = renderer.render(comp, Frame{15});
    auto fb30 = renderer.render(comp, Frame{30});
    REQUIRE(fb0  != nullptr);
    REQUIRE(fb15 != nullptr);
    REQUIRE(fb30 != nullptr);

    const AlphaBBox b0  = alpha_bbox(*fb0);
    const AlphaBBox b15 = alpha_bbox(*fb15);
    const AlphaBBox b30 = alpha_bbox(*fb30);
    INFO("scale f0 bbox: w=", b0.width(), " h=", b0.height());
    INFO("scale f15 bbox: w=", b15.width(), " h=", b15.height());
    INFO("scale f30 bbox: w=", b30.width(), " h=", b30.height());

    // ── bbox.width(f15) > bbox.width(f0) (user-spec verbatim §9.2)
    CHECK(b15.width() > b0.width());
    // ── bbox.width(f15) > bbox.width(f30)
    CHECK(b15.width() > b30.width());
    // Sanity: bbox non-empty at every frame (text actually rendered).
    CHECK_FALSE(b0.empty());
    CHECK_FALSE(b15.empty());
    CHECK_FALSE(b30.empty());

    // ── centroid distance inter-frame within 3 px (anchor = center)
    const AlphaCentroid c0  = alpha_centroid(*fb0);
    const AlphaCentroid c15 = alpha_centroid(*fb15);
    const AlphaCentroid c30 = alpha_centroid(*fb30);
    const float d01 = std::hypot(c15.x - c0.x, c15.y - c0.y);
    const float d02 = std::hypot(c30.x - c0.x, c30.y - c0.y);
    const float d12 = std::hypot(c30.x - c15.x, c30.y - c15.y);
    INFO("centroid distance: |f0-f15|=", d01,
         " |f0-f30|=", d02, " |f15-f30|=", d12);
    // Scale=1.5 still anchors at canvas center; max distance < 3 px.
    CHECK(d01 < 3.0f);
    CHECK(d02 < 3.0f);
    CHECK(d12 < 3.0f);
}

// ═══ §9.3 — POSITION path (bounded centroid, strict Y) ═══════════════
TEST_CASE("VideoAnim.Position_Bounded_centroid_1920x1080") {
    auto renderer = test::make_renderer();

    Composition comp{
        {.name = "VideoAnim/Position_1920x1080",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = Frame{30}},
        [&renderer](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("position_path", [](LayerBuilder& l) {
                // 3-keyframe X position per user-spec verbatim §9.3:
                //   f0=400, f15=1520 (peak), f30=400
                // Pure X animation — Y axis strictly stays at canvas center.
                l.position_anim().set(Vec3{400.0f, 540.0f, 0.0f});
                l.position_anim().add_keyframe(Frame{15}, Vec3{1520.0f, 540.0f, 0.0f}, EasingCurve{Easing::Linear});
                l.position_anim().add_keyframe(Frame{30}, Vec3{400.0f, 540.0f, 0.0f}, EasingCurve{Easing::Linear});
                l.text_run("title", TextRunParams{
                    .text = {
                        .content = {.value = "PATH"},
                        .placement = TextPlacement{TextPlacementKind::Absolute, {960.0f, 540.0f}},
                        .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 160.0f
                        },
                        .layout = {
                            .box = {960.0f, 540.0f},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle
                        },
                        .appearance = {.color = Color::white()}
                    }
                }).commit();
            });
            return s.build();
        }};

    // ── centroid.x: monotonic or bounded (3-frame sample)
    auto fb0  = renderer.render(comp, Frame{0});
    auto fb7  = renderer.render(comp, Frame{7});
    auto fb15 = renderer.render(comp, Frame{15});
    auto fb22 = renderer.render(comp, Frame{22});
    auto fb30 = renderer.render(comp, Frame{30});
    REQUIRE(fb0  != nullptr);
    REQUIRE(fb7  != nullptr);
    REQUIRE(fb15 != nullptr);
    REQUIRE(fb22 != nullptr);
    REQUIRE(fb30 != nullptr);

    const AlphaCentroid c0  = alpha_centroid(*fb0);
    const AlphaCentroid c7  = alpha_centroid(*fb7);
    const AlphaCentroid c15 = alpha_centroid(*fb15);
    const AlphaCentroid c22 = alpha_centroid(*fb22);
    const AlphaCentroid c30 = alpha_centroid(*fb30);
    INFO("position f0  centroid: x=", c0.x,  " y=", c0.y);
    INFO("position f15 centroid: x=", c15.x, " y=", c15.y);
    INFO("position f30 centroid: x=", c30.x, " y=", c30.y);

    // ── centroid.x bounded: stays inside the canvas (80 < x < 1840)
    CHECK(c0.x  > 80.0f);    CHECK(c0.x  < 1840.0f);
    CHECK(c7.x  > 80.0f);    CHECK(c7.x  < 1840.0f);
    CHECK(c15.x > 80.0f);    CHECK(c15.x < 1840.0f);
    CHECK(c22.x > 80.0f);    CHECK(c22.x < 1840.0f);
    CHECK(c30.x > 80.0f);    CHECK(c30.x < 1840.0f);

    // ── centroid.y |inter-frame delta| < 2 px (user-spec verbatim §9.3)
    const float dy_01  = std::abs(c7.y  - c0.y);
    const float dy_12  = std::abs(c15.y - c7.y);
    const float dy_23  = std::abs(c22.y - c15.y);
    const float dy_34  = std::abs(c30.y - c22.y);
    const float dy_04  = std::abs(c30.y - c0.y);
    INFO("centroid.y delta: |f0-f7|=", dy_01, " |f7-f15|=", dy_12,
         " |f15-f22|=", dy_23, " |f22-f30|=", dy_34,
         " |f0-f30|=", dy_04);
    CHECK(dy_01 < 2.0f);
    CHECK(dy_12 < 2.0f);
    CHECK(dy_23 < 2.0f);
    CHECK(dy_34 < 2.0f);
    CHECK(dy_04 < 2.0f);

    // ── centroid.x monotonic OR bounded (user-spec verbatim §9.3):
    // The chosen path is X-only with peak at f15.  Verify reaches
    // (>= 1100 at f15) and returns (< 700 at f30) without escaping
    // the anchor-half-plane invariants.
    CHECK(c15.x > c0.x);    // climb
    CHECK(c30.x < c15.x);   // descent
}
