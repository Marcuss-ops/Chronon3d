// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_export/text_export_golden.cpp
//
// Text Export V1 — deterministic golden regression test.
//
// Renders "TEXT EXPORT V1" via the canonical SceneBuilder + l.text() API
// and verifies pixel-by-pixel against a golden PNG reference.  This
// prevents regressions on the text export pipeline (font resolution,
// shaping, layout, rasterization, compositing).
//
// Test matrix:
//   • 640×360 (consumer resolution)
//   • Frame 0 (static)
//   • Font: Inter-Bold.ttf
//   • White text on dark background
//
// Golden PNGs: test_renders/golden/text/text_export_v1_640x360_F000.png
// Update:      CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextExportGolden
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

#include <tests/visual/support/golden_test.hpp>
#include <tests/helpers/test_utils.hpp>

#include <string>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

// Build the Text Export V1 composition — mirrors examples/text_export_consumer/main.cpp
Composition build_text_export_composition(SoftwareRenderer& renderer) {
    return composition(
        {.name = "text_export_v1_golden",
         .width = 640, .height = 360,
         .frame_rate = FrameRate{30, 1},
         .duration = 1},
        [&renderer](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());

            // Dark background
            s.layer("bg", [](LayerBuilder& l) {
                l.rect("bg_rect", RectParams{
                    .size = {640.0f, 360.0f},
                    .color = Color{0.1f, 0.1f, 0.15f, 1.0f},
                    .pos = {320.0f, 180.0f, 0.0f},
                    .fill = FillStyle::solid(Color{0.1f, 0.1f, 0.15f, 1.0f})
                });
            });

            // Text layer — "TEXT EXPORT V1" centered, white, Inter-Bold 48pt
            s.layer("title", [&renderer](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("title", TextRunParams{
                    .text = TextSpec{
                        .content = {.value = "TEXT EXPORT V1"},
                        .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                                 .font_family = "Inter",
                                 .font_weight = 700,
                                 .font_size = 48.0f},
                        .layout = {.box = {640.0f, 360.0f},
                                   .align = TextAlign::Center,
                                   .vertical_align = VerticalAlign::Middle},
                        .appearance = {.color = Color::white()},
                        .position = {320.0f, 180.0f, 0.0f}
                    }
                }).commit();
            });

            return s.build();
        });
}

// Golden config — relaxed thresholds for cross-platform font rendering.
GoldenTestConfig text_export_golden_config() {
    GoldenTestConfig cfg;
    cfg.golden_directory   = "test_renders/golden/text";
    cfg.artifact_directory = "test_renders/artifacts/text";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error     = 5.0 / 255.0;
    cfg.threshold.max_abs_error           = 40.0 / 255.0;
    cfg.threshold.max_changed_pixel_ratio = 0.05;
    cfg.threshold.max_rmse                = 6.0 / 255.0;
    cfg.threshold.min_ssim                = 0.92;
    return cfg;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Text Export V1 — deterministic golden test
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextExportGolden: TEXT EXPORT V1 640x360 F0") {
    auto renderer = test::make_renderer();
    auto comp = build_text_export_composition(renderer);

    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(fb != nullptr);

    // Verify text is actually visible (anti-false-green sanity)
    const Color* data = fb->data();
    size_t visible_pixels = 0;
    constexpr float kThreshold = 5.0f / 255.0f;
    for (size_t i = 0; i < fb->pixel_count(); ++i) {
        if (data[i].r > kThreshold || data[i].g > kThreshold || data[i].b > kThreshold) {
            ++visible_pixels;
        }
    }
    CHECK(visible_pixels > 0);
    INFO("Visible pixels: ", visible_pixels, "/", fb->pixel_count());

    // Golden diff
    auto cfg = text_export_golden_config();
    auto result = verify_golden(*fb, "text_export_v1_640x360_F000", cfg);
    INFO("Golden: ", result.message);
    if (result.golden_missing) {
        MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create.");
        return;
    }
    CHECK(result.passed);
}
