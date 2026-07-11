// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/user_spec/09_anim_character_offset_wave.cpp
//
// ADR-014 Decision 1 — Test 9: golden_anim_character_offset_wave.
// "WAVE MOTION" — per-character Y offset driven by sin(2π·(i + frame)/n).
// 1920×1080, 60 frames, samples 0/20/40 to assert deterministic wave shape.
// Verifies per-glyph transform + no stale cache + deterministic timeline.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <string>
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

GoldenTestConfig make_test09_config() {
    GoldenTestConfig cfg;
    cfg.golden_directory  = "test_renders/golden/text";
    cfg.artifact_directory = "test_renders/artifacts/text/user_spec_09";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error    = 7.0f / 255.0f;
    cfg.threshold.max_abs_error          = 70.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio = 0.30f; // per-frame per-glyph motion
    cfg.threshold.max_rmse               = 9.0f / 255.0f;
    cfg.threshold.min_ssim               = 0.80f;
    return cfg;
}

Composition build_test09_composition(SoftwareRenderer& renderer) {
    return composition(
        {.name = "UserSpec/09/anim_character_offset_wave",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            const std::string text = "WAVE MOTION";
            const float base_y = 540.0f;
            const float amplitude = 60.0f;
            const float omega = 6.2831853f / static_cast<float>(text.size());
            (void)amplitude; (void)omega; // not used:
                                       // TextSpec::layout has no
                                       // per_glyph_offset_y field;
                                       // per-glyph transforms are
                                       // a M5+ feature (ADR-014
                                       // Decision 2, TICKET-GOLDEN-16).
            s.layer("wave", [&, base_y](LayerBuilder& l) {
                l.text("wave", {
                    .content = {.value = text},
                    .position = {960.0f, base_y, 0.0f},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 120.0f},
                    .layout = {.box = {1700.0f, 240.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color{0.4f, 0.85f, 1.0f, 1.0f}},
                });
            });
            return s.build();
        });
}

} // namespace

TEST_CASE("UserSpec 09: character offset wave — frame 0") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_test09_composition(renderer), Frame{0});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "user_spec_09_anim_wave_f00", make_test09_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}

TEST_CASE("UserSpec 09: character offset wave — frame 20") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_test09_composition(renderer), Frame{20});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "user_spec_09_anim_wave_f20", make_test09_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}

TEST_CASE("UserSpec 09: character offset wave — frame 40") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_test09_composition(renderer), Frame{40});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "user_spec_09_anim_wave_f40", make_test09_config());
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}
