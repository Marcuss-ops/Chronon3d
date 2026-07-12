// ═══════════════════════════════════════════════════════════════════════
// tests/text_golden/text_completeness/text_golden_gaps.cpp
//
// Golden coverage gaps — RTL Hebrew, text gradient fill, camera DOF.
//
// These fill the 3 gaps identified in the golden test audit:
//   1. Hebrew RTL text      → golden regression lock
//   2. Text gradient fill   → TextMaterial gradient golden
//   3. Text + camera DOF    → depth-of-field blur on text layers
// ═══════════════════════════════════════════════════════════════════════

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
#include <chronon3d/text/text_material.hpp>
#include <chronon3d/scene/model/camera/dof.hpp>

#include <tests/visual/support/golden_test.hpp>
#include <tests/helpers/test_utils.hpp>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

GoldenTestConfig make_golden_config(const char* artifact_subdir) {
    GoldenTestConfig cfg;
    cfg.golden_directory   = "test_renders/golden/text";
    cfg.artifact_directory = std::string("test_renders/artifacts/text/") + artifact_subdir;
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error     = 8.0f / 255.0f;
    cfg.threshold.max_abs_error          = 60.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio = 0.10f;
    cfg.threshold.max_rmse               = 10.0f / 255.0f;
    cfg.threshold.min_ssim               = 0.90f;
    return cfg;
}

// ── 1. Hebrew RTL golden ───────────────────────────────────────────

Composition build_hebrew_rtl(SoftwareRenderer& renderer) {
    return composition(
        {.name = "GoldenGap/HebrewRTL",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hebrew", [](LayerBuilder& l) {
                // "שלום עולם" — Hebrew for "Hello World"
                l.text("t", {
                    .content = {.value = "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d \xd7\xa2\xd7\x95\xd7\x9c\xd7\x9d"},
                    .placement = TextPlacement{TextPlacementKind::Absolute, {960.0f, 540.0f}},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 96.0f},
                    .layout = {.box = {1920.0f, 1080.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color::white()}
                });
            });
            return s.build();
        });
}

TEST_CASE("GoldenGap 01: Hebrew RTL שלום עולם") {
    auto renderer = test::make_renderer();
    auto comp = build_hebrew_rtl(renderer);
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(fb != nullptr);

    auto result = verify_golden(*fb, "golden_gap_01_hebrew_rtl",
                                make_golden_config("golden_gap_01"));
    INFO("Golden: ", result.message);
    REQUIRE_FALSE(result.golden_missing);
    CHECK(result.passed);
}

// ── 2. Text gradient fill golden ───────────────────────────────────

Composition build_text_gradient(SoftwareRenderer& renderer) {
    TextMaterial mat;
    mat.enabled      = true;
    mat.top_color    = {1.0f, 0.9f, 0.2f, 1.0f};  // gold
    mat.bottom_color = {1.0f, 0.3f, 0.1f, 1.0f};  // red-orange
    mat.emissive     = 1.1f;

    return composition(
        {.name = "GoldenGap/TextGradient",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, mat](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("gradient", [mat](LayerBuilder& l) {
                l.text("t", {
                    .content = {.value = "GRADIENT"},
                    .placement = TextPlacement{TextPlacementKind::Absolute, {960.0f, 540.0f}},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 144.0f},
                    .layout = {.box = {1920.0f, 1080.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color::white(),
                                   .material = mat},
                });
            });
            return s.build();
        });
}

TEST_CASE("GoldenGap 02: text gradient fill gold→red") {
    auto renderer = test::make_renderer();
    auto comp = build_text_gradient(renderer);
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(fb != nullptr);

    auto result = verify_golden(*fb, "golden_gap_02_text_gradient",
                                make_golden_config("golden_gap_02"));
    INFO("Golden: ", result.message);
    REQUIRE_FALSE(result.golden_missing);
    CHECK(result.passed);
}

// ── 3. Text + camera DOF golden ────────────────────────────────────

Composition build_text_camera_dof(SoftwareRenderer& renderer, bool dof_on) {
    return composition(
        {.name = "GoldenGap/TextCameraDOF",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, dof_on](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());

            // Camera with DOF: focus on the text layer, aperture for blur.
            s.camera().dof(DepthOfFieldSettings{
                .enabled    = dof_on,
                .focus_z    = 0.0f,
                .aperture   = 0.04f,
                .max_blur   = 12.0f
            });

            // Layer 1: focused text at z=0
            s.layer("focused", [](LayerBuilder& l) {
                l.text("t0", {
                    .content = {.value = "FOCUS"},
                    .placement = TextPlacement{TextPlacementKind::Absolute, {960.0f, 400.0f}},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 80.0f},
                    .layout = {.box = {1920.0f, 1080.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color::white()},
                });
            });

            // Layer 2: out-of-focus text at z=-300 (farther from camera)
            s.layer("blurred", [](LayerBuilder& l) {
                l.text("t1", {
                    .content = {.value = "BLUR"},
                    .placement = TextPlacement{TextPlacementKind::Absolute, {960.0f, 680.0f, -300.0f}},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 80.0f},
                    .layout = {.box = {1920.0f, 1080.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = {0.7f, 0.7f, 1.0f, 1.0f}},
                });
            });

            return s.build();
        });
}

TEST_CASE("GoldenGap 03: text camera DOF off") {
    auto renderer = test::make_renderer();
    auto comp = build_text_camera_dof(renderer, false);
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(fb != nullptr);

    auto result = verify_golden(*fb, "golden_gap_03a_text_dof_off",
                                make_golden_config("golden_gap_03"));
    INFO("Golden: ", result.message);
    REQUIRE_FALSE(result.golden_missing);
    CHECK(result.passed);
}

TEST_CASE("GoldenGap 04: text camera DOF on") {
    auto renderer = test::make_renderer();
    auto comp = build_text_camera_dof(renderer, true);
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(fb != nullptr);

    auto result = verify_golden(*fb, "golden_gap_03b_text_dof_on",
                                make_golden_config("golden_gap_03"));
    INFO("Golden: ", result.message);
    REQUIRE_FALSE(result.golden_missing);
    CHECK(result.passed);
}

} // namespace
