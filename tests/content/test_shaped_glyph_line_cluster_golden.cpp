// tests/content/test_shaped_glyph_line_cluster_golden.cpp
//
// Golden equivalence test for ShapedGlyphLine::layout() cluster-boundary
// logic.
//
// Captures the current O(n²) cluster-boundary scan output as a golden
// baseline. After the O(n) optimization, layout() must produce IDENTICAL
// output — the golden test verifies exact equivalence of the GlyphPos
// vectors (character string + center_x + width).
//
// Test categories (per user request):
//   1. ASCII — simple Latin "Hello World"
//   2. Accented characters — "café résumé über ñño"
//   3. Ligatures — "fi fl ffi" (OpenType fi/fl/ffi ligatures)
//   4. Arabic RTL — "مرحبا بالعالم" (NotoNaskhArabic)
//   5. Emoji — "Hi 🌍!" (mixed with Latin; emoji may produce .notdef)
//   6. Multi-cluster — combining diacritics "é" (e + U+0301)
//   7. Mixed text — "Hello مرحبا café 🌍" (Latin + Arabic + accented + emoji)
//   8. Non-zero tracking + ref_offset_x
//   9. 200-glyph stress (B02 equivalent)
//  10. Empty text — both paths agree on failure
//  11. Single glyph — one-character text
//  12. All-same-cluster — ligature where all source chars map to one glyph
//  13. Pure LTR — long Latin text
//  14. Pure RTL — long Arabic text
//
// The reference O(n²) implementation is extracted VERBATIM from the
// current ShapedGlyphLine::layout() and run on the same raw GlyphRun.
// If layout() and the reference diverge after the O(n) fix, the
// optimization introduced a regression.

#include <doctest/doctest.h>

#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

#include <tests/helpers/test_utils.hpp>

#include "content/common/text/glyph_layout.hpp"

#include <optional>
#include <string>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::test;
using chronon3d::content::text_reveal::GlyphPos;
using chronon3d::content::text_reveal::ShapedGlyphLine;

// ── Reference O(n²) implementation (verbatim from current layout()) ──────
//
// This is the EXACT algorithm currently in ShapedGlyphLine::layout().
// It produces the golden baseline output. After the O(n) optimization,
// layout() must produce identical GlyphPos vectors.
//
// The inner O(n²) loop scans ALL glyphs to find the first one whose
// cluster value is strictly greater than the current glyph's cluster.
// This determines the text substring (cluster → grapheme) for each glyph.
//
// IMPORTANT: the inner loop finds the FIRST glyph (by iteration index)
// with cluster > start, NOT the minimum cluster > start. For LTR text
// (monotonically increasing clusters) these are equivalent. For RTL
// text (reversed glyph order) they differ. The O(n) fix MUST preserve
// this exact "first by index" semantics.
static std::vector<GlyphPos> reference_layout_o_n_squared(
    const std::vector<GlyphPosition>& glyphs,
    const std::string& text,
    float tracking,
    float ref_offset_x)
{
    std::vector<GlyphPos> out;
    out.reserve(glyphs.size());

    float cursor = ref_offset_x;
    for (size_t gi = 0; gi < glyphs.size(); ++gi) {
        const auto& g = glyphs[gi];
        size_t start = g.cluster;
        size_t end = text.size();
        // Inner O(n²) cluster-boundary scan; preserved verbatim from
        // upstream ShapedGlyphLine::layout().
        for (size_t i = 0; i < glyphs.size(); ++i) {
            const auto& o = glyphs[i];
            if (o.cluster > start) { end = o.cluster; break; }
        }
        std::string ch = text.substr(start, end - start);
        if (ch.empty()) continue;
        out.push_back({ch, cursor + g.advance_x * 0.5f, g.advance_x});
        cursor += g.advance_x + tracking;
    }
    return out;
}

