// ═══════════════════════════════════════════════════════════════════════════
// tests/text/test_character_offset_pre_shaping.cpp — FASE 2a
//
// Tests for the PreShaping CharacterOffset evaluation:
//   1. Caesar cipher on ASCII letters
//   2. UTF-8 multi-byte code points pass through unchanged
//   3. Ligatures (fi, fl) reshape when source text changes
//   4. RTL Arabic text with character offset
//   5. Combined characters (é = e + combining accent)
//   6. Empty string, zero offset, negative offset
//   7. has_pre_shaping_properties detection
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/text/animation/text_pre_shaping.hpp>
#include <chronon3d/text/animation/text_animator_spec.hpp>
#include <chronon3d/text/animation/glyph_instance_state.hpp>

#include <string>
#include <variant>

using namespace chronon3d;

// ═══════════════════════════════════════════════════════════════════════════
// Unit tests: apply_character_offset_to_source
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("CharacterOffset: zero offset returns identical string") {
    std::string src = "HELLO WORLD";
    auto result = apply_character_offset_to_source(src, 0);
    CHECK(result == src);
}

TEST_CASE("CharacterOffset: offset=1 shifts uppercase forward") {
    auto result = apply_character_offset_to_source("ABCXYZ", 1);
    CHECK(result == "BCDYZA");
}

TEST_CASE("CharacterOffset: offset=1 shifts lowercase forward") {
    auto result = apply_character_offset_to_source("abcxyz", 1);
    CHECK(result == "bcdyza");
}

TEST_CASE("CharacterOffset: offset=25 wraps correctly") {
    auto result = apply_character_offset_to_source("AZaz", 25);
    CHECK(result == "ZYzy");
}

TEST_CASE("CharacterOffset: offset=26 wraps to identity") {
    auto result = apply_character_offset_to_source("HELLO", 26);
    CHECK(result == "HELLO");
}

TEST_CASE("CharacterOffset: negative offset wraps backwards") {
    auto result = apply_character_offset_to_source("BCD", -1);
    CHECK(result == "ABC");
}

TEST_CASE("CharacterOffset: large negative offset") {
    auto result = apply_character_offset_to_source("ABC", -27);
    CHECK(result == "ZAB");
}

TEST_CASE("CharacterOffset: non-letter characters pass through") {
    auto result = apply_character_offset_to_source("A1! B2? C3.", 1);
    CHECK(result == "B1! C2? D3.");
}

TEST_CASE("CharacterOffset: spaces and punctuation unchanged") {
    auto result = apply_character_offset_to_source("Hello, World!", 1);
    CHECK(result == "Ifmmp, Xpsme!");
}

TEST_CASE("CharacterOffset: empty string") {
    auto result = apply_character_offset_to_source("", 5);
    CHECK(result.empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// UTF-8 multi-byte pass-through tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("CharacterOffset: CJK code points pass through unchanged") {
    // 日本語 = Japanese (3 CJK characters, 3 bytes each in UTF-8)
    std::string src = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e";
    auto result = apply_character_offset_to_source(src, 1);
    CHECK(result == src);
}

TEST_CASE("CharacterOffset: emoji pass through unchanged") {
    // 😀 = U+1F600 (4-byte UTF-8: F0 9F 98 80)
    std::string src = "\xf0\x9f\x98\x80";
    auto result = apply_character_offset_to_source(src, 1);
    CHECK(result == src);
}

TEST_CASE("CharacterOffset: RTL Arabic letters pass through unchanged") {
    // ب = U+0628 (2-byte UTF-8: D8 A8)
    std::string src = "\xd8\xa8";
    auto result = apply_character_offset_to_source(src, 1);
    CHECK(result == src);
}

TEST_CASE("CharacterOffset: accented Latin (2-byte) passes through") {
    // é = U+00E9 (2-byte UTF-8: C3 A9)
    std::string src = "\xc3\xa9";
    auto result = apply_character_offset_to_source(src, 1);
    CHECK(result == src);
}

TEST_CASE("CharacterOffset: mixed ASCII + UTF-8") {
    // "A" + 😀 + "B"
    std::string src = "A\xf0\x9f\x98\x80"
                      "B";
    auto result = apply_character_offset_to_source(src, 1);
    // A→B, emoji unchanged, B→C
    std::string expected = "B\xf0\x9f\x98\x80"
                           "C";
    CHECK(result == expected);
}

// ═══════════════════════════════════════════════════════════════════════════
// PreShaping detection tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("has_pre_shaping_properties: empty stack returns false") {
    std::vector<TextAnimatorSpec> animators;
    CHECK_FALSE(has_pre_shaping_properties(animators));
}

