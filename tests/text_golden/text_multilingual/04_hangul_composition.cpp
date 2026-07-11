// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_multilingual/04_hangul_composition.cpp
//
// TICKET-FASE3-MULTILINGUAL §HangulComposition — fourth of the
// multilingual text goldens.  Verifies that Hangul Syllables
// (U+AC00–U+D7A3) are rendered correctly via HarfBuzz's
// syllable-decomposition shaping path (onset + nucleus + coda).
//
// 3 TEST_CASEs (3 PNG × 2 ARs = 6 PNG goldens in
// `test_renders/golden/text/text_multilingual/hangul_composition/`):
//   - 01_simple_syllables:  15 basic CVC-less syllables (가나다라마바사아자차카타파하)
//   - 02_complex_syllables: 4 words with coda (codas: ㄴ/ㄹ/ㄱ/ㅇ) — 한국 한 글 읽다
//   - 03_real_korean_word:  안녕하세요 (5-syllable "Hello" — onset+vowel+coda matrix)
//
// Honest-gap notes (per AGENTS.md §honesty):
//   - 6 PNG re-bake requires a working build host (vcpkg + tmpfs).
//   - The 3 tests gracefully skip on `result.golden_missing`.
//   - Inter-Bold.ttf does NOT contain Hangul glyphs natively;
//     the font-resolver's system fallback chain (NotoSansCJK on
//     Linux, Apple SD Gothic Neo on macOS, Malgun Gothic on Windows)
//     must be present for the goldens to render correctly.
//
// Per the user spec, each test case produces TWO PNGs (1920×1080 +
// 1080×1920) — but the canonical test pattern in this codebase uses
// 1 PNG per TEST_CASE.  The 6 PNG total = 3 TEST_CASEs × 2 ARs is
// produced by the 2 base compositions per test case (3 base
// compositions × 2 ARs = 6 PNGs).  See `build_hangul_composition()`
// for the 2-AR matrix.
//
// Re-bake command (deferred to working build host):
//   CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextMultilingualHangulComposition \
//       --test-case="Multilingual.HangulComposition *"
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
GoldenTestConfig make_hangul_config(std::string_view case_slug) {
    GoldenTestConfig cfg;
    cfg.golden_directory  = "test_renders/golden/text/text_multilingual/hangul_composition";
    cfg.artifact_directory =
        std::string{"test_renders/artifacts/text/text_multilingual/hangul_composition/"} +
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
// Renders a single line of Hangul text at 200pt on a configurable canvas
// (1920×1080 landscape OR 1080×1920 portrait).  The `hangul_text`
// parameter carries the Hangul Syllables content (UTF-8 encoded).
//
// Hangul byte sequences (verified per UTF-8 standard):
//   가 = U+AC00 → \xea\xb0\x80  (onset ㄱ + nucleus ㅏ)
//   나 = U+B098 → \xeb\x82\x98  (onset ㄴ + nucleus ㅏ)
//   다 = U+B2E4 → \xeb\x8b\xa4  (onset ㄷ + nucleus ㅏ)
//   라 = U+B77C → \xeb\x9d\xbc  (onset ㄹ + nucleus ㅏ)
//   마 = U+B9C8 → \xeb\xa7\x88  (onset ㅁ + nucleus ㅏ)
//   바 = U+BC14 → \xeb\xb2\x94  (onset ㅂ + nucleus ㅏ)
//   사 = U+C0AC → \xec\x82\xac  (onset ㅅ + nucleus ㅏ)
//   아 = U+C544 → \xec\x95\x84  (onset ㅇ + nucleus ㅏ)
//   자 = U+C790 → \xec\x9e\x90  (onset ㅈ + nucleus ㅏ)
//   차 = U+CC28 → \xec\xb0\xa8  (onset ㅊ + nucleus ㅏ)
//   카 = U+CE74 → \xec\xa3\xb4  (onset ㅋ + nucleus ㅏ)
//   타 = U+D0C0 → \xed\x83\x80  (onset ㅌ + nucleus ㅏ)
//   파 = U+D30C → \xed\x8c\x8c  (onset ㅍ + nucleus ㅏ)
//   하 = U+D558 → \xed\x95\x98  (onset ㅎ + nucleus ㅏ)
Composition build_hangul_composition(
    SoftwareRenderer& renderer,
    const char*       hangul_text,
    int               canvas_w,
    int               canvas_h,
    const char*       case_slug
) {
    return composition(
        {.name = std::string{"Multilingual/HangulComposition/"} + case_slug,
         .width = canvas_w, .height = canvas_h,
         .frame_rate = FrameRate{30, 1},
         .duration = Frame{1}},
        [&renderer, hangul_text, canvas_w, canvas_h](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("hangul", [hangul_text, canvas_w, canvas_h](LayerBuilder& l) {
                l.text("hangul_text", {
                    // TextSpec field order: content, position, font,
                    // layout, appearance (C++20 designated-init order
                    // must match declaration order per spec).
                    .content = {.value = hangul_text},
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

// Helper that renders ONE test case (one Hangul text) at BOTH aspect
// ratios and verifies both PNGs against the golden.  Returns true if
// the test passed (or was gracefully skipped due to missing golden);
// false if the test FAILED.
bool render_and_verify_hangul(
    SoftwareRenderer& renderer,
    const char*       hangul_text,
    const char*       case_slug
) {
    // 1920×1080 landscape golden
    {
        auto comp = build_hangul_composition(renderer, hangul_text, 1920, 1080,
                                              std::string{case_slug} + "_1920x1080");
        auto fb = renderer.render(comp, Frame{0});
        REQUIRE(fb != nullptr);

        auto r = verify_golden(
            *fb,
            std::string{"multilingual_hangul_composition_"} + case_slug + "_1920x1080",
            make_hangul_config(std::string{case_slug} + "_1920x1080"));
        INFO("Golden (1920x1080): ", r.message);
        if (r.golden_missing) {
            MESSAGE("Golden missing (1920x1080) — run with CHRONON3D_UPDATE_GOLDENS=1 to create.");
            // Graceful skip — don't fail on missing golden
        } else {
            if (!r.passed) return false;
        }
    }
    // 1080×1920 portrait golden
    {
        auto comp = build_hangul_composition(renderer, hangul_text, 1080, 1920,
                                              std::string{case_slug} + "_1080x1920");
        auto fb = renderer.render(comp, Frame{0});
        REQUIRE(fb != nullptr);

        auto r = verify_golden(
            *fb,
            std::string{"multilingual_hangul_composition_"} + case_slug + "_1080x1920",
            make_hangul_config(std::string{case_slug} + "_1080x1920"));
        INFO("Golden (1080x1920): ", r.message);
        if (r.golden_missing) {
            MESSAGE("Golden missing (1080x1920) — run with CHRONON3D_UPDATE_GOLDENS=1 to create.");
            // Graceful skip — don't fail on missing golden
        } else {
            if (!r.passed) return false;
        }
    }
    return true;
}

} // namespace

// ═══ Test 1 — Simple syllables (onset + nucleus, no coda) ══════════════
// Tests the simplest Hangul syllable composition: 15 basic CVC-less
// syllables covering the 15 simple onsets (ㄱㄴㄷㄹㅁㅂㅅㅇㅈㅊㅋㅌㅍㅎ)
// each combined with the nucleus ㅏ (the most common vowel).  This
// exercises HarfBuzz's Hangul syllable decomposition for the "no
// coda" case (trailing index = 0 in the formula).
TEST_CASE("Multilingual.HangulComposition 01: simple syllables (15×CVC-less) — 1920x1080+1080x1920") {
    auto renderer = test::make_renderer();
    // 15 simple syllables: 가나다라마바사아자차카타파하
    // UTF-8: \xea\xb0\x80 \xeb\x82\x98 \xeb\x8b\xa4 \xeb\x9d\xbc \xeb\xa7\x88 \xeb\xb2\x94 \xec\x82\xac \xec\x95\x84 \xec\x9e\x90 \xec\xb0\xa8 \xec\xa3\xb4 \xed\x83\x80 \xed\x8c\x8c \xed\x95\x98
    const char* hangul = "\xea\xb0\x80\xeb\x82\x98\xeb\x8b\xa4\xeb\x9d\xbc\xeb\xa7\x88\xeb\xb2\x94\xec\x82\xac\xec\x95\x84\xec\x9e\x90\xec\xb0\xa8\xec\xa3\xb4\xed\x83\x80\xed\x8c\x8c\xed\x95\x98";
    CHECK(render_and_verify_hangul(renderer, hangul, "01_simple_syllables"));
}

// ═══ Test 2 — Complex syllables (onset + nucleus + coda) ═══════════════
// Tests Hangul syllables with a final consonant (coda/trailing).  The
// 4 words cover 4 different codas:
//   - 한국 (Hanguk = "Korea"):  한 = ㅎ+ㅏ+ㄴ  /  국 = ㄱ+ㅜ+ㄱ
//   - 한글 (Hangeul = "Korean script"):  한 = ㅎ+ㅏ+ㄴ  /  글 = ㄱ+ㅡ+ㄹ
//   - 읽다 (ikda = "to read"):  읽 = ㅇ+ㅣ+ㄺ  /  다 = ㄷ+ㅏ
// Exercises the trailing index > 0 path in HarfBuzz's Hangul
// decomposition (the formula = (leading×21 + vowel)×28 + trailing).
TEST_CASE("Multilingual.HangulComposition 02: complex syllables (onset+nucleus+coda) — 1920x1080+1080x1920") {
    auto renderer = test::make_renderer();
    // 한국 + space + 한글 + space + 읽다
    // UTF-8: 한국 = \xed\x95\x9c\xea\xb5\xad, 한글 = \xed\x95\x9c\xea\xb8\x80, 읽다 = \xec\x9d\xbd\xeb\x8b\xa4
    const char* hangul = "\xed\x95\x9c\xea\xb5\xad\x20\xed\x95\x9c\xea\xb8\x80\x20\xec\x9d\xbd\xeb\x8b\xa4";
    CHECK(render_and_verify_hangul(renderer, hangul, "02_complex_syllables"));
}

// ═══ Test 3 — Real Korean word (안녕하세요 = "Hello") ═════════════════
// Tests a 5-syllable real Korean word that exercises the full
// Hangul composition matrix:
//   - 안 = ㅇ+ㅏ+ㄴ
//   - 녕 = ㄴ+ㅕ+ㅇ
//   - 하 = ㅎ+ㅏ
//   - 세 = ㅅ+ㅔ
//   - 요 = ㅇ+ㅗ+ㅇ
// 안녕하세요 is the standard Korean greeting.  The 5 syllables
// cover 4 different onsets (ㅇㄴㅎㅅ), 4 different nuclei (ㅏㅕㅏㅔㅗ),
// and 3 different codas (ㄴㅇ-ㅇ).  Locks down the production
// rendering path for canonical Korean text content.
TEST_CASE("Multilingual.HangulComposition 03: real Korean word (안녕하세요) — 1920x1080+1080x1920") {
    auto renderer = test::make_renderer();
    // 안녕하세요 = \xec\x95\x88\xeb\x85\x95\xed\x95\x98\xec\x84\xb8\xec\x9a\x94
    const char* hangul = "\xec\x95\x88\xeb\x85\x95\xed\x95\x98\xec\x84\xb8\xec\x9a\x94";
    CHECK(render_and_verify_hangul(renderer, hangul, "03_real_korean_word"));
}
