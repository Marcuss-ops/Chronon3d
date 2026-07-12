// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_multilingual/06_arabic_shaping.cpp
//
// TICKET-FASE3-MULTILINGUAL §ArabicShaping — sixth of the
// multilingual text goldens. Verifies that Arabic script shaping
// is handled correctly by HarfBuzz, specifically the four positional
// forms (isolated / initial / medial / final) of connector and
// non-connector letters, the four mandatory lam-alef ligatures
// (لا / لأ / لإ / لآ), and the combining diacritics (harakat:
// fatha, kasra, damma, sukun, shadda, fathatan, dammatan, kasratan).
//
// 3 TEST_CASEs (3 cases × 2 ARs = 6 PNG goldens in
// `test_renders/golden/text/text_multilingual/arabic_shaping/`):
//   - 01_basic_joining:    3 words with connector + non-connector letters
//                          (جملة / كتاب / بسم — exercises initial, medial,
//                          final + non-connector final positional forms).
//   - 02_lam_alef_ligatures: 4 mandatory lam-alef ligatures
//                          (لا / لأ / لإ / لآ — exercises the four
//                          lam-alef variants + the OpenType `calt` feature).
//   - 03_diacritics_harakat: 3 fully-vocalised Arabic words
//                          (بِسْمِ / مَرْحَبًا / كُتَّابٌ — exercises
//                          fatha, kasra, damma, sukun, shadda, fathatan,
//                          dammatan combining marks + RTL base direction).
//
// Honest-gap notes (per AGENTS.md §honesty):
//   - 6 PNG re-bake requires a working build host (vcpkg + tmpfs).
//   - The 3 tests fail if the golden reference is missing (`REQUIRE_FALSE(result.golden_missing)`).
//   - Inter-Bold.ttf does NOT contain Arabic glyphs natively;
//     the font-resolver's system fallback chain (Noto Sans Arabic on
//     Linux, Geeza Pro on macOS, Arial on Windows) must be correctly
//     resolved for the goldens to render.
//   - RTL is auto-detected by HarfBuzz from the Arabic Unicode block
//     (no explicit `TextDirection::RTL` required); the pipeline
//     defaults to `Align::Center` which works for both LTR and RTL
//     shaping.
//
// Re-bake command (deferred to working build host):
//   CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextMultilingualArabicShaping \
//       --test-case="Multilingual.ArabicShaping *"
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
GoldenTestConfig make_arabic_config(std::string_view case_slug) {
    GoldenTestConfig cfg;
    cfg.golden_directory  = "test_renders/golden/text/text_multilingual/arabic_shaping";
    cfg.artifact_directory =
        std::string{"test_renders/artifacts/text/text_multilingual/arabic_shaping/"} +
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
// Renders a single line of Arabic text at 200pt on a configurable canvas
// (1920×1080 landscape OR 1080×1920 portrait).
//
// Arabic byte sequences (hand-decoded per the Unicode chart, 2-byte UTF-8
// encoding for the U+0600–U+06FF Arabic block — prefix 0xD8 for U+06xx
// in [0x00, 0x3F] and 0xD9 for U+06xx in [0x40, 0x7F]):
//   ب (ba)         = U+0628 → \xD8\xA8
//   ت (ta)         = U+062A → \xD8\xAA
//   ث (tha)        = U+062B → \xD8\xAB
//   ج (jim)        = U+062C → \xD8\xAC
//   ح (ha)         = U+062D → \xD8\xAD
//   س (sin)        = U+0633 → \xD8\xB3
//   ش (shin)       = U+0634 → \xD8\xB4
//   ر (ra)         = U+0631 → \xD8\xB1   (non-connector)
//   ا (alef)       = U+0627 → \xD8\xA7   (non-connector)
//   أ (alef+hamza↑)= U+0623 → \xD8\xA3   (non-connector)
//   إ (alef+hamza↓)= U+0625 → \xD8\xA5   (non-connector)
//   آ (alef+madda) = U+0622 → \xD8\xA2   (non-connector)
//   ك (kaf)        = U+0643 → \xD9\x83
//   ل (lam)        = U+0644 → \xD9\x84
//   م (mim)        = U+0645 → \xD9\x85
//   ه (ha)         = U+0647 → \xD9\x87
//   و (waw)        = U+0648 → \xD9\x88   (non-connector)
//   ي (ya)         = U+064A → \xD9\x8A
//   ة (ta marbuta) = U+0629 → \xD8\xA9
//   ى (alef maks.) = U+0649 → \xD9\x89
//   ً (fathatan)   = U+064B → \xD9\x8B
//   ٌ (dammatan)   = U+064C → \xD9\x8C
//   ٍ (kasratan)   = U+064D → \xD9\x8D
//   َ (fatha)      = U+064E → \xD9\x8E
//   ُ (damma)      = U+064F → \xD9\x8F
//   ِ (kasra)      = U+0650 → \xD9\x90
//   ّ (shadda)     = U+0651 → \xD9\x91
//   ْ (sukun)      = U+0652 → \xD9\x92
Composition build_arabic_composition(
    SoftwareRenderer& renderer,
    const char*       arabic_text,
    int               canvas_w,
    int               canvas_h,
    const char*       case_slug
) {
    return composition(
        {.name = std::string{"Multilingual/ArabicShaping/"} + case_slug,
         .width = canvas_w, .height = canvas_h,
         .frame_rate = FrameRate{30, 1},
         .duration = Frame{1}},
        [&renderer, arabic_text, canvas_w, canvas_h](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("arabic", [arabic_text, canvas_w, canvas_h](LayerBuilder& l) {
                l.text("arabic_text", {
                    // TextSpec field order: content, position, font,
                    // layout, appearance (C++20 designated-init order
                    // must match declaration order per spec).
                    .content = {.value = arabic_text},
                    .placement = TextPlacement{TextPlacementKind::Absolute, {static_cast<float>(canvas_w) * 0.5f,
                                 static_cast<float>(canvas_h) * 0.5f}},
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

// Helper that renders ONE test case (one Arabic line) at BOTH aspect
// ratios and verifies both PNGs against the golden. Returns true if the
// test passed (or failed via REQUIRE_FALSE if the golden reference was missing); false
// if the test FAILED.
bool render_and_verify_arabic(
    SoftwareRenderer& renderer,
    const char*       arabic_text,
    const char*       case_slug
) {
    // 1920×1080 landscape golden
    {
        auto comp = build_arabic_composition(renderer, arabic_text, 1920, 1080,
                                              std::string{case_slug} + "_1920x1080");
        auto fb = renderer.render(comp, Frame{0});
        REQUIRE(fb != nullptr);

        auto r = verify_golden(
            *fb,
            std::string{"multilingual_arabic_shaping_"} + case_slug + "_1920x1080",
            make_arabic_config(std::string{case_slug} + "_1920x1080"));
        INFO("Golden (1920x1080): ", r.message);
        REQUIRE_FALSE(r.golden_missing);
        if (!r.passed) return false;
    }
    // 1080×1920 portrait golden
    {
        auto comp = build_arabic_composition(renderer, arabic_text, 1080, 1920,
                                              std::string{case_slug} + "_1080x1920");
        auto fb = renderer.render(comp, Frame{0});
        REQUIRE(fb != nullptr);

        auto r = verify_golden(
            *fb,
            std::string{"multilingual_arabic_shaping_"} + case_slug + "_1080x1920",
            make_arabic_config(std::string{case_slug} + "_1080x1920"));
        INFO("Golden (1080x1920): ", r.message);
        REQUIRE_FALSE(r.golden_missing);
        if (!r.passed) return false;
    }
    return true;
}

} // namespace

// ═══ Test 1 — Basic joining (4 positional forms + non-connectors) ═════
// Tests HarfBuzz Arabic positional-form selection: the same letter has
// 4 different glyphs depending on its position in a word
// (isolated / initial / medial / final). Non-connector letters
// (alef, dal, dhal, ra, zayn, waw) only have 2 forms (isolated + final).
// 3 words: "جملة كتاب بسم" (jumla, kitab, bism) — exercises initial,
// medial, final + non-connector final forms.
TEST_CASE("Multilingual.ArabicShaping 01: basic joining (جملة كتاب بسم) — 1920x1080+1080x1920") {
    auto renderer = test::make_renderer();
    // جملة (jumla) + space + كتاب (kitab) + space + بسم (bism)
    const char* arabic = "\xD8\xAC\xD9\x85\xD9\x84\xD8\xA9\x20"
                          "\xD9\x83\xD8\xAA\xD8\xA7\xD8\xA8\x20"
                          "\xD8\xA8\xD8\xB3\xD9\x85";
    CHECK(render_and_verify_arabic(renderer, arabic, "01_basic_joining"));
}

// ═══ Test 2 — Lam-alef mandatory ligatures (لا / لأ / لإ / لآ) ═══════
// Tests the lam-alef mandatory OpenType ligature, the canonical
// Arabic ligature required by calligraphy. HarfBuzz emits a single
// glyph (via the `calt` feature) for the lam-alef combinations.
// 4 variants on one line: plain (لا) + hamza above (لأ) +
// hamza below (لإ) + alef madda (لآ).
TEST_CASE("Multilingual.ArabicShaping 02: lam-alef ligatures (لا لأ لإ لآ) — 1920x1080+1080x1920") {
    auto renderer = test::make_renderer();
    // لا + space + لأ + space + لإ + space + لآ
    const char* arabic = "\xD9\x84\xD8\xA7\x20"
                          "\xD9\x84\xD8\xA3\x20"
                          "\xD9\x84\xD8\xA5\x20"
                          "\xD9\x84\xD8\xA2";
    CHECK(render_and_verify_arabic(renderer, arabic, "02_lam_alef_ligatures"));
}

// ═══ Test 3 — Diacritics (harakat) + RTL rendering ══════════════════════
// Tests HarfBuzz handling of combining Arabic diacritics (harakat):
// kasra, fatha, damma, sukun, shadda, fathatan, dammatan. These are
// zero-width combining marks (Unicode General Category Mn) that
// position above/below their base letter. 3 fully-vocalised words:
//   - بِسْمِ  (bismi, "in the name [of]") — kasra + sukun + kasra
//   - مَرْحَبًا (marhaban, "welcome") — fatha + sukun + fatha + fathatan
//   - كُتَّابٌ (kuttābun, "writers") — damma + shadda + fatha + dammatan
// Exercises 7 of the 8 main Arabic diacritics + RTL base direction.
TEST_CASE("Multilingual.ArabicShaping 03: diacritics (بِسْمِ مَرْحَبًا كُتَّابٌ) — 1920x1080+1080x1920") {
    auto renderer = test::make_renderer();
    // بِسْمِ + space + مَرْحَبًا + space + كُتَّابٌ
    const char* arabic = "\xD8\xA8\xD9\x90\xD8\xB3\xD9\x92\xD9\x85\xD9\x90\x20"
                          "\xD9\x85\xD9\x8E\xD8\xB1\xD9\x92\xD8\xAD\xD9\x8E\xD8\xA8\xD9\x8B\xD8\xA7\x20"
                          "\xD9\x83\xD9\x8F\xD8\xAA\xD9\x91\xD9\x8E\xD8\xA7\xD8\xA8\xD9\x8C";
    CHECK(render_and_verify_arabic(renderer, arabic, "03_diacritics_harakat"));
}
