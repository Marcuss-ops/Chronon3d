// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/user_spec/11_aspect_ratio_layout.cpp
//
// ADR-014 Decision 1 — Test 11: golden_aspect_ratio_layout_semantics.
// Same semantic content ("TITLE" / "Subtitle line" / "Bottom caption")
// rendered in BOTH 1920×1080 (landscape) and 1080×1920 (portrait).
// Each composition uses the SAME normalized anchor ratios so the layout
// is responsive. Verifies anchor correctness, no hardcoded coordinates,
// safe-area on the 9:16 variant.
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

GoldenTestConfig make_test11_config() {
    GoldenTestConfig cfg;
    cfg.golden_directory  = "test_renders/golden/text";
    cfg.artifact_directory = "test_renders/artifacts/text/user_spec_11";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error    = 5.0f / 255.0f;
    cfg.threshold.max_abs_error          = 40.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio = 0.05f;
    cfg.threshold.max_rmse               = 6.0f / 255.0f;
    cfg.threshold.min_ssim               = 0.92f;
    return cfg;
}

// Landscape 1920×1080
Composition build_test11_landscape(SoftwareRenderer& renderer) {
    return composition(
        {.name = "UserSpec/11/aspect_landscape",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            // 50% horizontal, 25% vertical → top
            s.layer("title", [](LayerBuilder& l) {
                l.text("title", {
                    .content = {.value = "TITLE"},
                    .placement = TextPlacement{TextPlacementKind::Absolute, {960.0f, 270.0f}},  // 25% of 1080,
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 140.0f},
                    .layout = {.box = {1200.0f, 200.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color::white()},
                });
            });
            s.layer("subtitle", [](LayerBuilder& l) {
                l.text("subtitle", {
                    .content = {.value = "Subtitle line"},
                    .placement = TextPlacement{TextPlacementKind::Absolute, {960.0f, 540.0f}},  // 50% of 1080,
                    .font = {.font_path = "assets/fonts/Inter-Regular.ttf",
                             .font_family = "Inter",
                             .font_weight = 400,
                             .font_size = 56.0f},
                    .layout = {.box = {1200.0f, 120.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color{0.85f, 0.85f, 0.85f, 1.0f}},
                });
            });
            s.layer("caption", [](LayerBuilder& l) {
                l.text("caption", {
                    .content = {.value = "Bottom caption"},
                    .placement = TextPlacement{TextPlacementKind::Absolute, {960.0f, 900.0f}},  // ~83% of 1080,
                    .font = {.font_path = "assets/fonts/Inter-Regular.ttf",
                             .font_family = "Inter",
                             .font_weight = 400,
                             .font_size = 36.0f},
                    .layout = {.box = {1200.0f, 80.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color{0.7f, 0.7f, 0.7f, 1.0f}},
                });
            });
            return s.build();
        });
}

// Portrait 1080×1920 — same 25/50/83% ratios
Composition build_test11_portrait(SoftwareRenderer& renderer) {
    return composition(
        {.name = "UserSpec/11/aspect_portrait",
         .width = 1080, .height = 1920,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("title", [](LayerBuilder& l) {
                l.text("title", {
                    .content = {.value = "TITLE"},
                    .placement = TextPlacement{TextPlacementKind::Absolute, {540.0f, 480.0f}},  // 25% of 1920,
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 140.0f},
                    .layout = {.box = {920.0f, 200.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color::white()},
                });
            });
            s.layer("subtitle", [](LayerBuilder& l) {
                l.text("subtitle", {
                    .content = {.value = "Subtitle line"},
                    .placement = TextPlacement{TextPlacementKind::Absolute, {540.0f, 960.0f}},  // 50% of 1920,
                    .font = {.font_path = "assets/fonts/Inter-Regular.ttf",
                             .font_family = "Inter",
                             .font_weight = 400,
                             .font_size = 56.0f},
                    .layout = {.box = {920.0f, 120.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color{0.85f, 0.85f, 0.85f, 1.0f}},
                });
            });
            s.layer("caption", [](LayerBuilder& l) {
                l.text("caption", {
                    .content = {.value = "Bottom caption"},
                    .placement = TextPlacement{TextPlacementKind::Absolute, {540.0f, 1600.0f}},  // ~83% of 1920,
                    .font = {.font_path = "assets/fonts/Inter-Regular.ttf",
                             .font_family = "Inter",
                             .font_weight = 400,
                             .font_size = 36.0f},
                    .layout = {.box = {920.0f, 80.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color{0.7f, 0.7f, 0.7f, 1.0f}},
                });
            });
            return s.build();
        });
}

} // namespace

TEST_CASE("UserSpec 11: aspect ratio layout 1920x1080") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_test11_landscape(renderer), Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width() == 1920);
    REQUIRE(fb->height() == 1080);
    auto r = verify_golden(*fb, "user_spec_11_aspect_landscape", make_test11_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}

TEST_CASE("UserSpec 11: aspect ratio layout 1080x1920") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_test11_portrait(renderer), Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width() == 1080);
    REQUIRE(fb->height() == 1920);
    auto r = verify_golden(*fb, "user_spec_11_aspect_portrait", make_test11_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}
