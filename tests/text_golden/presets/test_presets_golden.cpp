// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/presets/test_presets_golden.cpp
//
// TICKET-SIMPLICITY-PRESETS §3C — Golden frame regression for the 5
// reusable TextDefinition-producing presets:
//
//   1. preset_title_centered      → preset_title_centered_1920x1080_F0.png
//   2. preset_subtitle_bottom     → preset_subtitle_bottom_1920x1080_F0.png
//   3. preset_caption_safe_area   → preset_caption_safe_area_1920x1080_F0.png
//   4. preset_kinetic_word        → preset_kinetic_word_1920x1080_F0.png
//   5. preset_lower_third         → preset_lower_third_1920x1080_F0.png
//
// Each TEST_CASE renders a single-frame composition (frame 0) on the
// canonical 1920×1080 canvas using one preset, then verifies the
// framebuffer against the corresponding PNG golden via the canonical
// `verify_golden()` helper.  The composition feeds the preset directly
// into the LayerBuilder::text(name, TextDefinition) overload (F2.C),
// which is the canonical pipeline for the 5 presets.
//
// Per AGENTS.md §honesty: 5 PNG re-bake requires a working build host
// (vcpkg-installed includes + tmpfs quota for full cmake build on this
// VPS); the CHANGELOG §13 entry documents the deferred re-bake command:
//
//   CHRONON3D_UPDATE_GOLDENS=1 ctest -R chronon3d_presets_golden_tests
//
// AGENTS.md v0.1 Cat-3 (zero new public SDK API — the 5 presets are in
// the existing `include/chronon3d/presets/text/text_presets_v1.hpp`
// header, additive-only) + Cat-5 (3-doc same-commit) freeze-compliant.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/presets/text/text_presets_v1.hpp>  // 5 preset functions
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

#include <tests/visual/support/golden_test.hpp>
#include <tests/helpers/test_utils.hpp>

using namespace chronon3d;
using namespace chronon3d::test;
using namespace chronon3d::presets::text;

namespace {

// Golden config — relaxed thresholds for cross-platform font rendering.
// Mirrors the text_export_golden_config pattern (the closest cousin).
GoldenTestConfig presets_golden_config() {
    GoldenTestConfig cfg;
    cfg.golden_directory   = "test_renders/golden/text/presets";
    cfg.artifact_directory = "test_renders/artifacts/text/presets";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error     = 5.0 / 255.0;
    cfg.threshold.max_abs_error           = 40.0 / 255.0;
    cfg.threshold.max_changed_pixel_ratio = 0.05;
    cfg.threshold.max_rmse                = 6.0 / 255.0;
    cfg.threshold.min_ssim                = 0.92;
    return cfg;
}

// Render a single-frame composition that uses a preset.  The composition
// has a dark background rect (so the white text is visible on screen)
// + a single text layer with the preset fed directly via the F2.C
// `LayerBuilder::text(name, TextDefinition&)` overload.
struct RenderedPreset {
    std::shared_ptr<Framebuffer> fb;
    size_t visible_pixels{0};
};

RenderedPreset render_preset(SoftwareRenderer& renderer, const TextDefinition& preset) {
    auto comp = composition(
        {.name = std::string("preset_") + preset.content.value,
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 1},
        [&renderer, preset](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());

            // Dark background so the white text is visible
            s.layer("bg", [](LayerBuilder& l) {
                l.rect("bg_rect", RectParams{
                    .size = {1920.0f, 1080.0f},
                    .color = Color{0.06f, 0.06f, 0.08f, 1.0f},
                    .pos = {960.0f, 540.0f, 0.0f},
                    .fill = FillStyle::solid(Color{0.06f, 0.06f, 0.08f, 1.0f})
                });
            });

            // Text layer — feeds the preset directly into the F2.C overload
            s.layer("text", [&renderer, preset](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text("label", preset);
            });

            return s.build();
        });

    RenderedPreset out;
    out.fb = renderer.render(comp, Frame{0});