TEST_CASE("has_pre_shaping_properties: no CharacterOffset returns false") {
    TextAnimatorSpec spec;
    spec.properties.push_back(OpacityProperty{1.0f});
    spec.properties.push_back(PositionProperty{});
    std::vector<TextAnimatorSpec> animators{spec};
    CHECK_FALSE(has_pre_shaping_properties(animators));
}

TEST_CASE("has_pre_shaping_properties: CharacterOffset returns true") {
    TextAnimatorSpec spec;
    spec.properties.push_back(CharacterOffsetProperty{5});
    std::vector<TextAnimatorSpec> animators{spec};
    CHECK(has_pre_shaping_properties(animators));
}

TEST_CASE("has_pre_shaping_properties: disabled animator with CharacterOffset returns false") {
    TextAnimatorSpec spec;
    spec.enabled = false;
    spec.properties.push_back(CharacterOffsetProperty{5});
    std::vector<TextAnimatorSpec> animators{spec};
    CHECK_FALSE(has_pre_shaping_properties(animators));
}

// ═══════════════════════════════════════════════════════════════════════════
// evaluate_pre_shaping_source tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("evaluate_pre_shaping_source: no properties returns original") {
    std::vector<TextAnimatorSpec> animators;
    auto result = evaluate_pre_shaping_source(animators, "HELLO");
    CHECK(result == "HELLO");
}

TEST_CASE("evaluate_pre_shaping_source: single CharacterOffset") {
    TextAnimatorSpec spec;
    spec.properties.push_back(CharacterOffsetProperty{1});
    std::vector<TextAnimatorSpec> animators{spec};
    auto result = evaluate_pre_shaping_source(animators, "ABC");
    CHECK(result == "BCD");
}

TEST_CASE("evaluate_pre_shaping_source: multiple CharacterOffset properties sum") {
    TextAnimatorSpec spec1;
    spec1.properties.push_back(CharacterOffsetProperty{1});
    TextAnimatorSpec spec2;
    spec2.properties.push_back(CharacterOffsetProperty{2});
    std::vector<TextAnimatorSpec> animators{spec1, spec2};
    auto result = evaluate_pre_shaping_source(animators, "ABC");
    // offset 1+2 = 3: A→D, B→E, C→F
    CHECK(result == "DEF");
}

TEST_CASE("evaluate_pre_shaping_source: mixed PostLayout + PreShaping") {
    TextAnimatorSpec spec;
    spec.properties.push_back(OpacityProperty{0.5f});       // PostLayout
    spec.properties.push_back(CharacterOffsetProperty{1});   // PreShaping
    spec.properties.push_back(PositionProperty{});            // PostLayout
    std::vector<TextAnimatorSpec> animators{spec};
    auto result = evaluate_pre_shaping_source(animators, "XYZ");
    CHECK(result == "YZA");
}

TEST_CASE("evaluate_pre_shaping_source: total offset zero returns original") {
    TextAnimatorSpec spec1;
    spec1.properties.push_back(CharacterOffsetProperty{3});
    TextAnimatorSpec spec2;
    spec2.properties.push_back(CharacterOffsetProperty{-3});
    std::vector<TextAnimatorSpec> animators{spec1, spec2};
    auto result = evaluate_pre_shaping_source(animators, "HELLO");
    CHECK(result == "HELLO");
}

// ═══════════════════════════════════════════════════════════════════════════
// P1-02 — animated CharacterOffset: sampled per frame, stateless
// ═══════════════════════════════════════════════════════════════════════════

