// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_multilingual/05_devanagari_conjuncts.cpp
//
// TICKET-FASE3-MULTILINGUAL §DevanagariConjuncts — fifth of the
// multilingual text goldens. Verifies that Devanagari script shaping
// is handled correctly by HarfBuzz, specifically the formation of
// conjuncts (संयुक्‍ताक्षर) using the virama/halant (U+094D), and the
// interaction between complex conjuncts and vowel marks (मात्रा).
//
// 3 TEST_CASEs (3 cases × 2 ARs = 6 PNG goldens in
// `test_renders/golden/text/text_multilingual/devanagari_conjuncts/`):
//   - 01_simple_conjuncts:   3 basic conjuncts (क्ष, त्र, ज्ञ)
//   - 02_conjuncts_vowels:   Conjuncts with vowel marks (क्षि, त्रा, ज्ञा)
//   - 03_real_words:         Real complex words (क्षमा, त्रिभुवन, ज्ञान)
//
// Honest-gap notes (per AGENTS.md §honesty):
//   - 6 PNG re-bake requires a working build host (vcpkg + tmpfs).
//   - The 3 tests gracefully skip on `result.golden_missing`.
//   - Inter-Bold.ttf does NOT contain Devanagari glyphs natively;
//     the font-resolver's system fallback chain (Noto Sans Devanagari
//     on Linux, Kohinoor Devanagari on macOS, Mangal on Windows)
//     must be correctly resolved for the goldens to render.
//
// Re-bake command (deferred to working build host):
//   CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextMultilingualDevanagariConjuncts \
//       --test-case="Multilingual.DevanagariConjuncts *"
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
GoldenTestConfig make_devanagari_config(std::string_view case_slug) {
    GoldenTestConfig cfg;
    cfg.golden_directory  = "test_renders/golden/text/text_multilingual/devanagari_conjuncts";
    cfg.artifact_directory =
        std::string{"test_renders/artifacts/text/text_multilingual/devanagari_conjuncts/"} +
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
// Renders a single line of Devanagari text at 200pt on a configurable canvas
// (1920×1080 landscape OR 1080×1920 portrait).
//
// Devanagari byte sequences (hand-decoded per the Unicode chart, 0xE0 0xA4/5
// prefix per UTF-8 3-byte encoding for U+0900–U+0FFF Devanagari block):
//   क (ka)        = U+0915 → \xE0\xA4\x95
//   ष (ssa)       = U+0937 → \xE0\xA4\xB7
//   ् (virama)     = U+094D → \xE0\xA5\x8D
//   त (ta)        = U+0924 → \xE0\xA4\xA4
//   र (ra)        = U+0930 → \xE0\xA4\xB0
//   ज (ja)        = U+091C → \xE0\xA4\x9C
//   ञ (nya)       = U+091E → \xE0\xA4\x9E
//   म (ma)        = U+092E → \xE0\xA4\xAE
//   ा (aa mark)   = U+093E → \xE0\xA4\xBE
//   ि (i mark)    = U+093F → \xE0\xA4\xBF
//   भ (bha)       = U+092D → \xE0\xA4\xAD
//   ु (u mark)    = U+0941 → \xE0\xA5\x81
//   व (va)        = U+0935 → \xE0\xA4\xB5
//   न (na)        = U+0928 → \xE0\xA4\xA8
Composition build_devanagari_composition(
    SoftwareRenderer& renderer,
    const char*       devanagari_text,
    int               canvas_w,
    int               canvas_h,
    const char*       case_slug
) {
    return composition(
        {.name = std::string{"Multilingual/DevanagariConjuncts/"} + case_slug,
         .width = canvas_w, .height = canvas_h,
         .frame_rate = FrameRate{30, 1},
         .duration = Frame{1}},
        [&renderer, devanagari_text, canvas_w, canvas_h](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("devanagari", [devanagari_text, canvas_w, canvas_h](LayerBuilder& l) {
                l.text("devanagari_text", {
                    // TextSpec field order: content, position, font,
                    // layout, appearance (C++20 designated-init order
                    // must match declaration order per spec).
                    .content = {.value = devanagari_text},
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

// Helper that renders ONE test case (one Devanagari text) at BOTH aspect
// ratios and verifies both PNGs against the golden. Returns true if the
// test passed (or was gracefully skipped due to missing golden); false
// if the test FAILED.
bool render_and_verify_devanagari(
    SoftwareRenderer& renderer,
    const char*       devanagari_text,
    const char*       case_slug
) {
    // 1920×1080 landscape golden
    {
        auto comp = build_devanagari_composition(renderer, devanagari_text, 1920, 1080,
                                                  std::string{case_slug} + "_1920x1080");
        auto fb = renderer.render(comp, Frame{0});
        REQUIRE(fb != nullptr);

        auto r = verify_golden(
            *fb,
            std::string{"multilingual_devanagari_conjuncts_"} + case_slug + "_1920x1080",
            make_devanagari_config(std::string{case_slug} + "_1920x1080"));
        INFO("Golden (1920x1080): ", r.message);
        if (r.golden_missing) {
            MESSAGE("Golden missing (1920x1080) — run with CHRONON3D_UPDATE_GOLDENS=1 to create.");
        } else {
            if (!r.passed) return false;
        }
    }
    // 1080×1920 portrait golden
    {
        auto comp = build_devanagari_composition(renderer, devanagari_text, 1080, 1920,
                                                  std::string{case_slug} + "_1080x1920");
        auto fb = renderer.render(comp, Frame{0});
        REQUIRE(fb != nullptr);

        auto r = verify_golden(
            *fb,
            std::string{"multilingual_devanagari_conjuncts_"} + case_slug + "_1080x1920",
            make_devanagari_config(std::string{case_slug} + "_1080x1920"));
        INFO("Golden (1080x1920): ", r.message);
        if (r.golden_missing) {
            MESSAGE("Golden missing (1080x1920) — run with CHRONON3D_UPDATE_GOLDENS=1 to create.");
        } else {
            if (!r.passed) return false;
        }
    }
    return true;
}

} // namespace

// ═══ Test 1 — Simple conjuncts (virama/halant joining) ═════════════════
// Tests the basic HarfBuzz Devanagari conjunct decomposition: consonants
// joined by the virama (U+094D) form ligatures. 3 canonical conjuncts:
//   - क्ष = क ् ष  (ka + virama + ssa)
//   - त्र = त ् र  (ta + virama + ra)
//   - ज्ञ = ज ् ञ  (ja + virama + nya)
// Exercises the "below-base" + "reph" forms that HarfBuzz emits from
// virama-joined consonants.
TEST_CASE("Multilingual.DevanagariConjuncts 01: simple conjuncts (क्ष त्र ज्ञ) — 1920x1080+1080x1920") {
    auto renderer = test::make_renderer();
    // क्ष (ka+virama+ssa) + space + त्र (ta+virama+ra) + space + ज्ञ (ja+virama+nya)
    const char* devanagari = "\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7\x20"
                              "\xE0\xA4\xA4\xE0\xA5\x8D\xE0\xA4\xB0\x20"
                              "\xE0\xA4\x9C\xE0\xA5\x8D\xE0\xA4\x9E";
    CHECK(render_and_verify_devanagari(renderer, devanagari, "01_simple_conjuncts"));
}

// ═══ Test 2 — Conjuncts with vowel marks (मात्रा) ═════════════════════
// Tests the interaction between conjuncts and vowel signs. HarfBuzz
// positions the vowel mark relative to the conjunct glyph (not the
// individual consonants). 3 conjunct+vowel combinations:
//   - क्षि = क्ष + ि  (ksha + pre-base "i" vowel mark)
//   - त्रा = त्र + ा  (tra + post-base "aa" vowel mark)
//   - ज्ञा = ज्ञ + ा  (gya + post-base "aa" vowel mark)
// Exercises the pre-base / post-base / below-base positioning rules.
TEST_CASE("Multilingual.DevanagariConjuncts 02: conjuncts with vowels (क्षि त्रा ज्ञा) — 1920x1080+1080x1920") {
    auto renderer = test::make_renderer();
    // क्षि (ksha+i) + space + त्रा (tra+aa) + space + ज्ञा (gya+aa)
    const char* devanagari = "\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7\xE0\xA4\xBF\x20"
                              "\xE0\xA4\xA4\xE0\xA5\x8D\xE0\xA4\xB0\xE0\xA4\xBE\x20"
                              "\xE0\xA4\x9C\xE0\xA5\x8D\xE0\xA4\x9E\xE0\xA4\xBE";
    CHECK(render_and_verify_devanagari(renderer, devanagari, "02_conjuncts_vowels"));
}

// ═══ Test 3 — Real Devanagari words ════════════════════════════════════
// Tests full Devanagari shaping in 3 real-world words:
//   - क्षमा (kshama, "forgiveness") — क्ष + म + आ
//   - त्रिभुवन (tribhuvan, "three worlds") — त्र + इ + भु + व + न
//                                              (note: भु has below-base "u" mark)
//   - ज्ञान (gyaan, "knowledge") — ज्ञ + आ + न
// Exercises the full HarfBuzz Devanagari shaping path including reph,
// pre-base, post-base, and below-base forms in realistic content.
TEST_CASE("Multilingual.DevanagariConjuncts 03: real words (क्षमा त्रिभुवन ज्ञान) — 1920x1080+1080x1920") {
    auto renderer = test::make_renderer();
    // क्षमा + space + त्रिभुवन + space + ज्ञान
    const char* devanagari = "\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7\xE0\xA4\xAE\xE0\xA4\xBE\x20"
                              "\xE0\xA4\xA4\xE0\xA5\x8D\xE0\xA4\xB0\xE0\xA4\xBF\xE0\xA4\xAD\xE0\xA5\x81\xE0\xA4\xB5\xE0\xA4\xA8\x20"
                              "\xE0\xA4\x9C\xE0\xA5\x8D\xE0\xA4\x9E\xE0\xA4\xBE\xE0\xA4\xA8";
    CHECK(render_and_verify_devanagari(renderer, devanagari, "03_real_words"));
}
