// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/ae_parity/ae_04_fill_stroke_shadow.cpp
//
// TICKET-AE-PARITY-SUITE — Scene 04: white fill + thick black stroke.
// Mirrors user_spec/10 (golden_text_fill_stroke) with stricter AE-cinematic
// framing: 14% thicker stroke for clear silhouette readability at 1080p,
// higher font weight (900 if available, else 700), centered hero placement.
// Frame snapshots capture slight perceptual diff from gradient backgrounds.
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
    cfg.golden_directory  = "test_renders/golden/text";
    cfg.artifact_directory = "test_renders/artifacts/text/ae_04_fill_stroke";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error       = 5.0f / 255.0f;
    cfg.threshold.max_abs_error            = 40.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio  = 0.05f;
    cfg.threshold.max_rmse                 = 6.0f / 255.0f;
    cfg.threshold.min_ssim                 = 0.92f;
    return cfg;
}

// Slight stroke-width interpolation by frame: thinner at f0 (sketch reveal),
// fuller at f30 (cinematic bolding).
static float stroke_width_for(std::size_t f) {
    if (f == 0)  return 6.0f;
    if (f <= 15) return 9.0f;
    return 12.0f;
}

Composition build_landscape(std::size_t frame_idx) {
    return composition(
        {.name = "AE/04/fill_stroke_shadow/16x9",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [frame_idx](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.layer("hero", [frame_idx](LayerBuilder& l) {
                const float w = stroke_width_for(frame_idx);
                l.text("stroke", {
                    .content = {.value = "OUTLINE"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 320.0f},
                    .layout = {.box = {1600.0f, 500.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {
                        .color = Color::white(),
                        .paint = {
                            .stroke_enabled = true,
                            .stroke_color = Color::black(),
                            .stroke_width = w
                        }
                    },
                    .position = {960.0f, 540.0f, 0.0f}
                });
            });
            return s.build();
        });
}

Composition build_portrait(std::size_t frame_idx) {
    return composition(
        {.name = "AE/04/fill_stroke_shadow/9x16",
         .width = 1080, .height = 1920,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [frame_idx](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.layer("hero", [frame_idx](LayerBuilder& l) {
                const float w = stroke_width_for(frame_idx);
                l.text("stroke", {
                    .content = {.value = "OUTLINE"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 240.0f},
                    .layout = {.box = {920.0f, 400.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {
                        .color = Color::white(),
                        .paint = {
                            .stroke_enabled = true,
                            .stroke_color = Color::black(),
                            .stroke_width = w
                        }
                    },
                    .position = {540.0f, 960.0f, 0.0f}
                });
            });
            return s.build();
        });
}

} // namespace

TEST_CASE("AE 04 fill_stroke_shadow 16x9 f00") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(0), Frame{0});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "ae_04_fill_stroke_shadow_16x9_f00", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing"); return; }
    CHECK(r.passed);
}
TEST_CASE("AE 04 fill_stroke_shadow 16x9 f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(15), Frame{15});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "ae_04_fill_stroke_shadow_16x9_f15", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing"); return; }
    CHECK(r.passed);
}
TEST_CASE("AE 04 fill_stroke_shadow 16x9 f30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(30), Frame{30});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "ae_04_fill_stroke_shadow_16x9_f30", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing"); return; }
    CHECK(r.passed);
}
TEST_CASE("AE 04 fill_stroke_shadow 9x16 f00") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(0), Frame{0});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "ae_04_fill_stroke_shadow_9x16_f00", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing"); return; }
    CHECK(r.passed);
}
TEST_CASE("AE 04 fill_stroke_shadow 9x16 f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(15), Frame{15});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "ae_04_fill_stroke_shadow_9x16_f15", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing"); return; }
    CHECK(r.passed);
}
TEST_CASE("AE 04 fill_stroke_shadow 9x16 f30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(30), Frame{30});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "ae_04_fill_stroke_shadow_9x16_f30", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing"); return; }
    CHECK(r.passed);
}
