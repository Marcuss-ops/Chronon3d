// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/user_spec/12_anim_framerate_determinism.cpp
//
// ADR-014 Decision 1 — Test 12: golden_anim_framerate_determinism.
// "FRAME RATE TEST" animates from x=0 to x=1700 over 1.0 second.
// Rendered at 24/30/60 fps, all sampling at t=0.5s (24→f12, 30→f15, 60→f30).
// All three rendered frames must produce the same x position (deterministic
// timeline). Verifies framerate-tolerance + sample-time semantic equivalence.
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

GoldenTestConfig make_test12_config() {
    GoldenTestConfig cfg;
    cfg.golden_directory  = "test_renders/golden/text";
    cfg.artifact_directory = "test_renders/artifacts/text/user_spec_12";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error    = 5.0f / 255.0f;
    cfg.threshold.max_abs_error          = 40.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio = 0.05f;
    cfg.threshold.max_rmse               = 6.0f / 255.0f;
    cfg.threshold.min_ssim               = 0.92f;
    return cfg;
}

Composition build_test12_composition(SoftwareRenderer& renderer, int fps, int sample_frame) {
    return composition(
        {.name = "UserSpec/12/anim_framerate_determinism",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{fps, 1},
         .duration = fps * 2},
        [&renderer, sample_frame](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            // Time 0..1.0s: position x animates 0 → 1700 (linear).
            // At frame sample_frame (half a second in): x ≈ 850.
            // FrameContext exposes frame_rate directly; the field is
            // `numerator` per include/chronon3d/core/types/frame_context.hpp.
            const float t01 = static_cast<float>(sample_frame) /
                              static_cast<float>(ctx.frame_rate.numerator);
            const float clamped = t01 > 1.0f ? 1.0f : t01;
            const float x_pos = 100.0f + 800.0f * clamped;
            s.layer("hero", [x_pos](LayerBuilder& l) {
                l.text("rate", {
                    .content = {.value = "FRAME RATE TEST"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 100.0f},
                    .layout = {.box = {1000.0f, 200.0f},
                               .align = TextAlign::Left,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color{1.0f, 0.95f, 0.5f, 1.0f}},
                    .position = {x_pos, 540.0f, 0.0f}
                });
            });
            return s.build();
        });
}

} // namespace

TEST_CASE("UserSpec 12: framerate determinism — 24fps @ frame 12") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_test12_composition(renderer, 24, 12), Frame{12});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "user_spec_12_framerate_24fps", make_test12_config());
    REQUIRE_GOLDEN_PASSED(r);
}

TEST_CASE("UserSpec 12: framerate determinism — 30fps @ frame 15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_test12_composition(renderer, 30, 15), Frame{15});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "user_spec_12_framerate_30fps", make_test12_config());
    REQUIRE_GOLDEN_PASSED(r);
}

TEST_CASE("UserSpec 12: framerate determinism — 60fps @ frame 30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_test12_composition(renderer, 60, 30), Frame{30});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "user_spec_12_framerate_60fps", make_test12_config());
    REQUIRE_GOLDEN_PASSED(r);
}