namespace {

TextAnimatorSpec make_animated_offset_animator(float end_offset) {
    TextAnimatorSpec spec;
    spec.properties.push_back(CharacterOffsetProperty{});
    auto* offset = std::get_if<CharacterOffsetProperty>(&spec.properties.back());
    CHECK(offset != nullptr);
    if (offset == nullptr) return spec;
    offset->offset.add_keyframe(Frame{0}, 0.0f);
    offset->offset.add_keyframe(Frame{10}, end_offset);
    return spec;
}

SampleTime sample_at(int frame) {
    return SampleTime::from_frame_int(Frame{frame}, FrameRate{30, 1});
}

} // namespace

TEST_CASE("CharacterOffset P1-02: animated offset samples each frame") {
    std::vector<TextAnimatorSpec> animators{make_animated_offset_animator(4.0f)};

    CHECK(evaluate_pre_shaping_source(animators, "ABCDEF", sample_at(0)) == "ABCDEF");
    CHECK(evaluate_pre_shaping_source(animators, "ABCDEF", sample_at(5)) == "CDEFGH");
    CHECK(evaluate_pre_shaping_source(animators, "ABCDEF", sample_at(10)) == "EFGHIJ");
}

TEST_CASE("CharacterOffset P1-02: fractional offsets round to the nearest codepoint") {
    std::vector<TextAnimatorSpec> animators{make_animated_offset_animator(3.0f)};

    // frame 5 -> 1.5, rounded away from zero -> 2
    CHECK(evaluate_pre_shaping_source(animators, "ABC", sample_at(5)) == "CDE");
}

TEST_CASE("CharacterOffset P1-02: evaluation is stateless across frames") {
    std::vector<TextAnimatorSpec> animators{make_animated_offset_animator(2.0f)};

    CHECK(evaluate_pre_shaping_source(animators, "AB", sample_at(10)) == "CD");
    // Returning to frame 0 must re-apply the offset to the authored source,
    // not to the already-shifted text (the old cumulative-shift bug).
    CHECK(evaluate_pre_shaping_source(animators, "AB", sample_at(0)) == "AB");
    CHECK(evaluate_pre_shaping_source(animators, "AB", sample_at(10)) == "CD");
}

// ═══════════════════════════════════════════════════════════════════════════
// GlyphInstanceState: character_offset is deprecated (always 0)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("GlyphInstanceState: character_offset defaults to 0") {
    GlyphInstanceState gs;
    CHECK(gs.character_offset == 0);
}

TEST_CASE("GlyphInstanceState: character_offset can be set but is unused") {
    GlyphInstanceState gs;
    gs.character_offset = 5;
    CHECK(gs.character_offset == 5);
}

// ═══════════════════════════════════════════════════════════════════════════
// End-to-end: verify source text changes with CharacterOffset in animator
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("PreShaping E2E: animator with CharacterOffset transforms source") {
    TextAnimatorSpec spec;
    spec.id = "test_offset";
    spec.properties.push_back(CharacterOffsetProperty{1});
    std::vector<TextAnimatorSpec> animators{spec};

    CHECK(has_pre_shaping_properties(animators));

    std::string original = "HELLO";
    std::string transformed = evaluate_pre_shaping_source(animators, original);
    CHECK(transformed == "IFMMP");
    CHECK(transformed != original);
}

TEST_CASE("PreShaping E2E: without CharacterOffset, source is unchanged") {
    TextAnimatorSpec spec;
    spec.id = "test_opacity";
    spec.properties.push_back(OpacityProperty{0.5f});
    spec.properties.push_back(PositionProperty{});
    std::vector<TextAnimatorSpec> animators{spec};

    CHECK_FALSE(has_pre_shaping_properties(animators));

    std::string original = "HELLO";
    std::string result = evaluate_pre_shaping_source(animators, original);
    CHECK(result == original);
}

TEST_CASE("PreShaping E2E: ligature text 'fi' becomes 'gj' with offset=1") {
    // When "fi" becomes "gj", the fi ligature should NOT form —
    // the source text changes before HarfBuzz sees it.
    TextAnimatorSpec spec;
    spec.properties.push_back(CharacterOffsetProperty{1});
    std::vector<TextAnimatorSpec> animators{spec};

    std::string original = "fi";
    std::string transformed = evaluate_pre_shaping_source(animators, original);
    CHECK(transformed == "gj");
    CHECK(transformed != original);
}

