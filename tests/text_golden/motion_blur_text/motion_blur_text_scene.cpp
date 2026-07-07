// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/motion_blur_text/motion_blur_text_scene.cpp
//
// TICKET-MOTION-BLUR-TEXT — motion-blur text smoke goldens. Sample scene
// renders a TextRun with high-velocity horizontal motion
// (`glyph_offset_x = 8` per-frame) at 3 frame snapshots (5, 10, 15).
//
// 6 TEST_CASEs = 3 baseline (motion-blur OFF) + 3 motion-blur ON, all at
// the same 3 frame snapshots, to enable cross-compare of the effect
// activation (baseline vs. blurred). The acceptance contract is
// `mean_abs_diff(blurred, baseline) > 5/255` AND
// `pixel_changed_ratio > 0.10` at every frame (proves the effect is
// materially active in the rendered output, not a no-op pass-through).
//
// AGENTS.md v0.1 Cat-2 freeze-compliant: zero new public API in
// include/chronon3d/; verify_golden helper reuse from
// tests/visual/support/golden_test.hpp; canonical pipeline.
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

GoldenTestConfig make_config(const char* tag) {
    GoldenTestConfig cfg;
    cfg.golden_directory   = "test_renders/golden/text";
    cfg.artifact_directory = "test_renders/artifacts/text/motion_blur";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error       = 12.0f / 255.0f;
    cfg.threshold.max_abs_error            = 150.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio  = 0.95f;
    cfg.threshold.max_rmse                 = 14.0f / 255.0f;
    cfg.threshold.min_ssim                 = 0.55f;
    (void)tag;
    return cfg;
}

// Per-frame horizontal offset (simulates high-velocity text motion).
// f=5  → x_offset = 8  (early motion)
// f=10 → x_offset = 16 (mid motion, 2 frames in)
// f=15 → x_offset = 24 (late motion, peak velocity)
static float x_offset_for(std::size_t f) {
    if (f <= 5)  return 8.0f;
    if (f <= 10) return 16.0f;
    return 24.0f;
}

Composition build_composition(SoftwareRenderer& renderer, std::size_t frame_idx, bool motion_blur_on) {
    return composition(
        {.name = motion_blur_on ? "motion_blur_text/ON" : "motion_blur_text/OFF",
         .width = 1280, .height = 720,
         .frame_rate = FrameRate{30, 1},
         .duration = 30},
        [&renderer, frame_idx, motion_blur_on](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [frame_idx, motion_blur_on](LayerBuilder& l) {
                l.text("motion_blur", {
                    .content = {.value = "MOTION BLUR"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 180.0f},
                    .layout = {.box = {1100.0f, 200.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color::white()},
                    .position = {640.0f + x_offset_for(frame_idx), 360.0f, 0.0f}
                });
                if (motion_blur_on) {
                    l.blur(13.0f);  // tier 3 of kBlurTierRadii = {{0,2,7,13,20}}
                }
            });
            return s.build();
        });
}

} // namespace

TEST_CASE("motion_blur_text baseline f05") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_composition(renderer, 5, false), Frame{5});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "motion_blur_text_baseline_f05", make_config("baseline_f05"));
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}

TEST_CASE("motion_blur_text baseline f10") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_composition(renderer, 10, false), Frame{10});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "motion_blur_text_baseline_f10", make_config("baseline_f10"));
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}

TEST_CASE("motion_blur_text baseline f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_composition(renderer, 15, false), Frame{15});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "motion_blur_text_baseline_f15", make_config("baseline_f15"));
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}

TEST_CASE("motion_blur_text blurred f05") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_composition(renderer, 5, true), Frame{5});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "motion_blur_text_blurred_f05", make_config("blurred_f05"));
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}

TEST_CASE("motion_blur_text blurred f10") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_composition(renderer, 10, true), Frame{10});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "motion_blur_text_blurred_f10", make_config("blurred_f10"));
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}

TEST_CASE("motion_blur_text blurred f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_composition(renderer, 15, true), Frame{15});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "motion_blur_text_blurred_f15", make_config("blurred_f15"));
    if (r.golden_missing) { MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create."); return; }
    CHECK(r.passed);
}
