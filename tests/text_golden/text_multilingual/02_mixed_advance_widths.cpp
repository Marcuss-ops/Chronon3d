// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_multilingual/02_mixed_advance_widths.cpp
//
// TICKET-FASE3-MULTILINGUAL §MixedAdvanceWidths — second of the 3 genuinely
// new multilingual text goldens.  Verifies that mixed-script text
// (Latin proportional + CJK uniform + CJK-in-Latin-line) renders without
// breaking alignment or producing missing-glyph fallback boxes.
//
// 3 TEST_CASEs (3 PNG goldens in
// `test_renders/golden/text/text_multilingual/mixed_advance_widths/`):
//   - 01_latin_only:        "Hello World" Inter-Bold proportional
//   - 02_cjk_only:          "你好世界" 4 CJK glyphs (uniform advance)
//   - 03_mixed:             "Hello你好World" mixed scripts
//
// Anti-duplicazione honour:
//   - Reuses `verify_golden()` + `GoldenTestConfig` + `golden_mode_from_environment()`
//     + `test::make_renderer()` from existing helpers.
//   - Uses the canonical `text(name, TextSpec)` overload (flat, no
//     per-run animators needed for the mixed-advance-widths test).
//
// Honest-gap note (per AGENTS.md §honesty): Inter-Bold.ttf does NOT
// contain CJK glyphs natively.  When a CJK character is shaped with a
// font that lacks the glyph, Chronon3D's font-resolver falls back to
// the system font fallback chain (NotoSansCJK on Linux).  The 3 PNG
// goldens therefore depend on the system font fallback being present
// and reproducible.  If a build host lacks NotoSansCJK or equivalent,
// the CJK glyphs will render as `.notdef` (empty rectangles) and the
// golden comparison will fail with a visible difference.  The 3 tests
// gracefully skip on `result.golden_missing`.
//
// Re-bake command:
//   CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextMultilingualMixedAdvanceWidths \
//       --test-case="Multilingual.MixedAdvanceWidths *"
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

#include <string>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

// ── Golden config factory ──────────────────────────────────────────────
GoldenTestConfig make_advance_config(std::string_view case_slug) {
    GoldenTestConfig cfg;
    cfg.golden_directory  = "test_renders/golden/text/text_multilingual/mixed_advance_widths";
    cfg.artifact_directory =
        std::string{"test_renders/artifacts/text/text_multilingual/mixed_advance_widths/"} +
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
// Renders a single line of text at 200pt, centered on a 1920×1080
// canvas.  The `content_value` parameter carries the script mix:
//   - "Hello World"               (Latin only)
//   - "\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\xe7\x95\x8c" (CJK only: 你好世界)
//   - "Hello\xe4\xbd\xa0\xe5\xa5\xbdWorld"            (Mixed)
Composition build_advance_composition(
    SoftwareRenderer& renderer,
    const char*       content_value,
    const char*       case_slug
) {
    return composition(
        {.name = std::string{"Multilingual/MixedAdvanceWidths/"} + case_slug,
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = Frame{1}},
        [&renderer, content_value](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("mixed", [content_value](LayerBuilder& l) {
                l.text("mixed_text", {
                    // TextSpec field order: content, position, font,
                    // layout, appearance (C++20 designated-init order
                    // must match declaration order per spec).
                    .content = {.value = content_value},
                    .position = {960.0f, 540.0f, 0.0f},
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
                });
            });
            return s.build();
        });
}

} // namespace

// ═══ Test 1 — Latin only ═══════════════════════════════════════════════
TEST_CASE("Multilingual.MixedAdvanceWidths 01: Latin only — 1920x1080") {
    auto renderer = test::make_renderer();
    auto comp = build_advance_composition(renderer, "Hello World", "01_latin_only");
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(fb != nullptr);

    auto r = verify_golden(*fb, "multilingual_mixed_advance_widths_01_latin_only",
                           make_advance_config("01_latin_only"));
    INFO("Golden: ", r.message);
    if (r.golden_missing) {
        MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create.");
        return;
    }
    CHECK(r.passed);
}

// ═══ Test 2 — CJK only ═════════════════════════════════════════════════
// 你好世界 = "Hello World" in Chinese (Mandarin).  Each CJK glyph has
// a uniform advance width (typically 1em square), so 4 CJK glyphs
// occupy the same total width as 4 "em-wide" Latin glyphs would.
TEST_CASE("Multilingual.MixedAdvanceWidths 02: CJK only — 1920x1080") {
    auto renderer = test::make_renderer();
    // 你好世界 in UTF-8: \xe4\xbd\xa0 \xe5\xa5\xbd \xe4\xb8\x96 \xe7\x95\x8c
    auto comp = build_advance_composition(
        renderer,
        "\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\xe7\x95\x8c",
        "02_cjk_only");
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(fb != nullptr);

    auto r = verify_golden(*fb, "multilingual_mixed_advance_widths_02_cjk_only",
                           make_advance_config("02_cjk_only"));
    INFO("Golden: ", r.message);
    if (r.golden_missing) {
        MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create.");
        return;
    }
    CHECK(r.passed);
}

// ═══ Test 3 — Mixed Latin + CJK ═════════════════════════════════════════
// "Hello" + 你好 + "World" — exercises the mixed-script shaping path
// where HarfBuzz must segment the line into LTR-Latin + LTR-CJK +
// LTR-Latin runs (CJK is LTR direction but uses ideographic glyph
// metrics that differ from Latin).
TEST_CASE("Multilingual.MixedAdvanceWidths 03: mixed Latin + CJK — 1920x1080") {
    auto renderer = test::make_renderer();
    auto comp = build_advance_composition(
        renderer,
        "Hello\xe4\xbd\xa0\xe5\xa5\xbdWorld",
        "03_mixed");
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(fb != nullptr);

    auto r = verify_golden(*fb, "multilingual_mixed_advance_widths_03_mixed",
                           make_advance_config("03_mixed"));
    INFO("Golden: ", r.message);
    if (r.golden_missing) {
        MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create.");
        return;
    }
    CHECK(r.passed);
}