TEST_CASE("PreShaping E2E: RTL Arabic text unchanged by ASCII-only offset") {
    // Arabic text (non-ASCII) should pass through the Caesar cipher
    // unchanged — only ASCII letters are shifted.
    std::string arabic = "\xd8\xa8";  // U+0628 BAH
    TextAnimatorSpec spec;
    spec.properties.push_back(CharacterOffsetProperty{3});
    std::vector<TextAnimatorSpec> animators{spec};

    std::string result = evaluate_pre_shaping_source(animators, arabic);
    CHECK(result == arabic);
}

TEST_CASE("PreShaping E2E: combined characters pass through") {
    // e + combining acute accent (U+0065 U+0301) = é
    // The 'e' is ASCII and gets shifted; the combining accent passes through.
    std::string combined = "e\xcc\x81";  // e + U+0301
    TextAnimatorSpec spec;
    spec.properties.push_back(CharacterOffsetProperty{1});
    std::vector<TextAnimatorSpec> animators{spec};

    std::string result = evaluate_pre_shaping_source(animators, combined);
    // 'e' shifts to 'f', combining accent stays
    std::string expected = "f\xcc\x81";
    CHECK(result == expected);
}

// ═══════════════════════════════════════════════════════════════════════════
// P1-01 — Character Value: replace every non-whitespace code point
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("CharacterValue: replaces letters with the sampled scalar") {
    CHECK(apply_character_value_to_source("abc", U'A') == "AAA");
    CHECK(apply_character_value_to_source("Hello", U'x') == "xxxxx");
}

TEST_CASE("CharacterValue: preserves ASCII whitespace and line structure") {
    CHECK(apply_character_value_to_source("a b\tc\nd", U'#') == "# #\t#\n#");
}

TEST_CASE("CharacterValue: replaces multi-byte letters with one scalar") {
    CHECK(apply_character_value_to_source("caf\xc3\xa9", U'Z') == "ZZZZ");
}

TEST_CASE("CharacterValue: invalid scalars leave the source unchanged") {
    CHECK(apply_character_value_to_source("abc", 0) == "abc");
    CHECK(apply_character_value_to_source("abc", 0xD800) == "abc");
    CHECK(apply_character_value_to_source("abc", 0x110000) == "abc");
}

TEST_CASE("CharacterValue: evaluate_pre_shaping_source substitutes source") {
    TextAnimatorSpec spec;
    spec.properties.push_back(CharacterValueProperty{65.0f});  // 'A'
    std::vector<TextAnimatorSpec> animators{spec};

    CHECK(has_pre_shaping_properties(animators));
    CHECK(evaluate_pre_shaping_source(animators, "abc") == "AAA");
}

TEST_CASE("CharacterValue: offset applies first, value replaces the result") {
    TextAnimatorSpec offset_spec;
    offset_spec.properties.push_back(CharacterOffsetProperty{1});
    TextAnimatorSpec value_spec;
    value_spec.properties.push_back(CharacterValueProperty{88.0f});  // 'X'
    std::vector<TextAnimatorSpec> animators{offset_spec, value_spec};

    CHECK(evaluate_pre_shaping_source(animators, "ab") == "XX");
}

TEST_CASE("CharacterValue: zero sampled value is a no-op") {
    TextAnimatorSpec spec;
    spec.properties.push_back(CharacterValueProperty{0.0f});
    std::vector<TextAnimatorSpec> animators{spec};

    CHECK(evaluate_pre_shaping_source(animators, "ab") == "ab");
}

// ═══════════════════════════════════════════════════════════════════════════
// ADR-037 — selector-scoped PreShaping through the canonical unit map
// ═══════════════════════════════════════════════════════════════════════════

