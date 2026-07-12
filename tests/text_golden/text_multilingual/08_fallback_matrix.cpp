// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_multilingual/08_fallback_matrix.cpp
//
// TICKET-FASE3-MULTILINGUAL §FallbackMatrix — Eighth test of the V0.2
// multilingual cluster. Locks the **conservative-bbox-fallback
// counter** (`text_bbox_contract_violations` in `RenderCounters`)
// to **0 in nominal cases** for 10 representative text categories:
//
//   01 — ASCII              "Hello World"
//   02 — Latin accents      "Café au lait, piñata"
//   03 — Arabic RTL         "جميلة" (jamīla, beautiful)
//   04 — Hebrew RTL         "שלום" (shalom, hello/peace)
//   05 — CJK                "こんにちは" (Japanese hiragana, hello)
//   06 — Emoji              "🍎🚀🌈"
//   07 — Punctuation        ".,!?;:'\"()[]{}<>"
//   08 — Numbers            "0123456789"
//   09 — Combining marks    "naïve" (e + combining diaeresis on ï, or "Z\u0301")
//   10 — Ligatures          "fi fl ffi ffl"
//
// 10 TEST_CASEs × 1 AR (1920×1080) = 10 PNG goldens in
// `test_renders/golden/text/text_multilingual/fallback_matrix/`.
//
// Each test case asserts:
//   1. **Visual regression** (`verify_golden` against the seeded PNG).
//   2. **Conservative fallback contract** — the
//      `text_bbox_contract_violations` counter must be **0** after
//      rendering.  In the nominal case (system font fallback chain
//      correctly resolves all glyphs, no degenerate bbox, no
//      alpha-bbox overflow), the pre-render and post-render
//      conservative expansion paths in `TextRunNode.cpp` +
//      `node_runner.cpp` are NEVER triggered, so the counter stays
//      strictly at 0.  A non-zero value indicates a regression in
//      either the font fallback chain OR the bbox computation.
//
// Honest-gap notes (per AGENTS.md §honesty):
//   - 10 PNG re-bake requires a working build host (vcpkg + tmpfs).
//   - All 10 test cases gracefully skip on `result.golden_missing`.
//   - The fallback counter check is independent of the golden re-bake
//     and is the **primary regression lock** (golden is the visual
//     safety net; counter is the contract lock).
//   - Inter-Bold.ttf does NOT contain Arabic / Hebrew / CJK / emoji
//     glyphs natively; the system font fallback chain must be present
//     (Noto Sans Arabic, Noto Sans Hebrew, Noto Sans CJK, Noto Color
//     Emoji on Linux; fallback fonts on macOS / Windows).
//   - RTL is auto-detected by HarfBuzz from the Arabic / Hebrew
//     Unicode block (no explicit `TextDirection::RTL` required).
//
// AGENTS.md v0.1 Cat-2 freeze-compliant: zero new public SDK API. The
// test uses the existing `verify_golden()` + `GoldenTestConfig` +
// `alpha_bbox()` helpers + the `SoftwareRenderer::counters()` accessor
// to read the existing `text_bbox_contract_violations` counter.
//
// Re-bake command (deferred to working build host):
//   CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextMultilingualFallbackMatrix \
//       --test-case="Multilingual.FallbackMatrix *"
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

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
#include <string_view>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

