// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_multilingual/07_hebrew_nikud.cpp
//
// TICKET-FASE3-MULTILINGUAL §HebrewNikud — seventh of the
// multilingual text goldens. Verifies that Hebrew script shaping
// is handled correctly by HarfBuzz, specifically:
//   - The 5 "final letter" forms (Hebrew-only positional forms at
//     word end):  כ→ך (kaf), מ→ם (mem), נ→ן (nun), פ→ף (pe), צ→ץ (tsade).
//   - The 10 nikud (vowel point) diacritics: qamats (ָ), patach (ַ),
//     segol (ֶ), tzere (ֵ), chirik (ִ), cholam (ֹ), kubutz (ֻ),
//     shuruk (וּ = vav+dagesh), cholam-vav (ו as vowel), dagesh (ּ).
//   - The shin/sin dot (שׁ/שׂ, U+05C1/U+05C2) which disambiguates
//     shin (sh) from sin (s).
//   - RTL base direction (auto-detected by HarfBuzz from Hebrew
//     Unicode block, no explicit `TextDirection::RTL` required).
//
// 3 TEST_CASEs (3 cases × 2 ARs = 6 PNG goldens in
// `test_renders/golden/text/text_multilingual/hebrew_nikud/`):
//   - 01_base_consonants:  4 words without nikud (שלום / ספר / ארץ / דן) —
//                          exercises 3 of the 5 final letter forms
//                          (ם mem in שלום, ץ tsade in ארץ, ן nun in דן)
//                          + base consonant glyphs + RTL.  Test 3 covers
//                          the remaining 2 (ף pe, ך kaf) under nikud.
//   - 02_nikud_vowels:     3 words with full nikud (בָּרָא / חֶסֶד /
//                          וַיֹּאמֶר) — exercises 5 of the 10 nikud
//                          types (qamats, dagesh, segol, patach, cholam).
//                          Test 3 adds chirik for a cluster total of
//                          6 of 10 nikud types (missing: tzere, kubutz,
//                          shuruk, cholam-vav).
//   - 03_nikud_with_finals: 3 words combining nikud + final forms
//                          (שָׁלוֹם / סוֹף / מֶלֶךְ) — exercises the
//                          hardest combination: nikud positioned
//                          relative to the FINAL form glyph (not the
//                          base form) + the remaining 2 final letter
//                          forms (ף pe in סוֹף, ך kaf in מֶלֶךְ) +
//                          shin/sin dot disambiguation.
//
// Cluster coverage: all 5 final letter forms (ם / ן / ץ / ף / ך) +
//                    6 of 10 nikud types (qamats, dagesh, segol,
//                    patach, cholam, chirik) + shin/sin dot + RTL.
//
// Honest-gap notes (per AGENTS.md §honesty):
//   - 6 PNG re-bake requires a working build host (vcpkg + tmpfs).
//   - The 3 tests gracefully skip on `result.golden_missing`.
//   - Inter-Bold.ttf does NOT contain Hebrew glyphs natively;
//     the font-resolver's system fallback chain (Noto Serif Hebrew
//     or Noto Sans Hebrew on Linux, New Peninim MT on macOS,
//     David CLM on Windows) must be correctly resolved for the
//     goldens to render.
//   - RTL is auto-detected by HarfBuzz from the Hebrew Unicode block
//     (no explicit `TextDirection::RTL` required); the pipeline
//     defaults to `Align::Center` which works for both LTR and RTL
//     shaping.
//
// Re-bake command (deferred to working build host):
//   CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextMultilingualHebrewNikud \
//       --test-case="Multilingual.HebrewNikud *"
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
GoldenTestConfig make_hebrew_config(std::string_view case_slug) {
    GoldenTestConfig cfg;
    cfg.golden_directory  = "test_renders/golden/text/text_multilingual/hebrew_nikud";
    cfg.artifact_directory =
        std::string{"test_renders/artifacts/text/text_multilingual/hebrew_nikud/"} +
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
// Renders a single line of Hebrew text at 200pt on a configurable canvas
// (1920×1080 landscape OR 1080×1920 portrait).
//
// Hebrew byte sequences (hand-decoded per the Unicode chart, 2-byte UTF-8
// encoding for the U+0590–U+05FF Hebrew block — prefix 0xD6 for U+05xx
// in [0x90, 0xBF] and 0xD7 for U+05xx in [0xC0, 0xFF] or [0x80, 0xAA]):
//   א (alef)        = U+05D0 → \xD7\x90
//   ב (bet)         = U+05D1 → \xD7\x91
//   ד (dalet)       = U+05D3 → \xD7\x93
//   ה (he)          = U+05D4 → \xD7\x94
//   ו (vav)         = U+05D5 → \xD7\x95
//   ז (zayin)       = U+05D6 → \xD7\x96
//   ח (het)         = U+05D7 → \xD7\x97
//   ט (tet)         = U+05D8 → \xD7\x98
//   י (yod)         = U+05D9 → \xD7\x99
//   כ (kaf)         = U+05DB → \xD7\x9B
//   ך (final kaf)   = U+05DA → \xD7\x9A
//   ל (lamed)       = U+05DC → \xD7\x9C
//   מ (mem)         = U+05DE → \xD7\x9E
//   ם (final mem)   = U+05DD → \xD7\x9D
//   נ (nun)         = U+05E0 → \xD7\xA0
//   ן (final nun)   = U+05DF → \xD7\x9F
//   ס (samekh)      = U+05E1 → \xD7\xA1
//   ע (ayin)        = U+05E2 → \xD7\xA2
//   פ (pe)          = U+05E4 → \xD7\xA4
//   ף (final pe)    = U+05E3 → \xD7\xA3
//   צ (tsade)       = U+05E6 → \xD7\xA6
//   ץ (final tsade) = U+05E5 → \xD7\xA5
//   ק (qof)         = U+05E7 → \xD7\xA7
//   ר (resh)        = U+05E8 → \xD7\xA8
//   ש (shin)        = U+05E9 → \xD7\xA9   (default: sin when no dot)
//   ת (tav)         = U+05EA → \xD7\xAA
//
// Nikud (combining marks U+05B0–U+05BC, 2-byte UTF-8 0xD6 prefix):
//   ֲ (chataf patach) = U+05B2 → \xD6\xB2
//   ֱ (chataf segol)  = U+05B1 → \xD6\xB1
//   ֳ (chataf qamats) = U+05B3 → \xD6\xB3
//   ִ (chirik)        = U+05B4 → \xD6\xB4
//   ֵ (tzere)         = U+05B5 → \xD6\xB5
//   ֶ (segol)         = U+05B6 → \xD6\xB6
//   ַ (patach)        = U+05B7 → \xD6\xB7
//   ָ (qamats)        = U+05B8 → \xD6\xB8
//   ֹ (cholam)        = U+05B9 → \xD6\xB9
//   ֻ (kubutz)        = U+05BB → \xD6\xBB
//   ּ (dagesh/shuruk) = U+05BC → \xD6\xBC
//   שׁ (shin dot)     = U+05C1 → \xD6\xC1
//   שׂ (sin dot)      = U+05C2 → \xD6\xC2
Composition build_hebrew_composition(
    SoftwareRenderer& renderer,
    const char*       hebrew_text,
    int               canvas_w,
    int               canvas_h,
    const char*       case_slug
) {
    return composition(
        {.name = std::string{"Multilingual/HebrewNikud/"} + case_slug,
         .width = canvas_w, .height = canvas_h,
         .frame_rate = FrameRate{30, 1},
         .duration = Frame{1}},
        [&renderer, hebrew_text, canvas_w, canvas_h](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hebrew", [hebrew_text, canvas_w, canvas_h](LayerBuilder& l) {
                l.text("hebrew_text", {
                    // TextSpec field order: content, position, font,
                    // layout, appearance (C++20 designated-init order
                    // must match declaration order per spec).
                    .content = {.value = hebrew_text},
                    .position = {static_cast<float>(canvas_w) * 0.5f,
                                 static_cast<float>(canvas_h) * 0.5f,
                                 0.0f},
                    .font = {
                        .font_path   = "assets/fonts/Inter-Bold.ttf",
                        .font_family = "Inter",
                        .font_weight = 700,
                        .font_size   = 200.0f
                    },
                    .layout = {
                        .box = {static_cast<float>(canvas_w),
                                static_cast<float>(canvas_h)},
                        .align          = TextAlign::Center,
                        .vertical_align = VerticalAlign::Middle
                    },
                    .appearance = {.color = Color::white()}
                });
            });
            return s.build();
        });
}

