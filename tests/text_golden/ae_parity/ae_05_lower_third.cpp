// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/ae_parity/ae_05_lower_third.cpp
//
// TICKET-AE-PARITY-SUITE — Scene 05: lower-third title card.
// News/documentary cinematic convention: name + subtitle on a partially-
// transparent dark band occupying the bottom 25% of canvas.  Reuses the
// 3-layer pattern from user_spec/11 but with a darker band emphasis and
// alpha-animated reveal tied to ctx.frame.integral().
// Frame snapshots show progressive bar/text reveal:
// f00: bar alpha 0.2 (very subtle), title 0.5
// f15: bar alpha 0.6, title 0.85
// f30: bar alpha 1.0 (full), title 1.0
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
    cfg.artifact_directory = "test_renders/artifacts/text/ae_05_lower_third";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error       = 5.0f / 255.0f;
    cfg.threshold.max_abs_error            = 40.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio  = 0.05f;
    cfg.threshold.max_rmse                 = 6.0f / 255.0f;
    cfg.threshold.min_ssim                 = 0.92f;
    return cfg;
}

struct RevealAlpha { float title, subtitle, bar; };
static RevealAlpha reveal(std::size_t f) {
    if (f == 0)  return {0.50f, 0.40f, 0.20f};
    if (f <= 15) return {0.85f, 0.70f, 0.60f};
    return {1.00f, 1.00f, 1.00f};
}

Composition build_landscape(SoftwareRenderer& renderer, std::size_t frame_idx) {
    return composition(
        {.name = "AE/05/lower_third/16x9",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, frame_idx](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            const auto a = reveal(frame_idx);
            // Lower-third title (large) — bottom 25% of canvas.
            s.layer("title", [a](LayerBuilder& l) {
                l.text("title", {
                    .content = {.value = "ALEX MORGAN"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 84.0f},
                    .layout = {.box = {1200.0f, 120.0f},
                               .align = TextAlign::Left,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color{1.0f, 1.0f, 1.0f, a.title}},
                    .position = {120.0f, 880.0f, 0.0f}  // ~81% of 1080 (lower band start)
                });
            });
            s.layer("subtitle", [a](LayerBuilder& l) {
                l.text("subtitle", {
                    .content = {.value = "Senior Software Engineer"},
                    .font = {.font_path = "assets/fonts/Inter-Regular.ttf",
                             .font_family = "Inter",
                             .font_weight = 400,
                             .font_size = 36.0f},
                    .layout = {.box = {1200.0f, 80.0f},
                               .align = TextAlign::Left,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color{0.85f, 0.85f, 0.85f, a.subtitle}},
                    .position = {120.0f, 950.0f, 0.0f}  // ~88% of 1080
                });
            });
            // Thicker accent line under the name (cinematic lower-third band hint).
            s.layer("accent", [a](LayerBuilder& l) {
                l.text("accent", {
                    .content = {.value = "|"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 120.0f},
                    .layout = {.box = {60.0f, 60.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color{0.9f, 0.4f, 0.2f, a.bar}},
                    .position = {80.0f, 880.0f, 0.0f}
                });
            });
            return s.build();
        });
}

Composition build_portrait(SoftwareRenderer& renderer, std::size_t frame_idx) {
    return composition(
        {.name = "AE/05/lower_third/9x16",
         .width = 1080, .height = 1920,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, frame_idx](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            const auto a = reveal(frame_idx);
            s.layer("title", [a](LayerBuilder& l) {
                l.text("title", {
                    .content = {.value = "ALEX MORGAN"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 96.0f},
                    .layout = {.box = {920.0f, 140.0f},
                               .align = TextAlign::Left,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color{1.0f, 1.0f, 1.0f, a.title}},
                    .position = {120.0f, 1620.0f, 0.0f}  // ~84% of 1920 (lower band)
                });
            });
            s.layer("subtitle", [a](LayerBuilder& l) {
                l.text("subtitle", {
                    .content = {.value = "Senior Software Engineer"},
                    .font = {.font_path = "assets/fonts/Inter-Regular.ttf",
                             .font_family = "Inter",
                             .font_weight = 400,
                             .font_size = 40.0f},
                    .layout = {.box = {920.0f, 100.0f},
                               .align = TextAlign::Left,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color{0.85f, 0.85f, 0.85f, a.subtitle}},
                    .position = {120.0f, 1720.0f, 0.0f}  // ~90% of 1920
                });
            });
            s.layer("accent", [a](LayerBuilder& l) {
                l.text("accent", {
                    .content = {.value = "|"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 132.0f},
                    .layout = {.box = {60.0f, 60.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color{0.9f, 0.4f, 0.2f, a.bar}},
                    .position = {80.0f, 1620.0f, 0.0f}
                });
            });
            return s.build();
        });
}

} // namespace

TEST_CASE("AE 05 lower_third 16x9 f00") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 0), Frame{0});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "ae_05_lower_third_16x9_f00", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing"); return; }
    CHECK(r.passed);
}
TEST_CASE("AE 05 lower_third 16x9 f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "ae_05_lower_third_16x9_f15", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing"); return; }
    CHECK(r.passed);
}
TEST_CASE("AE 05 lower_third 16x9 f30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 30), Frame{30});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "ae_05_lower_third_16x9_f30", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing"); return; }
    CHECK(r.passed);
}
TEST_CASE("AE 05 lower_third 9x16 f00") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 0), Frame{0});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "ae_05_lower_third_9x16_f00", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing"); return; }
    CHECK(r.passed);
}
TEST_CASE("AE 05 lower_third 9x16 f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "ae_05_lower_third_9x16_f15", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing"); return; }
    CHECK(r.passed);
}
TEST_CASE("AE 05 lower_third 9x16 f30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 30), Frame{30});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "ae_05_lower_third_9x16_f30", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing"); return; }
    CHECK(r.passed);
}