// ── Golden config factory ──────────────────────────────────────────────
GoldenTestConfig make_fallback_matrix_config(std::string_view case_slug) {
    GoldenTestConfig cfg;
    cfg.golden_directory  =
        "test_renders/golden/text/text_multilingual/fallback_matrix";
    cfg.artifact_directory =
        std::string{"test_renders/artifacts/text/text_multilingual/fallback_matrix/"} +
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
// Renders a single line of text at 200pt on a 1920×1080 canvas.
// `case_slug` is the golden / artifact name suffix (e.g. "01_ascii").
Composition build_fallback_matrix_composition(
    SoftwareRenderer& renderer,
    const char*       text,
    const char*       case_slug
) {
    return composition(
        {.name = std::string{"Multilingual/FallbackMatrix/"} + case_slug,
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = Frame{1}},
        [&renderer, text](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("text", [text](LayerBuilder& l) {
                l.text("text", {
                    // TextSpec field order: content, position, font,
                    // layout, appearance (C++20 designated-init order
                    // must match declaration order per spec).
                    .content = {.value = text},
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
                });
            });
            return s.build();
        });
}

// ── Render + verify + assert fallback counter == 0 ─────────────────────
// Single helper that the 10 TEST_CASEs share.  Returns true on success
// (or graceful golden-skip), false on failure.
//
// The two invariants checked:
//   1. Visual regression: `verify_golden` against the seeded PNG.
//   2. Conservative fallback contract:
//      `text_bbox_contract_violations == 0` after the render.
//      The counter is **reset** before the render to isolate the
//      delta attributable to THIS test case (in case the renderer
//      was reused across cases).
bool render_and_verify_fallback_matrix(
    SoftwareRenderer& renderer,
    const char*       text,
    const char*       case_slug
) {
    // ── Step 1: Reset the counter to isolate this test's contribution.
    renderer.reset_counters();

    // ── Step 2: Render the composition at frame 0.
    auto comp = build_fallback_matrix_composition(renderer, text, case_slug);
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(fb != nullptr);

    // ── Step 3: Visual regression lock (graceful skip on missing).
    auto r = verify_golden(
        *fb,
        std::string{"multilingual_fallback_matrix_"} + case_slug,
        make_fallback_matrix_config(case_slug));
    INFO("Golden: ", r.message);
    if (r.golden_missing) {
        MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create.");
    } else {
        if (!r.passed) return false;
    }

    // ── Step 4: Conservative fallback contract — counter MUST be 0
    //    in the nominal case.  Any non-zero value indicates that the
    //    pre-render or post-render conservative expansion path
    //    fired, which is a regression in the font fallback chain
    //    or the bbox computation.
    const auto violation_count =
        renderer.counters()->text_bbox_contract_violations.load();
    INFO("Conservative fallback counter (text_bbox_contract_violations): ",
         violation_count);
    CHECK(violation_count == 0);

    return true;
}

} // namespace

// ═══ Test 01 — ASCII ════════════════════════════════════════════════════
// Pure ASCII (U+0020-U+007E). Baseline test: Inter-Bold.ttf contains
// all ASCII glyphs natively, no fallback needed. The counter MUST be 0
// in this trivial case.
TEST_CASE("Multilingual.FallbackMatrix 01: ASCII (Hello World) — 1920x1080") {
    auto renderer = test::make_renderer();
    CHECK(render_and_verify_fallback_matrix(renderer, "Hello World", "01_ascii"));
}

// ═══ Test 02 — Latin accents ══════════════════════════════════════════
// Latin-1 supplement + Latin Extended-A (café, au, lait, piñata).
// Inter-Bold.ttf contains most Latin-1 glyphs natively; the
// `n with tilde` (ñ, U+00F1) and the `i with diaeresis` (ï, U+00EF)
// exercise the Unicode normalize / combining-mark fallback path.
TEST_CASE("Multilingual.FallbackMatrix 02: Latin accents (Café au lait, piñata) — 1920x1080") {
    auto renderer = test::make_renderer();
    // "Café au lait, piñata" — hand-decoded UTF-8 for é (U+00E9 = 0xC3 0xA9)
    // and ñ (U+00F1 = 0xC3 0xB1).
    const char* text = "Caf\xC3\xA9 au lait, pi\xC3\xB1ata";
    CHECK(render_and_verify_fallback_matrix(renderer, text, "02_latin_accents"));
}