// ── Comparison helper: exact equality of two GlyphPos vectors ────────────
//
// Both the reference and layout() use the same float operations
// (cursor += advance_x + tracking), so center_x and width are
// bit-identical. The ch string depends on cluster boundaries, which
// the O(n) fix must preserve exactly.
static void check_layout_equivalence(
    const std::vector<GlyphPos>& actual,
    const std::vector<GlyphPos>& reference,
    const std::string& label)
{
    INFO("Test category: ", label);
    REQUIRE(actual.size() == reference.size());
    for (size_t i = 0; i < actual.size(); ++i) {
        INFO("  glyph[", i, "/", actual.size(),
             "] ch='", actual[i].ch, "' vs ref='", reference[i].ch, "'");
        CHECK(actual[i].ch == reference[i].ch);
        CHECK(actual[i].center_x == reference[i].center_x);
        CHECK(actual[i].width == reference[i].width);
    }
}

// ── Golden equivalence runner ────────────────────────────────────────────
//
// Shapes text with FontEngine to get the raw GlyphRun, runs the reference
// O(n²) layout, constructs ShapedGlyphLine and calls layout(), then
// compares. Both paths use the same shape_text parameters, so any
// divergence is a bug in the O(n) optimization.
static void run_golden_equivalence(
    FontEngine& engine,
    const std::string& text,
    const FontSpec& spec,
    float font_size,
    float tracking,
    float ref_offset_x,
    const std::string& label)
{
    // Shape the text to get the raw GlyphRun (same call as ShapedGlyphLine ctor)
    auto run_opt = engine.shape_text(text, spec, font_size);

    if (!run_opt || run_opt->glyphs.empty()) {
        // Font cannot shape this text — verify both paths agree on failure.
        // ShapedGlyphLine ctor would throw; try_shape returns nullopt.
        auto shaped = ShapedGlyphLine::try_shape(
            text, font_size, spec, tracking, ref_offset_x, engine);
        CHECK_FALSE(shaped.has_value());
        SUCCEED(label + " — font cannot shape, both paths agree on failure");
        return;
    }

    // Run reference O(n²) on the raw GlyphRun
    auto reference = reference_layout_o_n_squared(
        run_opt->glyphs, text, tracking, ref_offset_x);

    // Construct ShapedGlyphLine and call layout()
    ShapedGlyphLine line(text, font_size, spec, tracking, ref_offset_x, engine);
    auto actual = line.layout();

    // Compare — must be identical
    check_layout_equivalence(actual, reference, label);
}

// ═════════════════════════════════════════════════════════════════════════
// Test 1: ASCII — simple Latin text
// ═════════════════════════════════════════════════════════════════════════
TEST_CASE("ShapedGlyphLine cluster golden: ASCII") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec spec{"assets/fonts/Poppins-Regular.ttf", "Poppins", 400};
    run_golden_equivalence(engine, "Hello World", spec, 72.0f, 4.0f, 0.0f, "ASCII");
}

// ═════════════════════════════════════════════════════════════════════════
// Test 2: Accented characters — UTF-8 multi-byte
// ═════════════════════════════════════════════════════════════════════════
TEST_CASE("ShapedGlyphLine cluster golden: accented characters") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec spec{"assets/fonts/Poppins-Regular.ttf", "Poppins", 400};
    // café résumé über ñño — each accented char is 2 bytes in UTF-8.
    // Cluster boundaries span 2-byte regions; the O(n²) inner loop
    // correctly finds them, and the O(n) fix must preserve this.
    run_golden_equivalence(engine, "café résumé über ñño",
                           spec, 72.0f, 4.0f, 0.0f, "Accented");
}

// ═════════════════════════════════════════════════════════════════════════
// Test 3: Ligatures — fi, fl, ffi (OpenType GSUB ligature lookups)
// ═════════════════════════════════════════════════════════════════════════
TEST_CASE("ShapedGlyphLine cluster golden: ligatures") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec spec{"assets/fonts/Inter-Regular.ttf", "Inter", 400};
    // "fi fl ffi" — if the font has ligature features, HarfBuzz merges
    // 'f'+'i' into a single ligature glyph. The cluster boundary for
    // the ligature glyph spans both source characters (cluster value =
    // byte offset of 'f', end = byte offset of the next non-ligature
    // character). This is a critical test case for the O(n) fix.
    run_golden_equivalence(engine, "fi fl ffi",
                           spec, 72.0f, 4.0f, 0.0f, "Ligatures");
}