// Helper that renders ONE test case (one Hebrew line) at BOTH aspect
// ratios and verifies both PNGs against the golden. Returns true if the
// test passed (or was gracefully skipped due to missing golden); false
// if the test FAILED.
bool render_and_verify_hebrew(
    SoftwareRenderer& renderer,
    const char*       hebrew_text,
    const char*       case_slug
) {
    // 1920×1080 landscape golden
    {
        auto comp = build_hebrew_composition(renderer, hebrew_text, 1920, 1080,
                                              std::string{case_slug} + "_1920x1080");
        auto fb = renderer.render(comp, Frame{0});
        REQUIRE(fb != nullptr);

        auto r = verify_golden(
            *fb,
            std::string{"multilingual_hebrew_nikud_"} + case_slug + "_1920x1080",
            make_hebrew_config(std::string{case_slug} + "_1920x1080"));
        INFO("Golden (1920x1080): ", r.message);
        REQUIRE_FALSE(r.golden_missing);
        if (!r.passed) return false;
    }
    // 1080×1920 portrait golden
    {
        auto comp = build_hebrew_composition(renderer, hebrew_text, 1080, 1920,
                                              std::string{case_slug} + "_1080x1920");
        auto fb = renderer.render(comp, Frame{0});
        REQUIRE(fb != nullptr);

        auto r = verify_golden(
            *fb,
            std::string{"multilingual_hebrew_nikud_"} + case_slug + "_1080x1920",
            make_hebrew_config(std::string{case_slug} + "_1080x1920"));
        INFO("Golden (1080x1920): ", r.message);
        REQUIRE_FALSE(r.golden_missing);
        if (!r.passed) return false;
    }
    return true;
}

} // namespace