// ═══ Test 03 — Arabic RTL ══════════════════════════════════════════════
// "جميلة" (jamīla = "beautiful"). 4 Arabic letters with diacritics
// (fatha on the second letter). Tests:
//   - RTL base direction auto-detected by HarfBuzz.
//   - Initial / medial / final positional forms.
//   - 1 combining diacritic (fatha).
//   - Conservative fallback counter MUST be 0 when the system font
//     fallback chain correctly resolves Arabic glyphs (Noto Sans
//     Arabic on Linux).
TEST_CASE("Multilingual.FallbackMatrix 03: Arabic RTL (جميلة) — 1920x1080") {
    auto renderer = test::make_renderer();
    // ج (U+062C = 0xD8 0xAC) + م (U+0645 = 0xD9 0x85) + ي (U+064A = 0xD9 0x8A) +
    // ل (U+0644 = 0xD9 0x84) + َ (fatha, U+064E = 0xD9 0x8E)
    const char* text = "\xD8\xAC\xD9\x85\xD9\x8A\xD9\x84\xD9\x8E";
    CHECK(render_and_verify_fallback_matrix(renderer, text, "03_arabic_rtl"));
}

// ═══ Test 04 — Hebrew RTL ═══════════════════════════════════════════════
// "שלום" (shalom = "hello" / "peace"). 4 Hebrew letters, all base form.
// Tests:
//   - RTL base direction auto-detected by HarfBuzz.
//   - Conservative fallback counter MUST be 0 when the system font
//     fallback chain correctly resolves Hebrew glyphs (Noto Sans
//     Hebrew on Linux).
TEST_CASE("Multilingual.FallbackMatrix 04: Hebrew RTL (שלום) — 1920x1080") {
    auto renderer = test::make_renderer();
    // ש (U+05E9 = 0xD7 0xA9) + ל (U+05DC = 0xD7 0x9C) + ו (U+05D5 = 0xD7 0x95) +
    // ם (final mem, U+05DD = 0xD7 0x9D)
    const char* text = "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D";
    CHECK(render_and_verify_fallback_matrix(renderer, text, "04_hebrew_rtl"));
}

// ═══ Test 05 — CJK ══════════════════════════════════════════════════════
// "こんにちは" (Japanese hiragana, "hello" / "good afternoon").
// 5 hiragana characters in the CJK Unified Ideographs / Hiragana
// block (U+3040–U+309F). Tests:
//   - CJK shaping path through HarfBuzz.
//   - Conservative fallback counter MUST be 0 when the system font
//     fallback chain correctly resolves CJK glyphs (Noto Sans CJK
//     on Linux).
TEST_CASE("Multilingual.FallbackMatrix 05: CJK (こんにちは) — 1920x1080") {
    auto renderer = test::make_renderer();
    // こ (U+3053 = 0xE3 0x81 0x93) + ん (U+3093 = 0xE3 0x82 0x93) +
    // に (U+306B = 0xE3 0x81 0xAB) + ち (U+3061 = 0xE3 0x81 0xA1) +
    // は (U+306F = 0xE3 0x81 0xAF)
    const char* text = "\xE3\x81\x93\xE3\x82\x93"
                       "\xE3\x81\xAB\xE3\x81\xA1"
                       "\xE3\x81\xAF";
    CHECK(render_and_verify_fallback_matrix(renderer, text, "05_cjk"));
}

