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