namespace {

PlacedGlyphRun make_adr_placed_run(std::initializer_list<std::size_t> byte_lengths) {
    PlacedGlyphRun run;
    std::size_t byte_offset = 0;
    std::size_t index = 0;
    for (const std::size_t byte_len : byte_lengths) {
        PlacedGlyph glyph;
        glyph.glyph_id = static_cast<u32>(index + 1);
        glyph.cluster = static_cast<u32>(index);
        glyph.is_cluster_start = true;
        glyph.advance_x = 10.0f;
        glyph.x = static_cast<f32>(index) * 10.0f;
        glyph.y = 0.0f;
        glyph.byte_offset = byte_offset;
        glyph.byte_len = byte_len;
        run.glyphs.push_back(glyph);

        PlacedGlyphRun::Cluster cluster;
        cluster.start_glyph = index;
        cluster.end_glyph = index + 1;
        cluster.byte_offset = byte_offset;
        cluster.byte_len = byte_len;
        cluster.advance = 10.0f;
        cluster.raw_advance = 10.0f;
        run.clusters.push_back(cluster);

        byte_offset += byte_len;
        ++index;
    }
    run.total_width = static_cast<f32>(index) * 10.0f;
    run.total_height = 16.0f;
    run.ascent = 12.0f;
    run.descent = 4.0f;
    run.baseline = 12.0f;
    run.font_size = 16.0f;
    return run;
}

GlyphSelectorSpec make_adr_selector(
    TextSelectorUnit unit,
    f32 start,
    f32 end,
    TextSelectorOrder order = TextSelectorOrder::Forward,
    f32 amount = 100.0f
) {
    GlyphSelectorSpec selector;
    selector.unit = unit;
    selector.shape = TextSelectorShape::Square;
    selector.order = order;
    selector.start.set(start);
    selector.end.set(end);
    selector.amount.set(amount);
    selector.exclude_spaces = true;
    return selector;
}

} // namespace

TEST_CASE("ADR-037: word selector scopes CharacterOffset to selected codepoints") {
    const std::string source = "AB CD";
    const auto run = make_adr_placed_run({1, 1, 1, 1});
    const auto map = build_pre_shaping_unit_map(run, source);
    REQUIRE(map != nullptr);

    TextAnimatorSpec spec;
    spec.selectors.push_back(
        make_adr_selector(TextSelectorUnit::Word, 0.0f, 50.0f));
    spec.properties.push_back(CharacterOffsetProperty{1});

    CHECK(evaluate_pre_shaping_source(
              {spec}, source, sample_at(0), map.get()) == "BC CD");
}

TEST_CASE("ADR-037: word selector scopes CharacterValue to selected codepoints") {
    const std::string source = "AB CD";
    const auto run = make_adr_placed_run({1, 1, 1, 1});
    const auto map = build_pre_shaping_unit_map(run, source);
    REQUIRE(map != nullptr);

    TextAnimatorSpec spec;
    spec.selectors.push_back(
        make_adr_selector(TextSelectorUnit::Word, 0.0f, 50.0f));
    spec.properties.push_back(CharacterValueProperty{88.0f});  // 'X'

    CHECK(evaluate_pre_shaping_source(
              {spec}, source, sample_at(0), map.get()) == "XX CD");
}

TEST_CASE("ADR-037: partial weight rounds each selected codepoint") {
    const std::string source = "AB CD";
    const auto run = make_adr_placed_run({1, 1, 1, 1});
    const auto map = build_pre_shaping_unit_map(run, source);
    REQUIRE(map != nullptr);

    TextAnimatorSpec spec;
    spec.selectors.push_back(
        make_adr_selector(TextSelectorUnit::Word, 0.0f, 50.0f,
                          TextSelectorOrder::Forward, 50.0f));
    spec.properties.push_back(CharacterOffsetProperty{3});

    // 3 * 0.5 = 1.5, rounded per codepoint -> 2 (not 1.5 on the run total).
    CHECK(evaluate_pre_shaping_source(
              {spec}, source, sample_at(0), map.get()) == "CD CD");
}

