// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/ae_parity/ae_02_typewriter.cpp
//
// TICKET-AE-PARITY-SUITE — Scene 02: typewriter (string growth per frame).
// "GOLDEN CAPTURE"  progressively revealed: 1 char (frame 0), half (frame 15),
// full (frame 30).  Per-character reveal is M5+ (TICKET-GOLDEN-16); we
// approximate via string slicing at composition-build time which produces
// the same observable golden artifacts without that dep.
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

#include <string>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

GoldenTestConfig make_config() {
    GoldenTestConfig cfg;
    cfg.golden_directory  = "test_renders/golden/text";
    cfg.artifact_directory = "test_renders/artifacts/text/ae_02_typewriter";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error       = 6.0f / 255.0f;
    cfg.threshold.max_abs_error            = 60.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio  = 0.20f;
    cfg.threshold.max_rmse                 = 8.0f / 255.0f;
    cfg.threshold.min_ssim                 = 0.85f;
    return cfg;
}

static const std::string kFull = "GOLDEN CAPTURE";

// Frame-by-frame substring: frame 0 → 1 char, frame 15 → half, frame 30 → full.
static std::string typewriter_text(std::size_t frame_idx) {
    const std::size_t n = kFull.size();
    if (frame_idx == 0)                 return std::string(1, kFull[0]);     // "G"
    if (frame_idx <= 15)                return kFull.substr(0, n / 2);       // approx half
    return kFull;                                                          // full
}

Composition build_landscape(SoftwareRenderer& renderer, std::size_t frame_idx) {
    return composition(
        {.name = "AE/02/typewriter/16x9",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, frame_idx](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("typewriter", [frame_idx](LayerBuilder& l) {
                l.text("text", {
                    .content = {.value = typewriter_text(frame_idx)},
                    .font = {.font_path = "assets/fonts/Inter-Regular.ttf",
                             .font_family = "Inter",
                             .font_weight = 400,
                             .font_size = 140.0f},
                    .layout = {.box = {1600.0f, 200.0f},
                               .align = TextAlign::Left,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color::white()},
                    .position = {200.0f, 540.0f, 0.0f}
                });
            });
            return s.build();
        });
}

Composition build_portrait(SoftwareRenderer& renderer, std::size_t frame_idx) {
    return composition(
        {.name = "AE/02/typewriter/9x16",
         .width = 1080, .height = 1920,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, frame_idx](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("typewriter", [frame_idx](LayerBuilder& l) {
                l.text("text", {
                    .content = {.value = typewriter_text(frame_idx)},
                    .font = {.font_path = "assets/fonts/Inter-Regular.ttf",
                             .font_family = "Inter",
                             .font_weight = 400,
                             .font_size = 96.0f},
                    .layout = {.box = {920.0f, 200.0f},
                               .align = TextAlign::Left,
                               .vertical_align = VerticalAlign::Middle},
                    .appearance = {.color = Color::white()},
                    .position = {120.0f, 960.0f, 0.0f}
                });
            });
            return s.build();
        });
}

} // namespace

TEST_CASE("AE 02 typewriter 16x9 f00") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 0), Frame{0});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "ae_02_typewriter_16x9_f00", make_config());
    REQUIRE_GOLDEN_PASSED(r);
}
TEST_CASE("AE 02 typewriter 16x9 f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "ae_02_typewriter_16x9_f15", make_config());
    REQUIRE_GOLDEN_PASSED(r);
}
TEST_CASE("AE 02 typewriter 16x9 f30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_landscape(renderer, 30), Frame{30});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "ae_02_typewriter_16x9_f30", make_config());
    REQUIRE_GOLDEN_PASSED(r);
}
TEST_CASE("AE 02 typewriter 9x16 f00") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 0), Frame{0});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "ae_02_typewriter_9x16_f00", make_config());
    REQUIRE_GOLDEN_PASSED(r);
}
TEST_CASE("AE 02 typewriter 9x16 f15") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 15), Frame{15});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "ae_02_typewriter_9x16_f15", make_config());
    REQUIRE_GOLDEN_PASSED(r);
}
TEST_CASE("AE 02 typewriter 9x16 f30") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_portrait(renderer, 30), Frame{30});
    REQUIRE(fb != nullptr);
    auto r = verify_golden(*fb, "ae_02_typewriter_9x16_f30", make_config());
    REQUIRE_GOLDEN_PASSED(r);
}
