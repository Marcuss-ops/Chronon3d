// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/ae_parity/ae_03_word_cascade.cpp
//
// TICKET-AE-PARITY-SUITE — Scene 03: word cascade (3 layers fall from top).
// Three words "FRAMEWORK" / "STUDIO" / "MOTION" drop from top of frame
// at staggered offsets.  Frame snapshots show progressive drop:
// f00 - all near top (~30% y), f15 - mid-cascade (~50% y), f30 - settled
// (~80% y for bottom word, ~60% mid, ~40% top).
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>
#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/sdk/render_output.hpp>
#include <chronon3d/sdk/render_error.hpp>
#include <chronon3d/sdk/render_request.hpp>
#include <chronon3d/sdk/render_settings.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
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
    cfg.artifact_directory = "test_renders/artifacts/text/ae_03_word_cascade";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error       = 6.0f / 255.0f;
    cfg.threshold.max_abs_error            = 60.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio  = 0.20f;
    cfg.threshold.max_rmse                 = 8.0f / 255.0f;
    cfg.threshold.min_ssim                 = 0.85f;
    return cfg;
}

// Word cascade: positions interpolate based on frame.
// Frame 0 → all words near top y (~15% down) for cascade-in-flight.
// Frame 15 → mid y (~50% down) for cascade-passing-through.
// Frame 30 → settled final layout: row of 3 words at 25/50/75%.
struct CascadeY { float top, mid, bot; };
static CascadeY cascade_y(std::size_t f, float canvas_h) {
    if (f == 0)  return {0.15f, 0.20f, 0.25f};  // near top, all squished
    if (f <= 15) return {0.35f, 0.45f, 0.55f};  // mid-cascade
    return {0.25f, 0.50f, 0.75f};               // final settled
}

Composition build_landscape(SoftwareRenderer& renderer, std::size_t frame_idx) {
    return composition(
        {.name = "AE/03/word_cascade/16x9",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, frame_idx](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            const auto cy = cascade_y(frame_idx, 1080.0f);
            s.layer("word_top", [cy](LayerBuilder& l) {
                l.text("word_top", {
                    .content = {.value = "FRAMEWORK"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 96.0f},
                    .layout = {.box = {1200.0f, 120.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color::white()},
                    .position = {960.0f, cy.top * 1080.0f, 0.0f}
                });
            });
            s.layer("word_mid", [cy](LayerBuilder& l) {
                l.text("word_mid", {
                    .content = {.value = "STUDIO"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 96.0f},
                    .layout = {.box = {1200.0f, 120.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color{0.9f, 0.9f, 0.9f, 1.0f}},
                    .position = {960.0f, cy.mid * 1080.0f, 0.0f}
                });
            });
            s.layer("word_bot", [cy](LayerBuilder& l) {
                l.text("word_bot", {
                    .content = {.value = "MOTION"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 96.0f},
                    .layout = {.box = {1200.0f, 120.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color{0.75f, 0.75f, 0.75f, 1.0f}},
                    .position = {960.0f, cy.bot * 1080.0f, 0.0f}
                });
            });
            return s.build();
        });
}

Composition build_portrait(SoftwareRenderer& renderer, std::size_t frame_idx) {
    return composition(
        {.name = "AE/03/word_cascade/9x16",
         .width = 1080, .height = 1920,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, frame_idx](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            const auto cy = cascade_y(frame_idx, 1920.0f);
            s.layer("word_top", [cy](LayerBuilder& l) {
                l.text("word_top", {
                    .content = {.value = "FRAMEWORK"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 80.0f},
                    .layout = {.box = {920.0f, 120.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color::white()},
                    .position = {540.0f, cy.top * 1920.0f, 0.0f}
                });
            });
            s.layer("word_mid", [cy](LayerBuilder& l) {
                l.text("word_mid", {
                    .content = {.value = "STUDIO"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 80.0f},
                    .layout = {.box = {920.0f, 120.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color{0.9f, 0.9f, 0.9f, 1.0f}},
                    .position = {540.0f, cy.mid * 1920.0f, 0.0f}
                });
            });
            s.layer("word_bot", [cy](LayerBuilder& l) {
                l.text("word_bot", {
                    .content = {.value = "MOTION"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 80.0f},
                    .layout = {.box = {920.0f, 120.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color{0.75f, 0.75f, 0.75f, 1.0f}},
                    .position = {540.0f, cy.bot * 1920.0f, 0.0f}
                });
            });
            return s.build();
        });
}

} // namespace

TEST_CASE("AE 03 word_cascade 16x9 f00") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 0), Frame{0});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "ae_03_word_cascade_16x9_f00", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing"); return; }
    CHECK(r.passed);
}
TEST_CASE("AE 03 word_cascade 16x9 f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "ae_03_word_cascade_16x9_f15", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing"); return; }
    CHECK(r.passed);
}
TEST_CASE("AE 03 word_cascade 16x9 f30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 30), Frame{30});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "ae_03_word_cascade_16x9_f30", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing"); return; }
    CHECK(r.passed);
}
TEST_CASE("AE 03 word_cascade 9x16 f00") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 0), Frame{0});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "ae_03_word_cascade_9x16_f00", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing"); return; }
    CHECK(r.passed);
}
TEST_CASE("AE 03 word_cascade 9x16 f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "ae_03_word_cascade_9x16_f15", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing"); return; }
    CHECK(r.passed);
}
TEST_CASE("AE 03 word_cascade 9x16 f30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 30), Frame{30});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "ae_03_word_cascade_9x16_f30", make_config());
    if (r.golden_missing) { MESSAGE("Golden missing"); return; }
    CHECK(r.passed);
}