TEST_CASE("ADR-037: CharacterValue replaces a ligature as one grapheme") {
    const std::string source = "fi";
    const auto run = make_adr_placed_run({2});
    const auto map = build_pre_shaping_unit_map(run, source);
    REQUIRE(map != nullptr);

    TextAnimatorSpec spec;
    spec.selectors.push_back(
        make_adr_selector(TextSelectorUnit::Grapheme, 0.0f, 100.0f));
    spec.properties.push_back(CharacterValueProperty{88.0f});  // 'X'

    CHECK(evaluate_pre_shaping_source(
              {spec}, source, sample_at(0), map.get()) == "X");
}

TEST_CASE("ADR-037: combining marks stay with their base grapheme") {
    const std::string source = "e\xCC\x81x";  // e + U+0301 COMBINING ACUTE + x
    const auto run = make_adr_placed_run({3, 1});
    const auto map = build_pre_shaping_unit_map(run, source);
    REQUIRE(map != nullptr);

    TextAnimatorSpec spec;
    spec.selectors.push_back(
        make_adr_selector(TextSelectorUnit::Grapheme, 0.0f, 50.0f));
    spec.properties.push_back(CharacterValueProperty{65.0f});  // 'A'

    // The selected grapheme is replaced whole: no dangling combining mark.
    CHECK(evaluate_pre_shaping_source(
              {spec}, source, sample_at(0), map.get()) == "Ax");
}

TEST_CASE("ADR-037: CharacterValue threshold is inclusive at 50 percent") {
    const std::string source = "AB";
    const auto run = make_adr_placed_run({1, 1});
    const auto map = build_pre_shaping_unit_map(run, source);
    REQUIRE(map != nullptr);

    const auto value_at = [&](f32 amount) {
        TextAnimatorSpec spec;
        spec.selectors.push_back(
            make_adr_selector(TextSelectorUnit::Grapheme, 0.0f, 100.0f,
                              TextSelectorOrder::Forward, amount));
        spec.properties.push_back(CharacterValueProperty{88.0f});  // 'X'
        return evaluate_pre_shaping_source(
            {spec}, source, sample_at(0), map.get());
    };

    CHECK(value_at(50.0f) == "XX");
    CHECK(value_at(49.0f) == "AB");
}

TEST_CASE("ADR-037: selector-scoped evaluation is stateless across frames") {
    const std::string source = "ABCD";
    const auto run = make_adr_placed_run({1, 1, 1, 1});
    const auto map = build_pre_shaping_unit_map(run, source);
    REQUIRE(map != nullptr);

    TextAnimatorSpec spec;
    spec.selectors.push_back(
        make_adr_selector(TextSelectorUnit::Grapheme, 0.0f, 100.0f));
    spec.properties.push_back(CharacterOffsetProperty{});
    auto* offset = std::get_if<CharacterOffsetProperty>(&spec.properties.back());
    REQUIRE(offset != nullptr);
    offset->offset.add_keyframe(Frame{0}, 0.0f);
    offset->offset.add_keyframe(Frame{10}, 4.0f);

    CHECK(evaluate_pre_shaping_source(
              {spec}, source, sample_at(10), map.get()) == "EFGH");
    CHECK(evaluate_pre_shaping_source(
              {spec}, source, sample_at(0), map.get()) == "ABCD");
    CHECK(evaluate_pre_shaping_source(
              {spec}, source, sample_at(10), map.get()) == "EFGH");
}

TEST_CASE("ADR-037: Reverse order operates on logical source order for RTL") {
    const std::string source = "\xD8\xA8\xD8\xAA";  // Arabic beh + teh
    const auto run = make_adr_placed_run({2, 2});
    const auto map = build_pre_shaping_unit_map(run, source);
    REQUIRE(map != nullptr);

    TextAnimatorSpec spec;
    spec.selectors.push_back(
        make_adr_selector(TextSelectorUnit::Grapheme, 0.0f, 50.0f,
                          TextSelectorOrder::Reverse));
    spec.properties.push_back(CharacterValueProperty{88.0f});  // 'X'

    // Reverse selects the second logical codepoint (teh), not the visually
    // leading one; PreShaping happens before bidi reordering.
    CHECK(evaluate_pre_shaping_source(
              {spec}, source, sample_at(0), map.get()) == "\xD8\xA8X");
}