// ═════════════════════════════════════════════════════════════════════════
// Test 4: Arabic RTL — right-to-left script with contextual shaping
// ═════════════════════════════════════════════════════════════════════════
TEST_CASE("ShapedGlyphLine cluster golden: Arabic RTL") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec spec{"assets/fonts/NotoNaskhArabic-Regular.ttf", "Noto Naskh Arabic", 400};
    // "مرحبا بالعالم" = "Hello World" in Arabic.
    // HarfBuzz applies contextual shaping (initial/medial/final forms)
    // and reverses glyph order for RTL. Cluster values are byte offsets
    // into the original (logical-order) text, NOT visual order.
    //
    // CRITICAL: the inner O(n²) loop finds the FIRST glyph (by index)
    // with cluster > start. For RTL, glyphs are in reversed visual
    // order but clusters are in logical order, so the "first by index"
    // with a higher cluster is NOT the minimum higher cluster.
    // The O(n) fix must preserve this exact semantics.
    run_golden_equivalence(engine, "مرحبا بالعالم",
                           spec, 72.0f, 4.0f, 0.0f, "Arabic RTL");
}

// ═════════════════════════════════════════════════════════════════════════
// Test 5: Emoji — mixed with Latin (no emoji font in assets)
// ═════════════════════════════════════════════════════════════════════════
TEST_CASE("ShapedGlyphLine cluster golden: emoji mixed with Latin") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec spec{"assets/fonts/Inter-Regular.ttf", "Inter", 400};
    // "Hi 🌍!" — emoji U+1F30D is 4 bytes in UTF-8 (F0 9F 8C 8D).
    // Without an emoji font, HarfBuzz may produce .notdef glyphs for
    // the emoji or skip them entirely. The Latin parts should still
    // shape. The golden test captures whatever the current behavior is.
    run_golden_equivalence(engine, "Hi 🌍!",
                           spec, 72.0f, 4.0f, 0.0f, "Emoji mixed");
}

// ═════════════════════════════════════════════════════════════════════════
// Test 6: Multi-cluster — combining diacritics
// ═════════════════════════════════════════════════════════════════════════
TEST_CASE("ShapedGlyphLine cluster golden: multi-cluster combining diacritics") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec spec{"assets/fonts/Inter-Regular.ttf", "Inter", 400};
    // "e" + U+0301 (combining acute accent) = "é" decomposed.
    // HarfBuzz may merge the base + combining mark into one cluster
    // (cluster boundary spans both code points) or keep them separate
    // (each gets its own glyph with its own cluster). The golden test
    // captures the current cluster behavior for both possibilities.
    run_golden_equivalence(engine, "éáóéáó",
                           spec, 72.0f, 4.0f, 0.0f, "Multi-cluster combining");
}

// ═════════════════════════════════════════════════════════════════════════
// Test 7: Mixed text — Latin + Arabic + accented + emoji
// ═════════════════════════════════════════════════════════════════════════
TEST_CASE("ShapedGlyphLine cluster golden: mixed scripts") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    // For mixed scripts, we use Inter (Latin) — Arabic and emoji parts
    // may not shape without their respective fonts. The golden test
    // captures whatever the current behavior is for the shapeable parts.
    FontSpec spec{"assets/fonts/Inter-Regular.ttf", "Inter", 400};
    // "Hello مرحبا café 🌍" — Latin + Arabic + accented + emoji
    run_golden_equivalence(engine, "Hello مرحبا café 🌍",
                           spec, 72.0f, 4.0f, 0.0f, "Mixed scripts");
}

// ═════════════════════════════════════════════════════════════════════════
// Test 8: Non-zero tracking + ref_offset_x
// ═════════════════════════════════════════════════════════════════════════
TEST_CASE("ShapedGlyphLine cluster golden: non-zero ref_offset_x + tracking") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec spec{"assets/fonts/Poppins-Regular.ttf", "Poppins", 400};
    // Non-zero ref_offset_x and tracking — verifies that the cluster
    // boundary logic is independent of cursor/tracking math.
    run_golden_equivalence(engine, "ABCDEFG",
                           spec, 72.0f, 8.0f, 120.0f, "Tracking + offset");
}

