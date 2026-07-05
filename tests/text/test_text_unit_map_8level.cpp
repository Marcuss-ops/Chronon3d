// ═══════════════════════════════════════════════════════════════════════════
// tests/text/test_text_unit_map_8level.cpp — TEXT-UNM-01 acceptance tests
// ═══════════════════════════════════════════════════════════════════════════
//
// Verifies the 8-level canonical TextUnitMap defined in
// include/chronon3d/text/text_unit_map.hpp + src/text/text_unit_map.cpp.
//
// Bit-exact deterministic — each test builds a fresh TextUnitMap from
// literal UTF-8 + literal HB-style placed clusters; no engine deps.
//
// Anti-duplication note: each test is small enough to read top-to-bottom
// without depending on any helper-framework abstraction.  We avoid
// fakers/canned builders so the test stays in lock-step with what
// TextUnitMap's contract is, rather than a derived view.

#include <chronon3d/text/text_unit_map.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using namespace chronon3d;
using std::pair;
using std::string_view;
using std::vector;

// ── Helpers (inline-by-need, not exported) ─────────────────────────────

namespace {

/// Build a PlacedGlyphRun from per-cluster (byte_offset, byte_len) pairs.
/// `advance_per_glyph` is the horizontal advance for each glyph (px).
PlacedGlyphRun make_placed(vector<u32> byte_offsets,
                           vector<u32> byte_lens,
                           f32 advance_per_glyph = 10.0f) {
    PlacedGlyphRun run;
    run.font_size = 16.0f;
    run.ascent = 12.0f;
    run.descent = 4.0f;
    run.baseline = 12.0f;
    run.total_height = 16.0f;

    for (size_t i = 0; i < byte_offsets.size(); ++i) {
        PlacedGlyph g;
        g.glyph_id = static_cast<u32>(i + 1);
        g.cluster = static_cast<u32>(i);
        g.is_cluster_start = true;
        g.advance_x = advance_per_glyph;
        g.x = static_cast<f32>(i) * advance_per_glyph;
        g.byte_offset = byte_offsets[i];
        g.byte_len = byte_lens[i];
        run.glyphs.push_back(g);

        PlacedGlyphRun::Cluster cl;
        cl.start_glyph = static_cast<u32>(i);
        cl.end_glyph = static_cast<u32>(i + 1);
        cl.byte_offset = byte_offsets[i];
        cl.byte_len = byte_lens[i];
        cl.advance = advance_per_glyph;
        cl.raw_advance = advance_per_glyph;
        run.clusters.push_back(cl);
    }
    run.total_width = static_cast<f32>(byte_offsets.size()) * advance_per_glyph;
    return run;
}

/// Walk UTF-8 byte-by-byte returning byte-length of each codepoint.
vector<u32> utf8_codepoint_byte_lengths(string_view s) {
    vector<u32> lens;
    u32 i = 0;
    while (i < s.size()) {
        const unsigned char lead = static_cast<unsigned char>(s[i]);
        u32 len = 1;
        if ((lead & 0x80u) == 0)      len = 1;
        else if ((lead & 0xE0u) == 0xC0u) len = 2;
        else if ((lead & 0xF0u) == 0xE0u) len = 3;
        else if ((lead & 0xF8u) == 0xF0u) len = 4;
        if (i + len > s.size()) len = 1;
        lens.push_back(len);
        i += len;
    }
    return lens;
}

/// Build byte_offsets/byte_lens vectors from a UTF-8 string, assuming
/// one glyph (HB cluster) per codepoint.  The simplifed TEXT-UNM-01 model
/// maps grapheme→glyph via cluster.byte_offset → first codepoint of the
/// grapheme.
pair<vector<u32>, vector<u32>>
one_cluster_per_codepoint(string_view s) {
    auto lens = utf8_codepoint_byte_lengths(s);
    vector<u32> offsets(lens.size(), 0);
    vector<u32> blens(lens.size(), 0);
    u32 cursor = 0;
    for (size_t i = 0; i < lens.size(); ++i) {
        offsets[i] = cursor;
        blens[i] = lens[i];
        cursor += lens[i];
    }
    return {offsets, blens};
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// TEST CASE (a) — ZWJ emoji cluster 👨‍👩‍👧
//
// Family emoji = 7 codepoints (3 extpict + 2 ZWJ + 2 extpict = 5 visible
// emoji + 2 ZWJ), 1 grapheme cluster under GB11.
TEST_CASE("TextUnitMap_8lvl (a) ZWJ family emoji 👨‍👩‍👧 = 1 grapheme") {
    // 👨‍👩‍👧 = \xF0\x9F\x91\xA8 \xE2\x80\x8D \xF0\x9F\x91\xA9 \xE2\x80\x8D
    //          \xF0\x9F\x91\xA7
    const string_view family =
        "\xF0\x9F\x91\xA8" "\xE2\x80\x8D"
        "\xF0\x9F\x91\xA9" "\xE2\x80\x8D"
        "\xF0\x9F\x91\xA7";
    auto [offs, lens] = one_cluster_per_codepoint(family);
    // Family: 5 codepoints (3 emoji + 2 ZWJ).
    CHECK(offs.size() == 5u);
    const PlacedGlyphRun placed = make_placed(offs, lens);

    const TextUnitMap map(family, placed);

    CHECK(map.byte_count()      == family.size());          // 13 bytes
    CHECK(map.codepoint_count() == 5u);
    // GB11 merges ZWJ+extpict into one grapheme cluster.
    // Expected: 1 grapheme (entire family), then leg-pos = 0.
    CHECK(map.grapheme_count()  == 1u);
    CHECK(map.grapheme_to_glyph(0).value_or(UINT32_MAX) == 0u);
}

// ═══════════════════════════════════════════════════════════════════════════
// TEST CASE (b) — grapheme cluster UAX#29: "café" (c+a+f+e_pre+e_acute →
// 4 graphemes? — actually "café" U+00E9 is single precomposed codepoint,
// 4 codepoints, 4 graphemes.  Use base + combining acute instead.)
TEST_CASE("TextUnitMap_8lvl (b) grapheme cluster cafe + combining acute") {
    // cafe with U+0065 + U+0301 (combining acute) = 5 codepoints, 4 graphemes.
    const string_view cafe =
        "c" "a" "f" "e" "\xCC\x81";
    auto [offs, lens] = one_cluster_per_codepoint(cafe);
    CHECK(offs.size() == 5u);
    const PlacedGlyphRun placed = make_placed(offs, lens);

    const TextUnitMap map(cafe, placed);

    CHECK(map.byte_count()      == cafe.size());
    CHECK(map.codepoint_count() == 5u);
    // GB9 attaches combining marks to previous grapheme → 4 graphemes.
    CHECK(map.grapheme_count()  == 4u);

    // First 3 graphemes are ASCII letters; 4th absorbs the combining acute.
    CHECK(map.grapheme_to_glyph(0).value() == 0u);
    CHECK(map.grapheme_to_glyph(1).value() == 1u);
    CHECK(map.grapheme_to_glyph(2).value() == 2u);
    CHECK(map.grapheme_to_glyph(3).value() == 3u);
}

// ═══════════════════════════════════════════════════════════════════════════
// TEST CASE (c) — shaped-glyph mapping under ligature.
//
// "fi" ligature: 2 codepoints → 1 grapheme (ASCII letters don't merge, but
// HarfBuzz produces 1 cluster for the ligature).  We simulate by giving
// a placed run with cluster 0 covering bytes [0,2) and no second cluster.
TEST_CASE("TextUnitMap_8lvl (c) shaped-glyph ligature (1 glyph, 2 codepoints)") {
    const string_view s = "fi";
    PlacedGlyphRun placed;
    placed.font_size = 16.0f;
    placed.ascent = 12.0f;
    placed.descent = 4.0f;
    placed.baseline = 12.0f;
    placed.total_height = 16.0f;
    placed.total_width = 10.0f;

    // 1 glyph, cluster covers both bytes (the "fi" ligature).
    PlacedGlyphRun::Cluster cl;
    cl.start_glyph = 0;
    cl.end_glyph = 1;
    cl.byte_offset = 0;
    cl.byte_len = 2;
    cl.advance = 10.0f;
    cl.raw_advance = 10.0f;
    placed.clusters.push_back(cl);

    const TextUnitMap map(s, placed);

    CHECK(map.glyph_count()       == 1u);
    CHECK(map.codepoint_count()   == 2u);
    CHECK(map.grapheme_count()    == 2u);
    // Both graphemes map to the single ligature glyph (ligature covers 2 cps,
    // but in our simplified grapheme-to-glyph model the first codepoint of
    // the FIRST grapheme maps to its cluster; subsequent graphemes that
    // share the same ligature cluster do NOT have a separate glyph index
    // — they appear as InvalidIndex on the forward map and the inverse
    // glyph_to_grapheme walks them under the prior grapheme's span).
    CHECK(map.grapheme_to_glyph(0).value_or(UINT32_MAX) == 0u);
    // Grapheme 1 shares the ligature glyph with grapheme 0; the
    // single-glyph model records only the first-encountered glyph per
    // grapheme; subsequent graphemes in the same HarfBuzz cluster map
    // via the inverse lookup under grapheme 0.
    auto g1_glyph = map.grapheme_to_glyph(1);
    CHECK(g1_glyph.value_or(InvalidIndex) == InvalidIndex);
}

// ═══════════════════════════════════════════════════════════════════════════
// TEST CASE (d) — WB on space + punct.
//
// "Hello, world!" = 2 words, 1 line, 0 paragraphs (no paragraph layout).
TEST_CASE("TextUnitMap_8lvl (d) UAX#29 word boundary on spaces + punct") {
    const string_view s = "Hello, world!";
    auto [offs, lens] = one_cluster_per_codepoint(s);
    CHECK(offs.size() == s.size());  // all ASCII
    const PlacedGlyphRun placed = make_placed(offs, lens);

    // No paragraph layout → 1 line, 1 paragraph.
    TextUnitMap map(s, placed);

    CHECK(map.glyph_count()   == 13u);
    CHECK(map.word_count()    == 2u);    // "Hello," and "world!"
    CHECK(map.line_count()    == 1u);
    CHECK(map.paragraph_count() == 1u);

    // glyph_to_word: 'H' (i=0) → word 0, ',' (i=5) → word 0,
    // ' ' (i=6) → word 0 (or word_count=2-1), 'w' (i=7) → word 1.
    CHECK(map.glyph_to_word(0).value_or(UINT32_MAX) == 0u);   // 'H'
    CHECK(map.glyph_to_word(7).value_or(UINT32_MAX) == 1u);   // 'w'
    CHECK(map.glyph_to_word(8).value_or(UINT32_MAX) == 1u);   // 'o'

    // Round-trip word index → byte of first glyph.
    CHECK(map.word_to_glyph(0).value_or(UINT32_MAX) == 0u);   // glyph of 'H'
    CHECK(map.word_to_glyph(1).value_or(UINT32_MAX) == 7u);   // glyph of 'w'
}

// ═══════════════════════════════════════════════════════════════════════════
// TEST CASE (e) — OOB safety on all 7 inverse lookups.
TEST_CASE("TextUnitMap_8lvl (e) OOB safety on all inverses") {
    const string_view s = "abc def";
    auto [offs, lens] = one_cluster_per_codepoint(s);
    const PlacedGlyphRun placed = make_placed(offs, lens);
    const TextUnitMap map(s, placed);

    // Below range → nullopt.
    CHECK_FALSE(map.codepoint_to_byte(0).has_value());
    CHECK_FALSE(map.grapheme_to_codepoint(0).has_value());
    CHECK_FALSE(map.glyph_to_grapheme(0).has_value());
    CHECK_FALSE(map.word_to_glyph(0).has_value());
    CHECK_FALSE(map.line_to_word(0).has_value());
    CHECK_FALSE(map.paragraph_to_line(0).has_value());
    CHECK_FALSE(map.span_to_paragraph(0).has_value());

    // Beyond range → nullopt (sentinel UINT32_MAX).
    CHECK_FALSE(map.codepoint_to_byte(1000).has_value());
    CHECK_FALSE(map.grapheme_to_codepoint(1000).has_value());
    CHECK_FALSE(map.glyph_to_grapheme(1000).has_value());
    CHECK_FALSE(map.word_to_glyph(1000).has_value());
    CHECK_FALSE(map.line_to_word(1000).has_value());
    CHECK_FALSE(map.paragraph_to_line(1000).has_value());
    CHECK_FALSE(map.span_to_paragraph(1000).has_value());

    // Forward edge OOB.
    CHECK_FALSE(map.byte_to_codepoint(s.size()).has_value());
    CHECK_FALSE(map.grapheme_to_glyph(1000).has_value());
    CHECK_FALSE(map.glyph_to_word(map.glyph_count()).has_value());
    CHECK_FALSE(map.paragraph_to_span(1000).has_value());
}

// ═══════════════════════════════════════════════════════════════════════════
// TEST CASE (f) — bit-exact determinism over 100 identical constructions.
TEST_CASE("TextUnitMap_8lvl (f) bit-exact determinism 100 iterations") {
    const string_view s = "The quick brown 🦊 (68 bytes) fx.";  // mixed ASCII
    auto [offs, lens] = one_cluster_per_codepoint(s);
    const PlacedGlyphRun placed = make_placed(offs, lens);

    TextUnitMap golden(s, placed);

    for (int iter = 0; iter < 100; ++iter) {
        TextUnitMap map(s, placed);
        CHECK(map.byte_count()      == golden.byte_count());
        CHECK(map.codepoint_count() == golden.codepoint_count());
        CHECK(map.grapheme_count()  == golden.grapheme_count());
        CHECK(map.glyph_count()     == golden.glyph_count());
        CHECK(map.word_count()      == golden.word_count());
        CHECK(map.line_count()      == golden.line_count());
        CHECK(map.paragraph_count() == golden.paragraph_count());

        // Spot-check forward + inverse lookup parity.
        for (u32 i = 0; i < map.byte_count(); ++i) {
            auto cp = map.byte_to_codepoint(i);
            REQUIRE(cp.has_value());
            auto back = map.codepoint_to_byte(*cp);
            REQUIRE(back.has_value());
            CHECK(*back == i);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// TEST CASE (g) — 8-level round-trip: byte → all-7 → byte.
TEST_CASE("TextUnitMap_8lvl (g) 8-level round-trip byte → all 7 → byte") {
    const string_view s = "Hello, mono!";  // 12 ASCII chars, no diacritics, 1 word
    auto [offs, lens] = one_cluster_per_codepoint(s);
    const PlacedGlyphRun placed = make_placed(offs, lens);
    TextUnitMap map(s, placed);

    // Pick byte 0 ('H') and verify the 8-level identity at that anchor.
    const auto id = map.identity_at_byte(0);

    CHECK(id.byte_idx      == 0u);
    CHECK(id.char_idx      == 0u);
    CHECK(id.grapheme_idx  == 0u);
    CHECK(id.glyph_idx     == 0u);
    CHECK(id.word_idx      == 0u);
    CHECK(id.line_idx      == 0u);
    CHECK(id.para_idx      == 0u);
    // span_idx defaults to InvalidIndex (no spans supplied).
    CHECK(id.span_idx      == InvalidIndex);

    // Forward then inverse: byte 0 → cp 0 → grapheme 0 → glyph 0 →
    // word 0 → line 0 → paragraph 0 → (no span).
    const u32 cp_for_byte0    = *map.byte_to_codepoint(0);
    const u32 g_for_cp0       = *map.codepoint_to_grapheme(cp_for_byte0);
    const u32 glyph_for_g0    = *map.grapheme_to_glyph(g_for_cp0);
    const u32 word_for_glyph0 = *map.glyph_to_word(glyph_for_g0);
    const u32 line_for_w0     = *map.word_to_line(word_for_glyph0);
    const u32 para_for_l0     = *map.line_to_paragraph(line_for_w0);
    CHECK(cp_for_byte0    == 0u);
    CHECK(g_for_cp0       == 0u);
    CHECK(glyph_for_g0    == 0u);
    CHECK(word_for_glyph0 == 0u);
    CHECK(line_for_w0     == 0u);
    CHECK(para_for_l0     == 0u);

    // Round-trip back to byte.
    const u32 byte_for_cp0 = *map.codepoint_to_byte(cp_for_byte0);
    CHECK(byte_for_cp0 == 0u);
}

// ═══════════════════════════════════════════════════════════════════════════
// TEST CASE (h) — empty-input edge: every count == 0, every lookup → nullopt.
TEST_CASE("TextUnitMap_8lvl (h) empty input → all 0 + all nullopt") {
    const string_view empty;
    PlacedGlyphRun empty_placed;  // size 0 clusters, size 0 glyphs
    const TextUnitMap map(empty, empty_placed);

    CHECK(map.byte_count()      == 0u);
    CHECK(map.codepoint_count() == 0u);
    CHECK(map.grapheme_count()  == 0u);
    CHECK(map.glyph_count()     == 0u);
    CHECK(map.word_count()      == 0u);
    CHECK(map.line_count()      == 0u);
    CHECK(map.paragraph_count() == 0u);
    CHECK(map.span_count()      == 0u);

    // Forward lookups → nullopt.
    CHECK_FALSE(map.byte_to_codepoint(0).has_value());
    CHECK_FALSE(map.codepoint_to_grapheme(0).has_value());
    CHECK_FALSE(map.grapheme_to_glyph(0).has_value());
    CHECK_FALSE(map.glyph_to_word(0).has_value());
    CHECK_FALSE(map.word_to_line(0).has_value());
    CHECK_FALSE(map.line_to_paragraph(0).has_value());
    CHECK_FALSE(map.paragraph_to_span(0).has_value());

    // Inverse lookups → nullopt.
    CHECK_FALSE(map.codepoint_to_byte(0).has_value());
    CHECK_FALSE(map.grapheme_to_codepoint(0).has_value());
    CHECK_FALSE(map.glyph_to_grapheme(0).has_value());
    CHECK_FALSE(map.word_to_glyph(0).has_value());
    CHECK_FALSE(map.line_to_word(0).has_value());
    CHECK_FALSE(map.paragraph_to_line(0).has_value());
    CHECK_FALSE(map.span_to_paragraph(0).has_value());

    // Range queries → 0.
    CHECK(map.byte_count_for_codepoint(0)         == 0u);
    CHECK(map.codepoint_count_for_grapheme(0)     == 0u);
    CHECK(map.glyph_count_for_grapheme(0)         == 0u);
    CHECK(map.glyph_count_for_word(0)             == 0u);
    CHECK(map.word_count_for_line(0)              == 0u);
    CHECK(map.line_count_for_paragraph(0)         == 0u);
    CHECK(map.paragraph_count_for_span(0)         == 0u);

    // Identity ladder → all InvalidIndex.
    const auto id = map.identity_at_byte(0);
    CHECK(id.byte_idx      == InvalidIndex);
    CHECK(id.char_idx      == InvalidIndex);
    CHECK(id.grapheme_idx  == InvalidIndex);
    CHECK(id.glyph_idx     == InvalidIndex);
    CHECK(id.word_idx      == InvalidIndex);
    CHECK(id.line_idx      == InvalidIndex);
    CHECK(id.para_idx      == InvalidIndex);
    CHECK(id.span_idx      == InvalidIndex);

    // Unknown span name → InvalidIndex.
    CHECK(map.span_index_by_name("nonexistent")   == InvalidIndex);
}

// =================================================================
// TEST CASE (i) -- span_index_by_name resolves SemanticSpanRef.name.
//
// Closes TEXT-UNM-01 paragraph 15 deferred item: span_index_by_name
// now reads from an OWNED span_names_[i] table populated at construction.
//
// Covers: canonical lookup (2 named spans), unknown name (returns
// InvalidIndex), empty name (anti-double-free guard), duplicate-name
// (first-wins semantics, mirrors paragraph_to_span_'s behaviour).
TEST_CASE("TextUnitMap_8lvl (i) span_index_by_name resolves SemanticSpanRef.name") {
    const string_view s = "Hello, world!";
    auto [offs, lens] = one_cluster_per_codepoint(s);
    const PlacedGlyphRun placed = make_placed(offs, lens);

    const std::vector<SemanticSpanRef> spans = {
        {"title", 0u, 5u},
        {"body",  6u, 12u},
    };
    TextUnitMap map(s, placed, {}, spans);

    CHECK(map.span_count() == 2u);
    CHECK(map.span_index_by_name("title") == 0u);
    CHECK(map.span_index_by_name("body")  == 1u);
    CHECK(map.span_index_by_name("nope")  == InvalidIndex);
    CHECK(map.span_index_by_name("")      == InvalidIndex);

    // Duplicate names: first-wins (matches paragraph_to_span_'s
    // "first span attached" semantics for the simplified 1-paragraph
    // model; future multi-paragraph attribution will keep this rule).
    const std::vector<SemanticSpanRef> dup_spans = {
        {"body", 6u,  12u},
        {"body", 12u, 13u},
    };
    TextUnitMap dup_map(s, placed, {}, dup_spans);
    CHECK(dup_map.span_index_by_name("body") == 0u);
}
