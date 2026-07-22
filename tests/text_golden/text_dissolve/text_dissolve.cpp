// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_dissolve/text_dissolve.cpp
//
// TRN-04 — AnimatedTextDocument DissolveLayouts golden coverage.
// Verifies that a crossfade between two text documents produces a
// mathematically correct dissolve with alpha complementari:
//   incoming alpha = mix
//   outgoing alpha = 1 - mix
//
// Captures golden frames at exact 0%, 25%, 50%, 75%, 100% of a 28-frame gap:
//   F0  -> only outgoing text
//   F7  -> 25% incoming
//   F14 -> 50% blend
//   F21 -> 75% incoming
//   F28 -> only incoming text
//
// The text run also carries stroke + shadow so the golden can detect
// whether effects are applied symmetrically to both sides.
//
// Golden PNGs: test_renders/golden/text/text_dissolve/
// Artifacts:    test_renders/artifacts/text/text_dissolve/
//
// Update goldens: CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextDissolve
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

#include <chronon3d/text/animated_text_document.hpp>

#include <tests/visual/support/golden_test.hpp>
#include <tests/helpers/test_utils.hpp>
#include <tests/text_golden/text_clip/test_helpers.hpp>

#include <cmath>
#include <memory>
#include <string>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

// ═══════════════════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════════════════

FontSpec make_dissolve_font(float size = 180.0f) {
    FontSpec font;
    font.font_path  = "assets/fonts/Inter-Bold.ttf";
    font.font_family = "Inter";
    font.font_weight = 700;
    font.font_style  = "normal";
    font.font_size   = size;
    return font;
}

TextDocument make_doc(const std::string& text) {
    TextDocument doc;
    doc.utf8 = text;
    doc.defaults.font = make_dissolve_font();
    return doc;
}

std::shared_ptr<AnimatedTextDocument> make_dissolve_document() {
    auto doc = std::make_shared<AnimatedTextDocument>();
    // Keyframe at frame 0: "DISSOLVE" starts dissolving into next.
    doc->add_keyframe(SourceTextKeyframe{
        .frame = Frame{0},
        .document = make_doc("DISSOLVE"),
        .transition = SourceTextTransition::DissolveLayouts,
    });
    // Keyframe at frame 30: "FADEOUT" holds after the dissolve completes.
    doc->add_keyframe(SourceTextKeyframe{
        .frame = Frame{28},
        .document = make_doc("FADEOUT"),
        .transition = SourceTextTransition::Hold,
    });
    return doc;
}

GoldenTestConfig make_dissolve_config(std::string_view case_slug) {
    GoldenTestConfig cfg;
    cfg.golden_directory   = "test_renders/golden/text/text_dissolve";
    cfg.artifact_directory =
        std::string{"test_renders/artifacts/text/text_dissolve/"} +
        std::string{case_slug};
    cfg.mode = golden_mode_from_environment();
    // Slightly looser thresholds than static text because a dissolve is
    // inherently alpha-sensitive, but still tight enough to catch
    // non-complementary alpha or missing stroke/shadow.
    cfg.threshold.max_mean_abs_error     = 8.0f / 255.0f;
    cfg.threshold.max_abs_error          = 80.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio = 0.10f;
    cfg.threshold.max_rmse               = 10.0f / 255.0f;
    cfg.threshold.min_ssim               = 0.90f;
    return cfg;
}

Composition build_dissolve_composition(
    SoftwareRenderer& renderer,
    int canvas_w = 1920,
    int canvas_h = 1080
) {
    const float cx = static_cast<float>(canvas_w) * 0.5f;
    const float cy = static_cast<float>(canvas_h) * 0.5f;

    return composition(
        {.name = std::string{"TextDissolve/"} +
                  std::to_string(canvas_w) + "x" + std::to_string(canvas_h),
         .width = canvas_w, .height = canvas_h,
         .frame_rate = FrameRate{30, 1},
         .duration = Frame{28}},
        [&renderer, cx, cy, canvas_w, canvas_h](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());

            auto animated_doc = make_dissolve_document();

            s.layer("dissolve", [cx, cy, canvas_w, canvas_h, animated_doc](LayerBuilder& l) {
                TextRunSpec spec;
                spec.text.content.value = "DISSOLVE";
                spec.text.placement = TextPlacement{
                    TextPlacementKind::Absolute, {cx, cy}};
                spec.text.font = make_dissolve_font();
                spec.text.layout = TextLayoutSpec{
                    .box = {static_cast<float>(canvas_w) * 0.85f,
                            static_cast<float>(canvas_h) * 0.85f},
                    .align = TextAlign::Center,
                    .vertical_align = VerticalAlign::Middle,
                };
                spec.text.appearance = TextAppearanceSpec{
                    .color = Color::white(),
                    .paint = TextPaint{
                        .stroke_enabled = true,
                        .stroke_color = Color{0.2f, 0.6f, 1.0f, 1.0f},
                        .stroke_width = 4.0f,
                    },
                    .shadows = {
                        TextShadow{
                            .enabled = true,
                            .offset = {6.0f, 6.0f},
                            .blur = 12.0f,
                            .opacity = 0.35f,
                            .color = {0.0f, 0.0f, 0.0f, 1.0f},
                        },
                    },
                };

                l.animated_text("title", spec)
                 .from_animated_document(std::move(animated_doc))
                 .commit();
            });

            return s.build();
        });
}

void run_dissolve_frame(int frame, const std::string& case_name) {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_dissolve_composition(renderer, 1920, 1080), Frame{frame});
    REQUIRE(fb != nullptr);

    const AlphaBBox bbox = alpha_bbox(*fb);
    CHECK_FALSE(bbox.empty());

    auto result = verify_golden(*fb, case_name, make_dissolve_config(case_name));
    CHECK_FALSE(result.golden_missing);
    if (!result.golden_missing) {
        CHECK(result.passed);
    }
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Golden cases
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextDissolve.0pct_1920x1080") {
    run_dissolve_frame(0, "text_dissolve_0pct_1920x1080");
}

TEST_CASE("TextDissolve.25pct_1920x1080") {
    run_dissolve_frame(7, "text_dissolve_25pct_1920x1080");
}

TEST_CASE("TextDissolve.50pct_1920x1080") {
    run_dissolve_frame(14, "text_dissolve_50pct_1920x1080");
}

TEST_CASE("TextDissolve.75pct_1920x1080") {
    run_dissolve_frame(21, "text_dissolve_75pct_1920x1080");
}

TEST_CASE("TextDissolve.100pct_1920x1080") {
    run_dissolve_frame(28, "text_dissolve_100pct_1920x1080");
}
