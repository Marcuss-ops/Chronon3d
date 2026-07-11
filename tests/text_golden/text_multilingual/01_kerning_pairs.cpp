// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_multilingual/01_kerning_pairs.cpp
//
// TICKET-FASE3-MULTILINGUAL §KerningPairs — first of the 3 genuinely new
// multilingual text goldens.  Verifies that classic kerning pairs
// ("AV", "TY", "WA", "We", "Ya", "To") are rendered with the font's
// GPOS kern table applied at multiple font sizes + tracking values.
//
// 3 TEST_CASEs (3 PNG goldens in
// `test_renders/golden/text/text_multilingual/kerning_pairs/`):
//   - 01_hero_200pt:    kerning pairs at 200pt (hero/poster size)
//   - 02_body_96pt:     kerning pairs at 96pt  (body text size)
//   - 03_with_tracking: kerning pairs at 200pt + tracking +8.0
//
// Honest-gap note (per AGENTS.md §honesty): the
// `TextRunSpec::features` field does NOT exist on the public TextRunParams
// struct yet (the `features` string is only present on the SHAPED RESULT
// `TextRunLayout`).  The 3 tests therefore exercise HarfBuzz's DEFAULT
// feature set, which enables kerning (kern=1) implicitly when the
// font's GPOS kern table is present.  When a `features` field is
// promoted from TextRunLayout to TextRunSpec, the 3 tests will be
// extended to compare kern=1 vs kern=0 — for now they lock down the
// kern=1 path at 3 different size+tracking contexts.
//
// Anti-duplicazione honour:
//   - Reuses `verify_golden()` + `GoldenTestConfig` + `golden_mode_from_environment()`
//     + `test::make_renderer()` from existing helpers (no parallel infrastructure).
//   - Reuses the canonical `composition()` + `SceneBuilder` + `LayerBuilder`
//     pipeline from `05_bidi_english_arabic_mixed.cpp`.
//
// Per AGENTS.md §honesty: 3 PNG re-bake requires a working build host
// (vcpkg-installed Inter-Bold.ttf + tmpfs quota for full cmake build on
// this VPS); the 3 test cases gracefully skip on `result.golden_missing`.
//
// Re-bake command:
//   CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextMultilingualKerningPairs \
//       --test-case="Multilingual.KerningPairs *"
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
GoldenTestConfig make_kerning_config(std::string_view case_slug) {
    GoldenTestConfig cfg;
    cfg.golden_directory  = "test_renders/golden/text/text_multilingual/kerning_pairs";
    cfg.artifact_directory =
        std::string{"test_renders/artifacts/text/text_multilingual/kerning_pairs/"} +
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
// Renders the kerning test text at a configurable font size + tracking
// value.  The text "AV  TY  WA  We  Ya  To" contains 6 classic kerning
// pairs separated by 2 spaces so the kerning offset is visually
// isolated from inter-word spacing.
Composition build_kerning_composition(
    SoftwareRenderer& renderer,
    f32               font_size_pt,
    f32               tracking_px,
    const char*       case_slug
) {
    return composition(
        {.name = std::string{"Multilingual/KerningPairs/"} + case_slug,
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = Frame{1}},
        [&renderer, font_size_pt, tracking_px](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("kerning", [font_size_pt, tracking_px](LayerBuilder& l) {
                l.text_run("kerning_pair_text", TextRunParams{
                    .text = {
                        // TextSpec field order: content, position, font,
                        // layout, appearance (C++20 designated-init order
                        // must match declaration order per spec).
                        .content = {.value = "AV  TY  WA  We  Ya  To"},
                        .position = {960.0f, 540.0f, 0.0f},
                        .font = {
                            .font_path   = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size   = font_size_pt
                        },
                        .layout = {
                            .box = {1920.0f, 1080.0f},
                            .align          = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle,
                            .tracking       = tracking_px
                        },
                        .appearance = {.color = Color::white()}
                    },
                    .language = "",
                    .cache_layout = true
                }).commit();
            });
            return s.build();
        });
}

} // namespace

// ═══ Test 1 — Kerning at 200pt (hero size) ═════════════════════════════
TEST_CASE("Multilingual.KerningPairs 01: hero 200pt — 1920x1080") {
    auto renderer = test::make_renderer();
    auto comp = build_kerning_composition(renderer, 200.0f, 0.0f, "01_hero_200pt");
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(fb != nullptr);

    auto r = verify_golden(*fb, "multilingual_kerning_pairs_01_hero_200pt",
                           make_kerning_config("01_hero_200pt"));
    REQUIRE_GOLDEN_PASSED(r);
}

// ═══ Test 2 — Kerning at 96pt (body size) ══════════════════════════════
TEST_CASE("Multilingual.KerningPairs 02: body 96pt — 1920x1080") {
    auto renderer = test::make_renderer();
    auto comp = build_kerning_composition(renderer, 96.0f, 0.0f, "02_body_96pt");
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(fb != nullptr);

    auto r = verify_golden(*fb, "multilingual_kerning_pairs_02_body_96pt",
                           make_kerning_config("02_body_96pt"));
    REQUIRE_GOLDEN_PASSED(r);
}

// ═══ Test 3 — Kerning + tracking +8px ══════════════════════════════════
TEST_CASE("Multilingual.KerningPairs 03: kerning + tracking +8px — 1920x1080") {
    auto renderer = test::make_renderer();
    auto comp = build_kerning_composition(renderer, 200.0f, 8.0f, "03_with_tracking");
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(fb != nullptr);

    auto r = verify_golden(*fb, "multilingual_kerning_pairs_03_with_tracking",
                           make_kerning_config("03_with_tracking"));
    REQUIRE_GOLDEN_PASSED(r);
}