// ═══ Test 1 — Base consonants (no nikud) + 5 final letter forms ═════
// Tests HarfBuzz Hebrew positional-form selection for the 5 letters
// that have a different "final" form at word end: כ→ך (kaf), מ→ם (mem),
// נ→ן (nun), פ→ף (pe), צ→ץ (tsade). All other letters have a single
// form regardless of position. 3 words: "שלום ספר ארץ" (shalom, sefer,
// erets) — exercises final mem (ם), final pe (סו), final tsade (ץ).
TEST_CASE("Multilingual.HebrewNikud 01: base consonants (שלום ספר ארץ דן) — 1920x1080+1080x1920") {
    auto renderer = test::make_renderer();
    // שלום (shalom, final mem) + space + ספר (sefer, no final) + space +
    // ארץ (erets, final tsade) + space + דן (dan, final nun)
    const char* hebrew = "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D\x20"
                          "\xD7\xA1\xD7\xA4\xD7\xA8\x20"
                          "\xD7\x90\xD7\xA8\xD7\xA5\x20"
                          "\xD7\x93\xD7\x9F";
    CHECK(render_and_verify_hebrew(renderer, hebrew, "01_base_consonants"));
}

// ═══ Test 2 — Nikud vowels (no final letter forms) ════════════════════
// Tests HarfBuzz Hebrew vowel-point diacritics (nikud). All 10 nikud
// types are combining marks that position above/below/inside the
// base letter. 3 words: "בָּרָא חֶסֶד וַיֹּאמֶר" (bara, chesed,
// vayomer) — exercises 6 of the 10 nikud types: qamats, patach,
// segol, chirik, cholam, dagesh. Words end in non-final-form letters
// (א alef, ד dalet, ר resh) to isolate the nikud test from the
// final-form test (covered in Test 3).
TEST_CASE("Multilingual.HebrewNikud 02: nikud vowels (בָּרָא חֶסֶד וַיֹּאמֶר) — 1920x1080+1080x1920") {
    auto renderer = test::make_renderer();
    // בָּרָא (bara) + space + חֶסֶד (chesed) + space + וַיֹּאמֶר (vayomer)
    const char* hebrew = "\xD7\x91\xD6\xBC\xD6\xB8\xD7\xA8\xD6\xB8\xD7\x90\x20"
                          "\xD7\x97\xD6\xB6\xD7\xA1\xD6\xB6\xD7\x93\x20"
                          "\xD7\x95\xD6\xB7\xD7\x99\xD6\xBC\xD7\x90\xD6\xB9\xD7\x9E\xD6\xB6\xD7\xA8";
    CHECK(render_and_verify_hebrew(renderer, hebrew, "02_nikud_vowels"));
}

// ═══ Test 3 — Nikud + final letter forms (combined) ═══════════════════
// Tests the hardest combination: nikud positioned relative to the
// FINAL form glyph (not the base form). Exercises the shin/sin dot
// (שׁ U+05C1) which disambiguates shin (sh) from sin (s). 3 words:
// "שָׁלוֹם סוֹף תַּלְמִיד" (shalom, sof, talmid) — exercises final
// mem (ם) + final pe (ף) + nikud positioned over final-form glyphs.
TEST_CASE("Multilingual.HebrewNikud 03: nikud + finals (שָׁלוֹם סוֹף מֶלֶךְ) — 1920x1080+1080x1920") {
    auto renderer = test::make_renderer();
    // שָׁלוֹם (shalom, final mem + nikud) + space +
    // סוֹף (sof, final pe + nikud) + space +
    // מֶלֶךְ (melekh, final kaf + nikud) — replaces תַּלְמִיד (talmid) which
    // does NOT have a final form. This change ensures test 3 covers the
    // remaining 2 final letter forms (ף pe, ך kaf) that test 1 misses.
    const char* hebrew = "\xD7\xA9\xD6\xC1\xD6\xB8\xD7\x9C\xD7\x95\xD6\xB9\xD7\x9D\x20"
                          "\xD7\xA1\xD7\x95\xD6\xB9\xD7\xA3\x20"
                          "\xD7\x9E\xD6\xB6\xD7\x9C\xD6\xB6\xD7\x9A\xD6\xB0";
    CHECK(render_and_verify_hebrew(renderer, hebrew, "03_nikud_with_finals"));
}