    // Count visible pixels (anti-false-green sanity)
    if (out.fb) {
        const Color* data = out.fb->data();
        constexpr float kThreshold = 5.0f / 255.0f;
        for (size_t i = 0; i < out.fb->pixel_count(); ++i) {
            if (data[i].r > kThreshold || data[i].g > kThreshold || data[i].b > kThreshold) {
                ++out.visible_pixels;
            }
        }
    }
    return out;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. title_centered — "CHRONON3D" 96pt canvas center
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("PresetsGolden: title_centered 'CHRONON3D' 1920x1080 F0") {
    auto renderer = test::make_renderer();
    auto rendered = render_preset(renderer, title_centered("CHRONON3D"));
    REQUIRE(rendered.fb != nullptr);
    CHECK(rendered.visible_pixels > 0);  // anti-false-green sanity
    INFO("Visible pixels: ", rendered.visible_pixels, "/", rendered.fb->pixel_count());

    auto cfg = presets_golden_config();
    auto result = verify_golden(*rendered.fb, "preset_title_centered_1920x1080_F0", cfg);
    INFO("Golden: ", result.message);
    if (result.golden_missing) {
        MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create.");
        return;
    }
    CHECK(result.passed);
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. subtitle_bottom — "a subtitle" 48pt SafeAreaBottom
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("PresetsGolden: subtitle_bottom 'a subtitle' 1920x1080 F0") {
    auto renderer = test::make_renderer();
    auto rendered = render_preset(renderer, subtitle_bottom("a subtitle"));
    REQUIRE(rendered.fb != nullptr);
    CHECK(rendered.visible_pixels > 0);

    auto cfg = presets_golden_config();
    auto result = verify_golden(*rendered.fb, "preset_subtitle_bottom_1920x1080_F0", cfg);
    INFO("Golden: ", result.message);
    if (result.golden_missing) {
        MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create.");
        return;
    }
    CHECK(result.passed);
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. caption_safe_area — "a caption" 36pt SafeAreaCenter
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("PresetsGolden: caption_safe_area 'a caption' 1920x1080 F0") {
    auto renderer = test::make_renderer();
    auto rendered = render_preset(renderer, caption_safe_area("a caption"));
    REQUIRE(rendered.fb != nullptr);
    CHECK(rendered.visible_pixels > 0);

    auto cfg = presets_golden_config();
    auto result = verify_golden(*rendered.fb, "preset_caption_safe_area_1920x1080_F0", cfg);
    INFO("Golden: ", result.message);
    if (result.golden_missing) {
        MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create.");
        return;
    }
    CHECK(result.passed);
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. kinetic_word — "HERO" 120pt canvas center
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("PresetsGolden: kinetic_word 'HERO' 1920x1080 F0") {
    auto renderer = test::make_renderer();
    auto rendered = render_preset(renderer, kinetic_word("HERO"));
    REQUIRE(rendered.fb != nullptr);
    CHECK(rendered.visible_pixels > 0);

    auto cfg = presets_golden_config();
    auto result = verify_golden(*rendered.fb, "preset_kinetic_word_1920x1080_F0", cfg);
    INFO("Golden: ", result.message);
    if (result.golden_missing) {
        MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create.");
        return;
    }
    CHECK(result.passed);
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. lower_third — "MARCO ROSSI" 42pt SafeAreaLeft-lower
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("PresetsGolden: lower_third 'MARCO ROSSI' 1920x1080 F0") {
    auto renderer = test::make_renderer();
    auto rendered = render_preset(renderer, lower_third("MARCO ROSSI"));
    REQUIRE(rendered.fb != nullptr);
    CHECK(rendered.visible_pixels > 0);

    auto cfg = presets_golden_config();
    auto result = verify_golden(*rendered.fb, "preset_lower_third_1920x1080_F0", cfg);
    INFO("Golden: ", result.message);
    if (result.golden_missing) {
        MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create.");
        return;
    }
    CHECK(result.passed);
}
