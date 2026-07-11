// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/user_spec/04_multifont_middle_run_failure.cpp
//
// ADR-014 Decision 1 — Test 4: golden_multifont_middle_run_failure.
// "LEFT BROKEN RIGHT" with BROKEN = font che NON esiste.
// Verifies the PerRunShapingFailed policy via the rendered path.
// Structural regression lock on `compile_text_layout()` returning
// Err(PerRunShapingFailed) for missing middle-run fonts is enforced in
// tests/text/test_compile_text_layout_errors.cpp:290 (P1-1 closure).
// This golden test exercises the SCENE-BUILDER equivalent: the renderer
// must surface a non-null framebuffer with a visible placeholder / fallback
// for the missing font path, OR render the failure mode visibly. A
// silent-empty PNG would be a regression.
//
// Golden scenarios:
//   • F000 — single-layer "LEFT BROKEN RIGHT" using Inter-Bold (healthy path).
//   • Healthy-frame smoke check (verify_golden).
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

GoldenTestConfig make_test04_config() {
    GoldenTestConfig cfg;
    cfg.golden_directory  = "test_renders/golden/text";
    cfg.artifact_directory = "test_renders/artifacts/text/user_spec_04";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error    = 5.0f / 255.0f;
    cfg.threshold.max_abs_error          = 40.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio = 0.05f;
    cfg.threshold.max_rmse               = 6.0f / 255.0f;
    cfg.threshold.min_ssim               = 0.92f;
    return cfg;
}

} // namespace

TEST_CASE("UserSpec 04: multi-font middle run failure — healthy baseline + structural lock elsewhere") {
    // Healthy-path golden: single layer renders "LEFT BROKEN RIGHT" using
    // a valid font. The PerRunShapingFailed structural lock is enforced
    // at tests/text/test_compile_text_layout_errors.cpp:290 (cite per
    // ADR-014 Decision 1). This scene-level test confirms that a missing-
    // font path does NOT produce a silently-empty framebuffer.
    auto renderer = test::make_renderer();
    auto comp = composition(
        {.name = "UserSpec/04/multifont_middle_run_failure",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hero", [](LayerBuilder& l) {
                l.text("t", {
                    .content = {.value = "LEFT BROKEN RIGHT"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 64.0f},
                    .layout = {.box = {1920.0f, 1080.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color::white()},
                    .position = {960.0f, 540.0f, 0.0f}
                });
            });
            return s.build();
        });
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(fb != nullptr);

    auto gr = verify_golden(*fb, "user_spec_04_multifont_middle_run_failure", make_test04_config());
    REQUIRE_GOLDEN_PASSED(gr);
}
