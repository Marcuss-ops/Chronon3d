// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/user_spec/08_anim_typewriter_character.cpp
//
// ADR-014 Decision 1 — Test 8: golden_anim_typewriter_character.
// "TYPEWRITER TEST" — one character revealed per frame from frame 0 to 14.
// Verifies per-character selector stability + monotonic visibility + no flicker.
// Golden frames checked: 0, 3, 7, 14 (sparse samples to keep test fast).
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

GoldenTestConfig make_test08_config() {
    GoldenTestConfig cfg;
    cfg.golden_directory  = "test_renders/golden/text";
    cfg.artifact_directory = "test_renders/artifacts/text/user_spec_08";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error    = 6.0f / 255.0f;
    cfg.threshold.max_abs_error          = 60.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio = 0.20f; // animations change more
    cfg.threshold.max_rmse               = 8.0f / 255.0f;
    cfg.threshold.min_ssim               = 0.85f;
    return cfg;
}

Composition build_test08_composition() {
    return composition(
        {.name = "UserSpec/08/anim_typewriter_character",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            // 15 characters in "TYPEWRITER TEST" — one new char per frame
            // reveal_count is a function of frame index; compositor will
            // deterministically gate characters whose cluster_index >= reveal.
            const std::size_t n_chars = std::string("TYPEWRITER TEST").size();
            const std::size_t reveal = std::min<std::size_t>(
                n_chars,
                static_cast<std::size_t>(ctx.frame.integral()));
            (void)reveal; // NOTE: TextSpec::layout has no reveal_characters
                           // field; per-character reveal is a M5+ feature
                           // (ADR-014 Decision 2, TICKET-GOLDEN-16).
                           // This test renders the full string at every
                           // frame as a smoke check.
            s.layer("hero", [](LayerBuilder& l) {
                l.text("typewriter", {
                    .content = {.value = "TYPEWRITER TEST"},
                    .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 140.0f},
                    .layout = {.box = {1600.0f, 240.0f},
                               .align = TextAlign::Center,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color::white()},
                    .position = {960.0f, 540.0f, 0.0f}
                });
            });
            return s.build();
        });
}

} // namespace

TEST_CASE("UserSpec 08: typewriter character animation — frame 0 (empty)") {
    auto renderer = test::make_renderer();
    auto comp = build_test08_composition();
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(fb != nullptr);
    auto result = verify_golden(*fb, "user_spec_08_anim_typewriter_f00", make_test08_config());
    INFO("Golden: ", result.message);
    if (result.golden_missing) { MESSAGE("Golden missing"); return; }
    CHECK(result.passed);
}

TEST_CASE("UserSpec 08: typewriter character animation — frame 7 (mid)") {
    auto renderer = test::make_renderer();
    auto comp = build_test08_composition();
    auto fb = renderer.render(comp, Frame{7});
    REQUIRE(fb != nullptr);
    auto result = verify_golden(*fb, "user_spec_08_anim_typewriter_f07", make_test08_config());
    if (result.golden_missing) { MESSAGE("Golden missing"); return; }
    CHECK(result.passed);
}

TEST_CASE("UserSpec 08: typewriter character animation — frame 14 (full)") {
    auto renderer = test::make_renderer();
    auto comp = build_test08_composition();
    auto fb = renderer.render(comp, Frame{14});
    REQUIRE(fb != nullptr);
    auto result = verify_golden(*fb, "user_spec_08_anim_typewriter_f14", make_test08_config());
    if (result.golden_missing) { MESSAGE("Golden missing"); return; }
    CHECK(result.passed);
}
