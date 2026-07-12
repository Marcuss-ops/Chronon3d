// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_multilingual/03_mixed_baseline.cpp
//
// TICKET-FASE3-MULTILINGUAL §MixedBaseline — third of the 3 genuinely new
// multilingual text goldens.  Verifies the `TextRunBuilder::baseline_shift()`
// per-run animator that shifts the entire text run vertically relative
// to the layer's default baseline (used for mathematical superscript /
// subscript, chemical formulas, typographic custom-baseline text).
//
// 3 TEST_CASEs (3 PNG goldens in
// `test_renders/golden/text/text_multilingual/mixed_baseline/`):
//   - 01_default:     "X^2 + Y^2 = Z^2"  with baseline_shift = 0
//   - 02_subscript:   "X_2 + Y_2 = Z_2"  with baseline_shift = +20 (subscript-like drop)
//   - 03_superscript: "X^2 + Y^2 = Z^2"  with baseline_shift = -20 (superscript-like lift)
//
// Anti-duplicazione honour:
//   - Reuses `verify_golden()` + `GoldenTestConfig` + `golden_mode_from_environment()`
//     + `test::make_renderer()` from existing helpers.
//   - Uses the canonical `text_run(name, TextRunParams)` API + chained
//     `.baseline_shift(px)` mutator (the per-glyph animator injection path
//     documented at the top of `text_run_builder.hpp`).
//
// Honest-gap note (per AGENTS.md §honesty): `baseline_shift` is a
// per-RUN animator, so the entire run shifts uniformly (not per-glyph
// like a true CSS `vertical-align`).  This is sufficient for the
// mathematical-notation use case (one shift per line of math) but is
// NOT a substitute for per-glyph baseline variation.  The 3 PNG
// goldens therefore lock down the per-RUN baseline_shift behavior.
//
// Re-bake command:
//   CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextMultilingualMixedBaseline \
//       --test-case="Multilingual.MixedBaseline *"
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

// ── Golden config factory ──────────────────────────────────────────────
GoldenTestConfig make_baseline_config(std::string_view case_slug) {
    GoldenTestConfig cfg;
    cfg.golden_directory  = "test_renders/golden/text/text_multilingual/mixed_baseline";
    cfg.artifact_directory =
        std::string{"test_renders/artifacts/text/text_multilingual/mixed_baseline/"} +
        std::string{case_slug};
    cfg.mode = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error     = 5.0f  / 255.0f;
    cfg.threshold.max_abs_error          = 40.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio = 0.05f;
    cfg.threshold.max_rmse               = 6.0f  / 255.0f;
    cfg.threshold.min_ssim               = 0.92f;
    return cfg;
}

// ── Composition builder ────────────────────────────────────────────────
// Renders a single line of text at 200pt with a configurable
// `baseline_shift_px` value applied to the entire text run via the
// `TextRunBuilder::baseline_shift(px)` chained mutator.  Positive
// values shift DOWN (subscript-like), negative values shift UP
// (superscript-like).
Composition build_baseline_composition(
    SoftwareRenderer& renderer,
    const char*       content_value,
    f32               baseline_shift_px,
    const char*       case_slug
) {
    return composition(
        {.name = std::string{"Multilingual/MixedBaseline/"} + case_slug,
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = Frame{1}},
        [&renderer, content_value, baseline_shift_px](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("baseline", [content_value, baseline_shift_px](LayerBuilder& l) {
                l.text_run("baseline_text", TextRunParams{
                    .text = {
                        // TextSpec field order: content, position, font,
                        // layout, appearance (C++20 designated-init order
                        // must match declaration order per spec).
                        .content = {.value = content_value},
                        .placement = TextPlacement{TextPlacementKind::Absolute, {960.0f, 540.0f}},
                        .font = {
                            .font_path   = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size   = 200.0f
                        },
                        .layout = {
                            .box = {1920.0f, 1080.0f},
                            .align          = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle
                        },
                        .appearance = {.color = Color::white()}
                    },
                    .language = "",
                    .cache_layout = true
                })
                // baseline_shift is a TextRunBuilder chained mutator that
                // injects an implicit TextAnimatorSpec shifting the entire
                // run vertically.  Positive px = down (subscript), negative
                // px = up (superscript).  See text_run_builder.hpp §"Per-
                // glyph transform mutators" for the chaining semantics.
                .baseline_shift(baseline_shift_px)
                .commit();
            });
            return s.build();
        });
}

} // namespace

// ═══ Test 1 — Default baseline (no shift) ═════════════════════════════
TEST_CASE("Multilingual.MixedBaseline 01: default baseline — 1920x1080") {
    auto renderer = test::make_renderer();
    auto comp = build_baseline_composition(renderer, "X^2 + Y^2 = Z^2", 0.0f, "01_default");
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(fb != nullptr);

    auto r = verify_golden(*fb, "multilingual_mixed_baseline_01_default",
                           make_baseline_config("01_default"));
    INFO("Golden: ", r.message);
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}

// ═══ Test 2 — Subscript-like (baseline_shift = +20px) ══════════════════
TEST_CASE("Multilingual.MixedBaseline 02: subscript +20px — 1920x1080") {
    auto renderer = test::make_renderer();
    auto comp = build_baseline_composition(renderer, "X_2 + Y_2 = Z_2", 20.0f, "02_subscript");
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(fb != nullptr);

    auto r = verify_golden(*fb, "multilingual_mixed_baseline_02_subscript",
                           make_baseline_config("02_subscript"));
    INFO("Golden: ", r.message);
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}

// ═══ Test 3 — Superscript-like (baseline_shift = -20px) ════════════════
TEST_CASE("Multilingual.MixedBaseline 03: superscript -20px — 1920x1080") {
    auto renderer = test::make_renderer();
    auto comp = build_baseline_composition(renderer, "X^2 + Y^2 = Z^2", -20.0f, "03_superscript");
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(fb != nullptr);

    auto r = verify_golden(*fb, "multilingual_mixed_baseline_03_superscript",
                           make_baseline_config("03_superscript"));
    INFO("Golden: ", r.message);
    REQUIRE_FALSE(r.golden_missing);
    CHECK(r.passed);
}