// ═════════════════════════════════════════════════════════════════════════
// Test 9: 200-glyph stress (B02 equivalent)
// ═════════════════════════════════════════════════════════════════════════
TEST_CASE("ShapedGlyphLine cluster golden: 200-glyph stress (B02 equivalent)") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec spec{"assets/fonts/Poppins-Regular.ttf", "Poppins", 400};
    // B02 equivalent: 200 glyphs of repeating Latin text.
    // The O(n²) inner loop does 200×200 = 40,000 comparisons.
    // The O(n) fix does ~200 — linear.
    // This test verifies equivalence at scale.
    std::string text_200;
    text_200.reserve(200);
    const std::string pangram = "THEQUICKBROWNFOXJUMPSOVERTHELAZYDOG";
    while (text_200.size() < 200) text_200 += pangram;
    text_200.resize(200);

    run_golden_equivalence(engine, text_200,
                           spec, 72.0f, 4.0f, 0.0f, "200-glyph stress");
}

// ═════════════════════════════════════════════════════════════════════════
// Test 10: Empty text — both paths agree on failure
// ═════════════════════════════════════════════════════════════════════════
TEST_CASE("ShapedGlyphLine cluster golden: empty text") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec spec{"assets/fonts/Poppins-Regular.ttf", "Poppins", 400};
    // Empty text produces zero glyphs. ShapedGlyphLine ctor throws;
    // try_shape returns nullopt. The runner already handles this by
    // verifying both paths agree on the failure mode.
    run_golden_equivalence(engine, "", spec, 72.0f, 4.0f, 0.0f, "Empty text");
}

// ═════════════════════════════════════════════════════════════════════════
// Test 11: Single glyph — one-character text
// ═════════════════════════════════════════════════════════════════════════
TEST_CASE("ShapedGlyphLine cluster golden: single glyph") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec spec{"assets/fonts/Poppins-Regular.ttf", "Poppins", 400};
    // Single glyph: cluster_end is text.size(), no inner-loop match.
    // Verifies the O(n) path handles n=1 correctly for both LTR and RTL.
    run_golden_equivalence(engine, "A", spec, 72.0f, 4.0f, 0.0f, "Single glyph");
}

// ═════════════════════════════════════════════════════════════════════════
// Test 12: All-same-cluster — ligature where multiple chars map to one glyph
// ═════════════════════════════════════════════════════════════════════════
TEST_CASE("ShapedGlyphLine cluster golden: all-same-cluster ligature") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec spec{"assets/fonts/Inter-Regular.ttf", "Inter", 400};
    // "ffi" may be shaped by HarfBuzz as a single ligature glyph with
    // cluster=0 spanning all three source characters. In that case all
    // glyphs (just one) share the same cluster and cluster_end=text.size().
    // If HarfBuzz keeps them as separate glyphs, the test still verifies
    // equivalence for whatever cluster assignment HarfBuzz chooses.
    run_golden_equivalence(engine, "ffi",
                           spec, 72.0f, 4.0f, 0.0f, "All-same-cluster ligature");
}

// ═════════════════════════════════════════════════════════════════════════
// Test 13: Pure LTR — long Latin text
// ═════════════════════════════════════════════════════════════════════════
TEST_CASE("ShapedGlyphLine cluster golden: pure LTR") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec spec{"assets/fonts/Poppins-Regular.ttf", "Poppins", 400};
    // Pure LTR with many glyphs — clusters are monotonically non-decreasing.
    // The O(n) backward-pass path must match the reference exactly.
    run_golden_equivalence(engine,
                           "The quick brown fox jumps over the lazy dog. "
                           "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG.",
                           spec, 72.0f, 4.0f, 0.0f, "Pure LTR");
}

// ═════════════════════════════════════════════════════════════════════════
// Test 14: Pure RTL — long Arabic text
// ═════════════════════════════════════════════════════════════════════════
TEST_CASE("ShapedGlyphLine cluster golden: pure RTL") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec spec{"assets/fonts/NotoNaskhArabic-Regular.ttf", "Noto Naskh Arabic", 400};
    // Pure RTL Arabic — clusters are monotonically non-increasing.
    // The O(n) RTL path (first glyph holds max cluster) must match the
    // reference inner loop, which for RTL always finds the max cluster first.
    run_golden_equivalence(engine,
                           "السلام عليكم ورحمة الله وبركاته",
                           spec, 72.0f, 4.0f, 0.0f, "Pure RTL");
}