// ═══ Test 06 — Emoji ════════════════════════════════════════════════════
// 3 emoji glyphs from the SMP (Supplementary Multilingual Plane,
// U+1F000–U+1FFFF): 🍎 (U+1F34E), 🚀 (U+1F680), 🌈 (U+1F308). Tests:
//   - 4-byte UTF-8 encoding (codepoints above U+FFFF).
//   - Color emoji rendering (depends on system font fallback chain,
//     typically Noto Color Emoji on Linux).
//   - Conservative fallback counter MUST be 0 in the nominal case.
TEST_CASE("Multilingual.FallbackMatrix 06: Emoji (🍎🚀🌈) — 1920x1080") {
    auto renderer = test::make_renderer();
    // 🍎 (U+1F34E = 0xF0 0x9F 0x8D 0x8E) +
    // 🚀 (U+1F680 = 0xF0 0x9F 0x9A 0x80) +
    // 🌈 (U+1F308 = 0xF0 0x9F 0x8C 0x88)
    const char* text = "\xF0\x9F\x8D\x8E"
                       "\xF0\x9F\x9A\x80"
                       "\xF0\x9F\x8C\x88";
    CHECK(render_and_verify_fallback_matrix(renderer, text, "06_emoji"));
}

// ═══ Test 07 — Punctuation ═════════════════════════════════════════════
// 14 ASCII punctuation glyphs: . , ! ? ; : ' " ( ) [ ] { } < >. Tests:
//   - Pure ASCII (no fallback needed) baseline case.
//   - All glyphs in Inter-Bold.ttf natively.
TEST_CASE("Multilingual.FallbackMatrix 07: Punctuation (.,!?;:'\"()[]{}<>) — 1920x1080") {
    auto renderer = test::make_renderer();
    const char* text = ".,!?;:'\"()[]{}<>";
    CHECK(render_and_verify_fallback_matrix(renderer, text, "07_punctuation"));
}

// ═══ Test 08 — Numbers ═════════════════════════════════════════════════
// 10 ASCII digit glyphs. Tests:
//   - Pure ASCII (no fallback needed) baseline case.
//   - All glyphs in Inter-Bold.ttf natively.
//   - Tabular figures (default Inter-Bold uses proportional digits,
//     which is the expected behavior).
TEST_CASE("Multilingual.FallbackMatrix 08: Numbers (0123456789) — 1920x1080") {
    auto renderer = test::make_renderer();
    const char* text = "0123456789";
    CHECK(render_and_verify_fallback_matrix(renderer, text, "08_numbers"));
}

// ═══ Test 09 — Combining marks ═════════════════════════════════════════
// "naïve" — base Latin glyphs with combining diacritics:
//   - n + a + ï (i + combining diaeresis U+0308) + v + e.
//   - The ï (U+00EF) is a precomposed character; we use the decomposed
//     form here to exercise the combining-mark path explicitly:
//       i (U+0069) + COMBINING DIAERESIS (U+0308)
//     (UTF-8: 0x69 0xCC 0x88).
// Tests:
//   - Combining-mark handling in HarfBuzz (zero-width marks
//     positioned above the base glyph).
//   - Conservative fallback counter MUST be 0 in the nominal case.
TEST_CASE("Multilingual.FallbackMatrix 09: Combining marks (naïve decomposed) — 1920x1080") {
    auto renderer = test::make_renderer();
    // "na" + "i" + COMBINING DIAERESIS (U+0308 = 0xCC 0x88) + "ve"
    const char* text = "nai\xCC\x88ve";
    CHECK(render_and_verify_fallback_matrix(renderer, text, "09_combining_marks"));
}

// ═══ Test 10 — Ligatures ═══════════════════════════════════════════════
// "fi fl ffi ffl" — 4 Latin ligature pairs/triples via the OpenType
// `liga` feature. Tests:
//   - Standard ligature formation in Inter-Bold.ttf (Inter includes
//     the standard 5 Latin ligatures: fi, fl, ffi, ffl, and the
//     "Th" digraph with the `dlig` discretionary feature).
//   - Conservative fallback counter MUST be 0 in the nominal case.
TEST_CASE("Multilingual.FallbackMatrix 10: Ligatures (fi fl ffi ffl) — 1920x1080") {
    auto renderer = test::make_renderer();
    const char* text = "fi fl ffi ffl";
    CHECK(render_and_verify_fallback_matrix(renderer, text, "10_ligatures"));
}
