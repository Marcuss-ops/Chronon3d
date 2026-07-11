// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/presets/test_text_presets_golden.cpp
//
// F3.C — Golden tests for the 5 reusable TextDefinition presets.
//
// Each TEST_CASE constructs a composition that renders a single preset
// on a 1920×1080 canvas and compares against a golden PNG.
//
// Golden targets:
//   test_renders/golden/text/f3c_title_preset.png
//   test_renders/golden/text/f3c_subtitle_preset.png
//   test_renders/golden/text/f3c_caption_preset.png
//   test_renders/golden/text/f3c_kinetic_hero_preset.png
//   test_renders/golden/text/f3c_lower_third_preset.png
//
// Update mode: CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextPresetsGolden
//
// NOTE: requires working build host with vcpkg deps (glm, magic_enum, etc).
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
#include <chronon3d/presets/text_presets.hpp>

#include <tests/visual/support/golden_test.hpp>
#include <tests/helpers/test_utils.hpp>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

GoldenTestConfig make_preset_config() {
    GoldenTestConfig cfg;
    cfg.golden_directory   = "test_renders/golden/text";
    cfg.artifact_directory = "test_renders/artifacts/text/f3c_presets";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error       = 5.0f / 255.0f;
    cfg.threshold.max_abs_error            = 40.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio  = 0.05f;
    cfg.threshold.max_rmse                 = 6.0f / 255.0f;
    cfg.threshold.min_ssim                 = 0.92f;
    return cfg;
}

// ═══════════════════════════════════════════════════════════════════════════
// Helper: build a single-preset composition and render frame 0.
// ═══════════════════════════════════════════════════════════════════════════

Composition build_preset_composition(
    SoftwareRenderer& renderer,
    const char* name,
    TextDefinition preset)
{
    return composition(
        {.name       = name,
         .width      = 1920,
         .height     = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration   = 60},
        [&renderer, preset = std::move(preset)](const FrameContext& ctx) mutable -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("preset", [&](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text("text", std::move(preset));
            });
            return s.build();
        });
}

void run_preset_golden_test(
    const char* case_name,
    const char* preset_label,
    TextDefinition preset)
{
    auto renderer = test::make_renderer();
    auto comp = build_preset_composition(renderer, case_name, std::move(preset));
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);

    auto cfg = make_preset_config();
    auto result = verify_golden(*fb, preset_label, cfg);
    INFO("Golden: ", result.message);
    if (result.golden_missing) {
        MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create.");
        return;
    }
    CHECK(result.passed);
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// F3.C Test 1: title_preset — large bold centered title
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("F3.C Presets: title_preset — large bold centered title") {
    run_preset_golden_test(
        "F3C/Title",
        "f3c_title_preset",
        presets::title_preset("CHRONON3D ENGINE"));
}

// ═══════════════════════════════════════════════════════════════════════════
// F3.C Test 2: subtitle_preset — medium subtitle with dark background
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("F3.C Presets: subtitle_preset — medium subtitle with background bar") {
    run_preset_golden_test(
        "F3C/Subtitle",
        "f3c_subtitle_preset",
        presets::subtitle_preset("Motion Graphics • Real-Time • CPU-First"));
}

// ═══════════════════════════════════════════════════════════════════════════
// F3.C Test 3: caption_preset — small informational caption
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("F3.C Presets: caption_preset — small bottom caption") {
    run_preset_golden_test(
        "F3C/Caption",
        "f3c_caption_preset",
        presets::caption_preset("Frame 42 — Chronon3D v0.1 — 2026-07-10"));
}

// ═══════════════════════════════════════════════════════════════════════════
// F3.C Test 4: kinetic_hero_preset — premium animated hero text
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("F3.C Presets: kinetic_hero_preset — premium hero with stroke and glow") {
    run_preset_golden_test(
        "F3C/KineticHero",
        "f3c_kinetic_hero_preset",
        presets::kinetic_hero_preset("UNLEASH\nCREATIVITY"));
}

// ═══════════════════════════════════════════════════════════════════════════
// F3.C Test 5: lower_third_preset — bottom-third overlay
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("F3.C Presets: lower_third_preset — broadcast-style L3 overlay") {
    run_preset_golden_test(
        "F3C/LowerThird",
        "f3c_lower_third_preset",
        presets::lower_third_preset("John Doe • Creative Director"));
}
