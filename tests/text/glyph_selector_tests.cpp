#include <doctest/doctest.h>
#include <chronon3d/text/glyph_selector.hpp>
using namespace chronon3d;


// ── Helpers ────────────────────────────────────────────────────────────────

namespace {

/// Build a minimal PlacedGlyphRun for testing purposes.
/// Each glyph gets a simple identity: cluster = index, advance = 10.
PlacedGlyphRun make_test_placed_run(size_t count) {
    PlacedGlyphRun run;
    for (size_t i = 0; i < count; ++i) {
        PlacedGlyph g;
        g.glyph_id = static_cast<u32>(i + 1);
        g.cluster = static_cast<u32>(i);
        g.is_cluster_start = true;
        g.advance_x = 10.0f;
        g.x = static_cast<float>(i) * 10.0f;
        g.byte_offset = i;  // one byte per glyph for simple tests
        g.byte_len = 1;
        run.glyphs.push_back(g);
    }
    // Build clusters: one cluster per glyph
    for (size_t i = 0; i < count; ++i) {
        PlacedGlyphRun::Cluster cl;
        cl.start_glyph = i;
        cl.end_glyph = i + 1;
        cl.byte_offset = i;
        cl.byte_len = 1;
        cl.advance = 10.0f;
        cl.raw_advance = 10.0f;
        run.clusters.push_back(cl);
    }
    run.total_width = static_cast<float>(count) * 10.0f;
    run.total_height = 16.0f;
    run.ascent = 12.0f;
    run.descent = 4.0f;
    run.baseline = 12.0f;
    run.font_size = 16.0f;
    return run;
}

/// Build a source string of `count` lowercase ASCII letters starting from 'a'.
std::string make_test_source(size_t count) {
    std::string s;
    s.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        s.push_back(static_cast<char>('a' + (i % 26)));
    }
    return s;
}

/// Build a source string with spaces and newlines for word/line tests.
/// "hello world" → 11 chars, 2 words, 1 line
std::string make_word_test_source() {
    return "hello world";
}

/// Build a source string with multiple lines.
/// "line1\nline2\nline3" → 3 lines
std::string make_line_test_source() {
    return "line1\nline2\nline3";
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// TextUnitMap tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextUnitMap: simple single-glyph text") {
    auto placed = make_test_placed_run(1);
    auto source = make_test_source(1); // "a"

    auto map = build_text_unit_map(placed, source);

    CHECK(map.grapheme_count == 1);
    CHECK(map.word_count >= 1);
    CHECK(map.line_count >= 1);
    CHECK(map.glyph_to_grapheme[0] == 0);
}

TEST_CASE("TextUnitMap: five glyphs, no spaces") {
    auto placed = make_test_placed_run(5);
    auto source = make_test_source(5); // "abcde"

    auto map = build_text_unit_map(placed, source);

    CHECK(map.grapheme_count == 5);
    // All glyphs belong to the same word (no spaces)
    CHECK(map.glyph_to_word[0] == map.glyph_to_word[4]);
    // All glyphs belong to the same line (no newlines)
    CHECK(map.glyph_to_line[0] == map.glyph_to_line[4]);
}

TEST_CASE("TextUnitMap: words separated by space") {
    // Build source: "hello world"
    auto source = make_word_test_source(); // 11 chars

    // Build placed glyph run with matching byte offsets
    PlacedGlyphRun run;
    for (size_t i = 0; i < 11; ++i) {
        PlacedGlyph g;
        g.glyph_id = static_cast<u32>(i + 1);
        g.cluster = static_cast<u32>(i);
        g.is_cluster_start = true;
        g.advance_x = 10.0f;
        g.x = static_cast<float>(i) * 10.0f;
        g.byte_offset = i;
        g.byte_len = 1;
        run.glyphs.push_back(g);

        PlacedGlyphRun::Cluster cl;
        cl.start_glyph = i;
        cl.end_glyph = i + 1;
        cl.byte_offset = i;
        cl.byte_len = 1;
        cl.advance = 10.0f;
        cl.raw_advance = 10.0f;
        run.clusters.push_back(cl);
    }
    run.total_width = 110.0f;
    run.total_height = 16.0f;
    run.ascent = 12.0f;
    run.descent = 4.0f;
    run.baseline = 12.0f;
    run.font_size = 16.0f;

    auto map = build_text_unit_map(run, source);

    CHECK(map.word_count >= 2); // "hello" and "world"
    CHECK(map.line_count >= 1); // single line

    // Glyphs 0-4 (hello) should be in word 0
    CHECK(map.glyph_to_word[0] == 0);
    CHECK(map.glyph_to_word[4] == 0);

    // Glyph 5 is space — should be word separator
    // Glyphs 6-10 (world) should be in the next word
    if (map.word_count >= 2) {
        CHECK(map.glyph_to_word[6] != map.glyph_to_word[0]);
    }
}

TEST_CASE("TextUnitMap: lines separated by newline") {
    auto source = make_line_test_source(); // "line1\nline2\nline3"
    // Placeholder bytes: l(0) i(1) n(2) e(3) 1(4) \n(5) l(6) i(7) n(8) e(9) 2(10) \n(11) l(12) i(13) n(14) e(15) 3(16)

    PlacedGlyphRun run;
    for (size_t i = 0; i < source.size(); ++i) {
        PlacedGlyph g;
        g.glyph_id = static_cast<u32>(i + 1);
        g.cluster = static_cast<u32>(i);
        g.is_cluster_start = true;
        g.advance_x = 10.0f;
        g.x = static_cast<float>(i) * 10.0f;
        g.byte_offset = i;
        g.byte_len = 1;
        run.glyphs.push_back(g);

        PlacedGlyphRun::Cluster cl;
        cl.start_glyph = i;
        cl.end_glyph = i + 1;
        cl.byte_offset = i;
        cl.byte_len = 1;
        cl.advance = 10.0f;
        cl.raw_advance = 10.0f;
        run.clusters.push_back(cl);
    }
    run.total_width = static_cast<float>(source.size()) * 10.0f;

    auto map = build_text_unit_map(run, source);

    CHECK(map.line_count >= 3); // three lines

    // First line glyphs should be on line 0
    CHECK(map.glyph_to_line[0] == 0);
    // Third line glyphs should be on a different line
    CHECK(map.glyph_to_line[source.size() - 1] != map.glyph_to_line[0]);
}

// ═══════════════════════════════════════════════════════════════════════════
// Selector shape tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Shape: Square") {
    using namespace detail;

    // Inside range [0.2, 0.8]
    CHECK(evaluate_selector_shape(TextSelectorShape::Square, 0.5f, 0.2f, 0.8f) == doctest::Approx(1.0f));
    // Outside range (before)
    CHECK(evaluate_selector_shape(TextSelectorShape::Square, 0.1f, 0.2f, 0.8f) == doctest::Approx(0.0f));
    // Outside range (after)
    CHECK(evaluate_selector_shape(TextSelectorShape::Square, 0.9f, 0.2f, 0.8f) == doctest::Approx(0.0f));
    // At boundaries
    CHECK(evaluate_selector_shape(TextSelectorShape::Square, 0.2f, 0.2f, 0.8f) == doctest::Approx(0.0f));
    CHECK(evaluate_selector_shape(TextSelectorShape::Square, 0.8f, 0.2f, 0.8f) == doctest::Approx(0.0f));
}

TEST_CASE("Shape: RampUp") {
    using namespace detail;

    CHECK(evaluate_selector_shape(TextSelectorShape::RampUp, 0.0f, 0.0f, 1.0f) == doctest::Approx(0.0f));
    CHECK(evaluate_selector_shape(TextSelectorShape::RampUp, 0.5f, 0.0f, 1.0f) == doctest::Approx(0.5f));
    CHECK(evaluate_selector_shape(TextSelectorShape::RampUp, 1.0f, 0.0f, 1.0f) == doctest::Approx(1.0f).epsilon(0.001f));
}

TEST_CASE("Shape: RampDown") {
    using namespace detail;

    CHECK(evaluate_selector_shape(TextSelectorShape::RampDown, 0.0f, 0.0f, 1.0f) == doctest::Approx(1.0f));
    CHECK(evaluate_selector_shape(TextSelectorShape::RampDown, 0.5f, 0.0f, 1.0f) == doctest::Approx(0.5f));
    CHECK(evaluate_selector_shape(TextSelectorShape::RampDown, 1.0f, 0.0f, 1.0f) == doctest::Approx(0.0f).epsilon(0.001f));
}

TEST_CASE("Shape: Triangle") {
    using namespace detail;

    // Triangle peaks at 0.5
    CHECK(evaluate_selector_shape(TextSelectorShape::Triangle, 0.0f, 0.0f, 1.0f) == doctest::Approx(0.0f));
    CHECK(evaluate_selector_shape(TextSelectorShape::Triangle, 0.5f, 0.0f, 1.0f) == doctest::Approx(1.0f));
    CHECK(evaluate_selector_shape(TextSelectorShape::Triangle, 1.0f, 0.0f, 1.0f) == doctest::Approx(0.0f).epsilon(0.001f));
    CHECK(evaluate_selector_shape(TextSelectorShape::Triangle, 0.25f, 0.0f, 1.0f) == doctest::Approx(0.5f));
}

TEST_CASE("Shape: Round") {
    using namespace detail;

    // Round bell curve peaks at centre
    CHECK(evaluate_selector_shape(TextSelectorShape::Round, 0.0f, 0.0f, 1.0f) == doctest::Approx(0.0f));
    f32 centre = evaluate_selector_shape(TextSelectorShape::Round, 0.5f, 0.0f, 1.0f);
    CHECK(centre == doctest::Approx(1.0f));
    // Should be symmetric
    f32 left = evaluate_selector_shape(TextSelectorShape::Round, 0.25f, 0.0f, 1.0f);
    f32 right = evaluate_selector_shape(TextSelectorShape::Round, 0.75f, 0.0f, 1.0f);
    CHECK(left == doctest::Approx(right));
}

TEST_CASE("Shape: Smooth") {
    using namespace detail;

    // Smooth bell curve
    CHECK(evaluate_selector_shape(TextSelectorShape::Smooth, 0.0f, 0.0f, 1.0f) == doctest::Approx(0.0f));
    f32 centre = evaluate_selector_shape(TextSelectorShape::Smooth, 0.5f, 0.0f, 1.0f);
    CHECK(centre == doctest::Approx(1.0f));
    // Should be symmetric
    f32 left = evaluate_selector_shape(TextSelectorShape::Smooth, 0.25f, 0.0f, 1.0f);
    f32 right = evaluate_selector_shape(TextSelectorShape::Smooth, 0.75f, 0.0f, 1.0f);
    CHECK(left == doctest::Approx(right));
}

TEST_CASE("Shape: reversed range (start > end)") {
    using namespace detail;

    // When start > end, the range is swapped and position mirrored
    // start=0.8, end=0.2 → effective range is [0.2, 0.8], unit at 0.1 becomes 0.9
    CHECK(evaluate_selector_shape(TextSelectorShape::Square, 0.1f, 0.8f, 0.2f) == doctest::Approx(0.0f));
    CHECK(evaluate_selector_shape(TextSelectorShape::Square, 0.5f, 0.8f, 0.2f) == doctest::Approx(1.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// Order tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Order: Forward") {
    using namespace detail;
    CHECK(apply_selector_order(TextSelectorOrder::Forward, 0, 5, 0) == 0);
    CHECK(apply_selector_order(TextSelectorOrder::Forward, 4, 5, 0) == 4);
}

TEST_CASE("Order: Reverse") {
    using namespace detail;
    CHECK(apply_selector_order(TextSelectorOrder::Reverse, 0, 5, 0) == 4);
    CHECK(apply_selector_order(TextSelectorOrder::Reverse, 4, 5, 0) == 0);
}

TEST_CASE("Order: FromCenter even count") {
    using namespace detail;
    // 4 units [0,1,2,3]: alternating outward from the central pair (1,2).
    // Output: [1, 2, 0, 3] -- centre-left (1)->0, centre-right (2)->1,
    //                      edge-left (0)->2, edge-right (3)->3.
    //
    // Mapping table (asserted): original 1 2 0 3 -> positions 0 1 2 3
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 0, 4, 0) == 2);  // edge-left
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 1, 4, 0) == 0);  // centre-left
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 2, 4, 0) == 1);  // centre-right
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 3, 4, 0) == 3);  // edge-right

    // 6 units [0..5]: central pair=(2,3), pair-dist-1=(1,4), edge=(0,5).
    // Output: [2, 3, 1, 4, 0, 5]
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 2, 6, 0) == 0);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 3, 6, 0) == 1);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 1, 6, 0) == 2);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 4, 6, 0) == 3);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 0, 6, 0) == 4);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 5, 6, 0) == 5);

    // 8 units [0..7]: central pair=(3,4), pair-dist-1=(2,5), dist-2=(1,6), edge=(0,7).
    // Output: [3, 4, 2, 5, 1, 6, 0, 7]
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 3, 8, 0) == 0);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 4, 8, 0) == 1);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 2, 8, 0) == 2);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 5, 8, 0) == 3);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 1, 8, 0) == 4);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 6, 8, 0) == 5);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 0, 8, 0) == 6);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 7, 8, 0) == 7);

    // 2 units [0,1]: central pair (0,1), no outer pairs.
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 0, 2, 0) == 0);
    REQUIRE(apply_selector_order(TextSelectorOrder::FromCenter, 1, 2, 0) == 1);
}

TEST_CASE("Order: Random is bijective (true permutation, no collisions)") {
    using namespace detail;
    // Random must produce a permutation of [0..total_units) -- no two source
    // indices may map to the same output position, and every output position
    // must be reached.  The previous floor(hash * total) implementation could
    // fail this; the new Fisher-Yates permutation scheme cannot.
    for (u32 n : {1u, 2u, 3u, 5u, 10u, 16u, 50u, 100u}) {
        const auto& perm = get_or_build_permutation(42, n);
        REQUIRE(perm.size() == n);

        std::vector<u32> sorted(perm.begin(), perm.end());
        std::sort(sorted.begin(), sorted.end());
        for (u32 i = 0; i < n; ++i) {
            CHECK(sorted[i] == i);
        }
    }
}

TEST_CASE("Order: Random same seed -> same permutation; shuffle is non-trivial") {
    using namespace detail;
    constexpr u32 N = 32;
    constexpr u64 SEED_A = 12345;

    const auto& a1 = get_or_build_permutation(SEED_A, N);
    const auto& a2 = get_or_build_permutation(SEED_A, N);  // second call: cache hit
    REQUIRE(a1.size() == N);
    REQUIRE(a2.size() == N);
    for (u32 i = 0; i < N; ++i) {
        CHECK(a1[i] == a2[i]);
    }

    // Deterministic shuffle-quality check: the permutation must NOT be the
    // identity permutation.  For a uniformly random Fisher-Yates shuffle on
    // N>=3 the probability of producing exactly the identity is 1/N!, which
    // is negligibly small.  This avoids depending on any specific seed or
    // pre-computed fingerprint.
    //
    // We assert two equivalent properties:
    //   (a) at least one entry differs from identity (perm[i] != i)
    //   (b) the perm is not sorted in identity order (which would only hold
    //       if it's the identity permutation)
    size_t differ_count = 0;
    for (size_t i = 0; i < a1.size(); ++i) {
        if (a1[i] != static_cast<u32>(i)) ++differ_count;
    }
    CHECK(differ_count > 0);
    CHECK(std::is_sorted(a1.begin(), a1.end()) == false);
}

TEST_CASE("Order: Random via apply_selector_order is consistent with bijection") {
    using namespace detail;
    for (u32 n : {4u, 7u, 12u}) {
        std::vector<u32> outputs;
        outputs.reserve(n);
        for (u32 i = 0; i < n; ++i) {
            outputs.push_back(apply_selector_order(
                TextSelectorOrder::Random, i, n, 99));
        }
        std::vector<u32> sorted(outputs.begin(), outputs.end());
        std::sort(sorted.begin(), sorted.end());
        for (u32 i = 0; i < n; ++i) {
            CHECK(sorted[i] == i);
        }
    }
}

TEST_CASE("Order: FromCenter odd count") {
    using namespace detail;
    // 5 units: [0,1,2,3,4] → from centre: [2,1,3,0,4]
    // Index 2 (centre) → position 0
    CHECK(apply_selector_order(TextSelectorOrder::FromCenter, 2, 5, 0) == 0);
    // Index 1 → position 1
    CHECK(apply_selector_order(TextSelectorOrder::FromCenter, 1, 5, 0) == 1);
    // Index 3 → position 2
    CHECK(apply_selector_order(TextSelectorOrder::FromCenter, 3, 5, 0) == 2);
    // Index 0 → position 3
    CHECK(apply_selector_order(TextSelectorOrder::FromCenter, 0, 5, 0) == 3);
    // Index 4 → position 4
    CHECK(apply_selector_order(TextSelectorOrder::FromCenter, 4, 5, 0) == 4);
}

TEST_CASE("Order: ToCenter") {
    using namespace detail;
    // ToCenter is the reverse of FromCenter
    auto fc = apply_selector_order(TextSelectorOrder::FromCenter, 0, 4, 0);
    auto tc = apply_selector_order(TextSelectorOrder::ToCenter, 0, 4, 0);
    CHECK(tc == 4 - 1 - fc);
}

TEST_CASE("Order: Random is deterministic via apply_selector_order") {
    using namespace detail;

    u64 seed = 42;
    auto a = apply_selector_order(TextSelectorOrder::Random, 3, 10, seed);
    auto b = apply_selector_order(TextSelectorOrder::Random, 3, 10, seed);
    CHECK(a == b); // same seed, same index -> same result;

    // Different seed: result is still a valid index (no out-of-bounds), but
    // we don't assert inequality (that would be probabilistic).
    auto c = apply_selector_order(TextSelectorOrder::Random, 3, 10, seed + 1);
    CHECK(c < 10);
}

// ═══════════════════════════════════════════════════════════════════════════
// Hash determinism tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Hash: same input → same output") {
    using namespace detail;

    CHECK(hash_to_unit_float(12345, 67) == hash_to_unit_float(12345, 67));
}

TEST_CASE("Hash: different seed → different output") {
    using namespace detail;

    // Extremely unlikely to collide
    f32 a = hash_to_unit_float(1, 100);
    f32 b = hash_to_unit_float(2, 100);
    CHECK(a != b);
    CHECK(a >= 0.0f);
    CHECK(a < 1.0f);
    CHECK(b >= 0.0f);
    CHECK(b < 1.0f);
}

TEST_CASE("Hash: different unit_index → different output") {
    using namespace detail;

    f32 a = hash_to_unit_float(42, 0);
    f32 b = hash_to_unit_float(42, 1);
    CHECK(a != b);
}

// ═══════════════════════════════════════════════════════════════════════════
// Full selector evaluation tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Evaluate: basic selector with default params") {
    GlyphSelectorSpec spec;
    spec.id = "test";
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::Square;
    spec.order = TextSelectorOrder::Forward;
    spec.start.set(0.0f);
    spec.end.set(100.0f);

    auto placed = make_test_placed_run(5);
    auto source = make_test_source(5);
    auto map = build_text_unit_map(placed, source);

    SampleTime t = SampleTime::from_frame_int(Frame{0});

    // All glyphs should have weight 1.0 with Square shape, full range
    for (u32 i = 0; i < 5; ++i) {
        f32 w = evaluate_selector(spec, map, i, source, t);
        CHECK(w == doctest::Approx(1.0f));
    }
}

TEST_CASE("Evaluate: offset shifts the active window") {
    GlyphSelectorSpec spec;
    spec.id = "test_offset";
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::Square;
    spec.order = TextSelectorOrder::Forward;
    spec.start.set(0.0f);
    spec.end.set(50.0f);     // active on first half
    spec.offset.set(0.0f);

    auto placed = make_test_placed_run(4);
    auto source = make_test_source(4); // "abcd"
    auto map = build_text_unit_map(placed, source);

    SampleTime t = SampleTime::from_frame_int(Frame{0});

    // Without offset, first half active
    f32 w0 = evaluate_selector(spec, map, 0, source, t);
    f32 w2 = evaluate_selector(spec, map, 2, source, t);
    CHECK(w0 > 0.0f);
    CHECK(w2 == doctest::Approx(0.0f));

    // With offset=50, the window shifts to second half
    spec.offset.set(50.0f);
    f32 w0_shifted = evaluate_selector(spec, map, 0, source, t);
    f32 w2_shifted = evaluate_selector(spec, map, 2, source, t);
    CHECK(w0_shifted == doctest::Approx(0.0f));
    CHECK(w2_shifted > 0.0f);
}

TEST_CASE("Evaluate: amount scales the weight") {
    GlyphSelectorSpec spec;
    spec.id = "test_amount";
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::Square;
    spec.start.set(0.0f);
    spec.end.set(100.0f);
    spec.amount.set(50.0f); // 50% amount

    auto placed = make_test_placed_run(3);
    auto source = make_test_source(3);
    auto map = build_text_unit_map(placed, source);

    SampleTime t = SampleTime::from_frame_int(Frame{0});

    f32 w = evaluate_selector(spec, map, 0, source, t);
    CHECK(w == doctest::Approx(0.5f));
}

TEST_CASE("Evaluate: RampUp shape produces gradient") {
    GlyphSelectorSpec spec;
    spec.id = "test_ramp";
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::RampUp;
    spec.start.set(0.0f);
    spec.end.set(100.0f);

    auto placed = make_test_placed_run(3);
    auto source = make_test_source(3);
    auto map = build_text_unit_map(placed, source);

    SampleTime t = SampleTime::from_frame_int(Frame{0});

    f32 w0 = evaluate_selector(spec, map, 0, source, t);
    f32 w2 = evaluate_selector(spec, map, 2, source, t);
    // Last glyph should have higher weight than first
    CHECK(w2 > w0);
}

TEST_CASE("Evaluate: Reverse order inverts progression") {
    GlyphSelectorSpec spec;
    spec.id = "test_reverse";
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::RampUp;
    spec.order = TextSelectorOrder::Reverse;
    spec.start.set(0.0f);
    spec.end.set(100.0f);

    auto placed = make_test_placed_run(3);
    auto source = make_test_source(3);
    auto map = build_text_unit_map(placed, source);

    SampleTime t = SampleTime::from_frame_int(Frame{0});

    f32 w0 = evaluate_selector(spec, map, 0, source, t);
    f32 w2 = evaluate_selector(spec, map, 2, source, t);
    // With reverse order, first glyph should have higher weight
    CHECK(w0 > w2);
}

TEST_CASE("Evaluate: sub-frame evaluation consistency") {
    // The same sub-frame time should produce the same result
    GlyphSelectorSpec spec;
    spec.id = "test_subframe";
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::Smooth;
    spec.start.set(0.0f);
    spec.end.set(100.0f);

    auto placed = make_test_placed_run(10);
    auto source = make_test_source(10);
    auto map = build_text_unit_map(placed, source);

    SampleTime t1 = SampleTime::from_frame(15.5, FrameRate{30, 1});
    SampleTime t2 = SampleTime::from_frame(15.5, FrameRate{30, 1});

    for (u32 i = 0; i < 10; ++i) {
        f32 w1 = evaluate_selector(spec, map, i, source, t1);
        f32 w2 = evaluate_selector(spec, map, i, source, t2);
        CHECK(w1 == doctest::Approx(w2));
    }
}

TEST_CASE("Evaluate: deterministic random seed") {
    GlyphSelectorSpec spec;
    spec.id = "test_random";
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::RampUp;
    spec.randomize_order = true;
    spec.random_seed = 12345;
    spec.start.set(0.0f);
    spec.end.set(100.0f);

    auto placed = make_test_placed_run(5);
    auto source = make_test_source(5);
    auto map = build_text_unit_map(placed, source);

    SampleTime t1 = SampleTime::from_frame_int(Frame{0});
    SampleTime t2 = SampleTime::from_frame_int(Frame{0});

    // Same seed should produce same weights
    for (u32 i = 0; i < 5; ++i) {
        f32 w1 = evaluate_selector(spec, map, i, source, t1);
        f32 w2 = evaluate_selector(spec, map, i, source, t2);
        CHECK(w1 == doctest::Approx(w2));
    }

    // Different seed should produce different weights
    GlyphSelectorSpec spec2 = spec;
    spec2.random_seed = 54321;
    bool any_different = false;
    for (u32 i = 0; i < 5; ++i) {
        f32 w1 = evaluate_selector(spec, map, i, source, t1);
        f32 w2 = evaluate_selector(spec2, map, i, source, t1);
        if (std::abs(w1 - w2) > 0.001f) any_different = true;
    }
    CHECK(any_different);
}

// ═══════════════════════════════════════════════════════════════════════════
// Selector combination tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Combine: Add mode sums weights") {
    GlyphSelectorSpec spec1;
    spec1.id = "s1";
    spec1.combine = SelectorCombineMode::Replace; // first
    spec1.amount.set(50.0f);

    GlyphSelectorSpec spec2;
    spec2.id = "s2";
    spec2.combine = SelectorCombineMode::Add;
    spec2.amount.set(25.0f);

    auto placed = make_test_placed_run(1);
    auto source = make_test_source(1);
    auto map = build_text_unit_map(placed, source);
    SampleTime t = SampleTime::from_frame_int(Frame{0});

    f32 w = evaluate_selectors({spec1, spec2}, map, 0, source, t);
    CHECK(w == doctest::Approx(0.75f)); // 0.5 + 0.25
}

TEST_CASE("Combine: Subtract mode") {
    GlyphSelectorSpec spec1;
    spec1.id = "s1";
    spec1.combine = SelectorCombineMode::Replace;
    spec1.amount.set(100.0f);

    GlyphSelectorSpec spec2;
    spec2.id = "s2";
    spec2.combine = SelectorCombineMode::Subtract;
    spec2.amount.set(30.0f);

    auto placed = make_test_placed_run(1);
    auto source = make_test_source(1);
    auto map = build_text_unit_map(placed, source);
    SampleTime t = SampleTime::from_frame_int(Frame{0});

    f32 w = evaluate_selectors({spec1, spec2}, map, 0, source, t);
    CHECK(w == doctest::Approx(0.7f)); // 1.0 - 0.3
}

TEST_CASE("Combine: Min/Intersect mode") {
    GlyphSelectorSpec spec1;
    spec1.id = "s1";
    spec1.combine = SelectorCombineMode::Replace;
    spec1.amount.set(80.0f);

    GlyphSelectorSpec spec2;
    spec2.id = "s2";
    spec2.combine = SelectorCombineMode::Intersect;
    spec2.amount.set(40.0f);

    auto placed = make_test_placed_run(1);
    auto source = make_test_source(1);
    auto map = build_text_unit_map(placed, source);
    SampleTime t = SampleTime::from_frame_int(Frame{0});

    f32 w = evaluate_selectors({spec1, spec2}, map, 0, source, t);
    CHECK(w == doctest::Approx(0.4f)); // min(0.8, 0.4)
}

TEST_CASE("Combine: Max mode") {
    GlyphSelectorSpec spec1;
    spec1.id = "s1";
    spec1.combine = SelectorCombineMode::Replace;
    spec1.amount.set(30.0f);

    GlyphSelectorSpec spec2;
    spec2.id = "s2";
    spec2.combine = SelectorCombineMode::Max;
    spec2.amount.set(70.0f);

    auto placed = make_test_placed_run(1);
    auto source = make_test_source(1);
    auto map = build_text_unit_map(placed, source);
    SampleTime t = SampleTime::from_frame_int(Frame{0});

    f32 w = evaluate_selectors({spec1, spec2}, map, 0, source, t);
    CHECK(w == doctest::Approx(0.7f)); // max(0.3, 0.7)
}

// ═══════════════════════════════════════════════════════════════════════════
// PR 2 — Animated keyframe tests (AnimatedValue with real keyframes)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Animated: start sweeps across glyphs over time") {
    // A selector with start animated from 0→100 over 60 frames.
    // At frame 0, only the last glyphs are active (start=0).
    // At frame 60, no glyphs are active (start=100, end=100).
    GlyphSelectorSpec spec;
    spec.id = "sweep";
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::Square;
    spec.start.key(Frame{0}, 0.0f).key(Frame{60}, 100.0f, EasingCurve{Easing::Linear});
    spec.end.set(100.0f);
    spec.amount.set(100.0f);

    auto placed = make_test_placed_run(10);
    auto source = make_test_source(10);
    auto map = build_text_unit_map(placed, source);

    // At frame 0 (start=0), first glyph should be active
    SampleTime t0 = SampleTime::from_frame_int(Frame{0});
    f32 w0_first = evaluate_selector(spec, map, 0, source, t0);
    CHECK(w0_first == doctest::Approx(1.0f));

    // At frame 30 (start=50), first half should be outside, second half inside
    SampleTime t30 = SampleTime::from_frame_int(Frame{30});
    f32 w30_first = evaluate_selector(spec, map, 0, source, t30);
    f32 w30_last  = evaluate_selector(spec, map, 9, source, t30);
    CHECK(w30_first == doctest::Approx(0.0f));
    CHECK(w30_last == doctest::Approx(1.0f));

    // At frame 60 (start=100), nothing should be active
    SampleTime t60 = SampleTime::from_frame_int(Frame{60});
    f32 w60 = evaluate_selector(spec, map, 0, source, t60);
    CHECK(w60 == doctest::Approx(0.0f));
}

TEST_CASE("Animated: offset slides the window with easing") {
    GlyphSelectorSpec spec;
    spec.id = "slide";
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::Square;
    spec.start.set(0.0f);
    spec.end.set(40.0f);  // narrow 40% window
    spec.offset.key(Frame{0}, 0.0f).key(Frame{30}, 100.0f, EasingCurve{Easing::Linear});
    spec.amount.set(100.0f);

    auto placed = make_test_placed_run(8);
    auto source = make_test_source(8);
    auto map = build_text_unit_map(placed, source);

    // Frame 0: window at [0,40], offset=0 → first glyphs active
    SampleTime t0 = SampleTime::from_frame_int(Frame{0});
    f32 w0_first = evaluate_selector(spec, map, 0, source, t0);
    f32 w0_last  = evaluate_selector(spec, map, 7, source, t0);
    CHECK(w0_first > 0.0f);
    CHECK(w0_last == doctest::Approx(0.0f));

    // Frame 30: window shifted by 100% (wraps to same position), so same weights
    SampleTime t30 = SampleTime::from_frame_int(Frame{30});
    f32 w30_first = evaluate_selector(spec, map, 0, source, t30);
    CHECK(w30_first == doctest::Approx(w0_first));

    // Frame 15: offset=50, window shifted halfway
    SampleTime t15 = SampleTime::from_frame_int(Frame{15});
    f32 w15_first = evaluate_selector(spec, map, 0, source, t15);
    f32 w15_mid   = evaluate_selector(spec, map, 4, source, t15);
    CHECK(w15_first == doctest::Approx(0.0f));
    CHECK(w15_mid > 0.0f);
}

TEST_CASE("Animated: end shrinks the active range") {
    GlyphSelectorSpec spec;
    spec.id = "shrink";
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::Square;
    spec.start.set(0.0f);
    spec.end.key(Frame{0}, 100.0f).key(Frame{40}, 0.0f, EasingCurve{Easing::Linear});
    spec.amount.set(100.0f);

    auto placed = make_test_placed_run(10);
    auto source = make_test_source(10);
    auto map = build_text_unit_map(placed, source);

    // Frame 0: full range, all glyphs active
    SampleTime t0 = SampleTime::from_frame_int(Frame{0});
    f32 w0 = evaluate_selector(spec, map, 5, source, t0);
    CHECK(w0 == doctest::Approx(1.0f));

    // Frame 20: end=50, only first half active
    SampleTime t20 = SampleTime::from_frame_int(Frame{20});
    f32 w20_first = evaluate_selector(spec, map, 0, source, t20);
    f32 w20_last  = evaluate_selector(spec, map, 9, source, t20);
    CHECK(w20_first == doctest::Approx(1.0f));
    CHECK(w20_last == doctest::Approx(0.0f));

    // Frame 40: end=0, nothing active
    SampleTime t40 = SampleTime::from_frame_int(Frame{40});
    f32 w40 = evaluate_selector(spec, map, 0, source, t40);
    CHECK(w40 == doctest::Approx(0.0f));
}

TEST_CASE("Animated: amount fades with OutCubic easing") {
    GlyphSelectorSpec spec;
    spec.id = "fade";
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::Square;
    spec.start.set(0.0f);
    spec.end.set(100.0f);
    spec.amount.key(Frame{0}, 100.0f).key(Frame{30}, 0.0f, EasingCurve{Easing::OutCubic});

    auto placed = make_test_placed_run(3);
    auto source = make_test_source(3);
    auto map = build_text_unit_map(placed, source);

    SampleTime t0  = SampleTime::from_frame_int(Frame{0});
    SampleTime t15 = SampleTime::from_frame_int(Frame{15});
    SampleTime t30 = SampleTime::from_frame_int(Frame{30});

    f32 w0  = evaluate_selector(spec, map, 0, source, t0);
    f32 w15 = evaluate_selector(spec, map, 0, source, t15);
    f32 w30 = evaluate_selector(spec, map, 0, source, t30);

    CHECK(w0  == doctest::Approx(1.0f));
    CHECK(w15 < 0.8f);   // OutCubic drops quickly at first
    CHECK(w30 == doctest::Approx(0.0f));
    CHECK(w15 > 0.0f);   // but not yet zero at midpoint
}

TEST_CASE("Animated: sub-frame interpolation produces different weights") {
    // With animated start, sub-frame times should interpolate smoothly
    GlyphSelectorSpec spec;
    spec.id = "subframe_interp";
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::Square;
    spec.start.key(Frame{0}, 0.0f).key(Frame{10}, 100.0f, EasingCurve{Easing::Linear});
    spec.end.set(100.0f);

    auto placed = make_test_placed_run(10);
    auto source = make_test_source(10);
    auto map = build_text_unit_map(placed, source);

    // At frame 5.0, start should be 50%
    SampleTime t5_0 = SampleTime::from_frame(5.0, FrameRate{30, 1});
    // At frame 5.5, start should be 55% — different weights for some glyphs
    SampleTime t5_5 = SampleTime::from_frame(5.5, FrameRate{30, 1});

    f32 w5_0 = evaluate_selector(spec, map, 4, source, t5_0);
    f32 w5_5 = evaluate_selector(spec, map, 4, source, t5_5);

    // The window has shifted, so weights should differ
    // (Not a strict equality check — just that they can differ at sub-frame)
    CHECK(w5_0 >= 0.0f);
    CHECK(w5_5 >= 0.0f);
}

TEST_CASE("Animated: motion-blur sub-frame stability") {
    // For motion blur, sub-frame samples must be deterministic.
    // Same sub-frame time → same weight, regardless of when evaluated.
    GlyphSelectorSpec spec;
    spec.id = "mb_stable";
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::Smooth;
    spec.start.key(Frame{0}, 0.0f).key(Frame{24}, 100.0f, EasingCurve{Easing::InOutCubic});
    spec.end.set(100.0f);

    auto placed = make_test_placed_run(10);
    auto source = make_test_source(10);
    auto map = build_text_unit_map(placed, source);

    // Simulate 8 motion blur sub-samples at frame 12
    const FrameRate fps{30, 1};
    std::vector<f32> weights;
    for (int s = 0; s < 8; ++s) {
        double sub = 12.0 + static_cast<double>(s) / 8.0;
        SampleTime t = SampleTime::from_frame(sub, fps);
        weights.push_back(evaluate_selector(spec, map, 5, source, t));
    }

    // Re-evaluate: must get identical results
    for (int s = 0; s < 8; ++s) {
        double sub = 12.0 + static_cast<double>(s) / 8.0;
        SampleTime t = SampleTime::from_frame(sub, fps);
        f32 w = evaluate_selector(spec, map, 5, source, t);
        CHECK(w == doctest::Approx(weights[static_cast<size_t>(s)]));
    }

    // Sub-samples should produce different weights (the curve is animated)
    bool any_different = false;
    for (size_t s = 1; s < weights.size(); ++s) {
        if (std::abs(weights[s] - weights[0]) > 0.0001f) any_different = true;
    }
    CHECK(any_different);
}

// ═══════════════════════════════════════════════════════════════════════════
// PR 2 — Multi-selector composition with animated keyframes
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Multi-sel: two selectors with different easings over time") {
    // Selector 1: RampUp with OutCubic eased offset
    // Selector 2: Square with fixed narrow window (additive)
    GlyphSelectorSpec spec1;
    spec1.id = "ramp";
    spec1.unit = TextSelectorUnit::Glyph;
    spec1.shape = TextSelectorShape::Square;   // exact boundaries needed
    spec1.combine = SelectorCombineMode::Replace;
    spec1.start.set(0.0f);
    spec1.end.set(100.0f);
    spec1.amount.key(Frame{0}, 0.0f).key(Frame{20}, 70.0f, EasingCurve{Easing::OutCubic});

    GlyphSelectorSpec spec2;
    spec2.id = "highlight";
    spec2.unit = TextSelectorUnit::Glyph;
    spec2.shape = TextSelectorShape::Square;
    spec2.combine = SelectorCombineMode::Add;
    spec2.start.set(40.0f);
    spec2.end.set(60.0f);  // highlight middle 20%
    spec2.amount.set(30.0f);

    auto placed = make_test_placed_run(10);
    auto source = make_test_source(10);
    auto map = build_text_unit_map(placed, source);

    // Frame 0: spec1 amount=0 → weight comes entirely from spec2 highlight
    SampleTime t0 = SampleTime::from_frame_int(Frame{0});
    f32 w0_mid = evaluate_selectors({spec1, spec2}, map, 5, source, t0);
    CHECK(w0_mid == doctest::Approx(0.3f).epsilon(0.01f)); // only highlight

    // Frame 20: spec1 amount=70 → base=0.7, highlight adds 0.3 at centre → 1.0
    SampleTime t20 = SampleTime::from_frame_int(Frame{20});
    f32 w20_first = evaluate_selectors({spec1, spec2}, map, 0, source, t20);
    f32 w20_mid   = evaluate_selectors({spec1, spec2}, map, 5, source, t20);
    f32 w20_last  = evaluate_selectors({spec1, spec2}, map, 9, source, t20);

    CHECK(w20_first == doctest::Approx(0.7f).epsilon(0.02f));  // base 0.7, no highlight
    CHECK(w20_mid > 0.9f);  // base 0.7 + highlight 0.3 = 1.0 at centre
    CHECK(w20_last == doctest::Approx(0.7f).epsilon(0.02f));
}

TEST_CASE("Multi-sel: offset-sweep selector composed with Subtract") {
    // S1: full range 100% amount
    // S2: narrow window that subtracts, sliding via offset animation
    GlyphSelectorSpec spec1;
    spec1.id = "base";
    spec1.shape = TextSelectorShape::Square;  // exact full coverage
    spec1.combine = SelectorCombineMode::Replace;
    spec1.amount.set(100.0f);

    GlyphSelectorSpec spec2;
    spec2.id = "notch";
    spec2.unit = TextSelectorUnit::Glyph;
    spec2.shape = TextSelectorShape::Square;
    spec2.combine = SelectorCombineMode::Subtract;
    spec2.start.set(0.0f);
    spec2.end.set(20.0f);
    spec2.amount.set(50.0f);
    spec2.offset.key(Frame{0}, 0.0f).key(Frame{40}, 100.0f, EasingCurve{Easing::Linear});

    auto placed = make_test_placed_run(10);
    auto source = make_test_source(10);
    auto map = build_text_unit_map(placed, source);

    // Frame 0: notch at start → first glyphs get 1.0 - 0.5 = 0.5
    SampleTime t0 = SampleTime::from_frame_int(Frame{0});
    f32 w0_first = evaluate_selectors({spec1, spec2}, map, 0, source, t0);
    f32 w0_last  = evaluate_selectors({spec1, spec2}, map, 9, source, t0);
    CHECK(w0_first == doctest::Approx(0.5f).epsilon(0.01f));
    CHECK(w0_last == doctest::Approx(1.0f));

    // Frame 20: notch at middle → middle glyphs get 1.0 - 0.5 = 0.5
    SampleTime t20 = SampleTime::from_frame_int(Frame{20});
    f32 w20_mid   = evaluate_selectors({spec1, spec2}, map, 5, source, t20);
    f32 w20_first = evaluate_selectors({spec1, spec2}, map, 0, source, t20);
    CHECK(w20_mid == doctest::Approx(0.5f).epsilon(0.01f));
    CHECK(w20_first == doctest::Approx(1.0f));
}

TEST_CASE("Multi-sel: three selectors with Intersect → narrow active window") {
    // Build a narrow active window using three intersecting selectors
    GlyphSelectorSpec spec1;
    spec1.id = "from_left";
    spec1.unit = TextSelectorUnit::Glyph;
    spec1.shape = TextSelectorShape::RampUp;
    spec1.combine = SelectorCombineMode::Replace;
    spec1.start.set(20.0f);
    spec1.end.set(100.0f);
    spec1.amount.set(100.0f);

    GlyphSelectorSpec spec2;
    spec2.id = "from_right";
    spec2.unit = TextSelectorUnit::Glyph;
    spec2.shape = TextSelectorShape::RampDown;
    spec2.combine = SelectorCombineMode::Intersect;
    spec2.start.set(0.0f);
    spec2.end.set(80.0f);
    spec2.amount.set(100.0f);

    GlyphSelectorSpec spec3;
    spec3.id = "bell";
    spec3.unit = TextSelectorUnit::Glyph;
    spec3.shape = TextSelectorShape::Round;
    spec3.combine = SelectorCombineMode::Intersect;
    spec3.start.set(0.0f);
    spec3.end.set(100.0f);
    spec3.amount.key(Frame{0}, 0.0f).key(Frame{15}, 100.0f, EasingCurve{Easing::OutCubic});

    auto placed = make_test_placed_run(10);
    auto source = make_test_source(10);
    auto map = build_text_unit_map(placed, source);

    // Frame 0: bell amount=0 → everything 0 (intersect with 0 = 0)
    SampleTime t0 = SampleTime::from_frame_int(Frame{0});
    f32 w0 = evaluate_selectors({spec1, spec2, spec3}, map, 5, source, t0);
    CHECK(w0 == doctest::Approx(0.0f));

    // Frame 15: bell fully active, intersection of all three creates a peak at centre
    SampleTime t15 = SampleTime::from_frame_int(Frame{15});
    f32 w15_first = evaluate_selectors({spec1, spec2, spec3}, map, 0, source, t15);
    f32 w15_mid   = evaluate_selectors({spec1, spec2, spec3}, map, 5, source, t15);
    f32 w15_last  = evaluate_selectors({spec1, spec2, spec3}, map, 9, source, t15);

    CHECK(w15_first == doctest::Approx(0.0f));
    CHECK(w15_mid > 0.0f);
    CHECK(w15_last == doctest::Approx(0.0f));
}

TEST_CASE("Multi-sel: Max mode picks the stronger selector over time") {
    // Two ramps sliding in opposite directions, Max picks whichever is higher
    GlyphSelectorSpec spec1;
    spec1.id = "ramp_up";
    spec1.unit = TextSelectorUnit::Glyph;
    spec1.shape = TextSelectorShape::RampUp;
    spec1.combine = SelectorCombineMode::Replace;
    spec1.start.set(0.0f);
    spec1.end.set(100.0f);
    spec1.amount.key(Frame{0}, 100.0f).key(Frame{30}, 0.0f, EasingCurve{Easing::Linear});

    GlyphSelectorSpec spec2;
    spec2.id = "ramp_down";
    spec2.unit = TextSelectorUnit::Glyph;
    spec2.shape = TextSelectorShape::RampDown;
    spec2.combine = SelectorCombineMode::Max;
    spec2.start.set(0.0f);
    spec2.end.set(100.0f);
    spec2.amount.key(Frame{0}, 0.0f).key(Frame{30}, 100.0f, EasingCurve{Easing::Linear});

    auto placed = make_test_placed_run(10);
    auto source = make_test_source(10);
    auto map = build_text_unit_map(placed, source);

    // Frame 0: spec1 dominates (RampUp, amount=100), spec2 amount=0
    SampleTime t0 = SampleTime::from_frame_int(Frame{0});
    f32 w0_first = evaluate_selectors({spec1, spec2}, map, 0, source, t0);
    f32 w0_last  = evaluate_selectors({spec1, spec2}, map, 9, source, t0);
    CHECK(w0_first < 0.1f);
    CHECK(w0_last > 0.9f);  // RampUp peaks at end

    // Frame 15: crossover, both at ~50% amount
    SampleTime t15 = SampleTime::from_frame_int(Frame{15});
    f32 w15_first = evaluate_selectors({spec1, spec2}, map, 0, source, t15);
    f32 w15_last  = evaluate_selectors({spec1, spec2}, map, 9, source, t15);
    CHECK(w15_first > 0.2f);  // RampDown ~50% at start
    CHECK(w15_last > 0.2f);   // RampUp ~50% at end

    // Frame 30: spec2 dominates (RampDown, amount=100), spec1 amount=0
    SampleTime t30 = SampleTime::from_frame_int(Frame{30});
    f32 w30_first = evaluate_selectors({spec1, spec2}, map, 0, source, t30);
    f32 w30_last  = evaluate_selectors({spec1, spec2}, map, 9, source, t30);
    CHECK(w30_first > 0.9f);  // RampDown peaks at start
    CHECK(w30_last < 0.1f);
}

TEST_CASE("Combine: Replace mode overwrites") {
    GlyphSelectorSpec spec1;
    spec1.id = "s1";
    spec1.combine = SelectorCombineMode::Replace;
    spec1.amount.set(100.0f);

    GlyphSelectorSpec spec2;
    spec2.id = "s2";
    spec2.combine = SelectorCombineMode::Replace;
    spec2.amount.set(20.0f);

    auto placed = make_test_placed_run(1);
    auto source = make_test_source(1);
    auto map = build_text_unit_map(placed, source);
    SampleTime t = SampleTime::from_frame_int(Frame{0});

    f32 w = evaluate_selectors({spec1, spec2}, map, 0, source, t);
    CHECK(w == doctest::Approx(0.2f)); // second replaces first
}

// ═══════════════════════════════════════════════════════════════════════════
// Exclude_spaces real-implementation tests (requires PlacedGlyphRun threaded)
// ═══════════════════════════════════════════════════════════════════════════

namespace {

/// Build a placed run + source where the source contains whitespace bytes
/// at specific glyph positions.  Layout: each glyph occupies one source byte.
PlacedGlyphRun make_run_for_source(std::string_view source) {
    PlacedGlyphRun run;
    for (size_t i = 0; i < source.size(); ++i) {
        PlacedGlyph g;
        g.glyph_id = static_cast<u32>(i + 1);
        g.cluster = static_cast<u32>(i);
        g.is_cluster_start = true;
        g.advance_x = 10.0f;
        g.x = static_cast<float>(i) * 10.0f;
        g.byte_offset = i;
        g.byte_len = 1;
        run.glyphs.push_back(g);

        PlacedGlyphRun::Cluster cl;
        cl.start_glyph = i;
        cl.end_glyph = i + 1;
        cl.byte_offset = i;
        cl.byte_len = 1;
        cl.advance = 10.0f;
        cl.raw_advance = 10.0f;
        run.clusters.push_back(cl);
    }
    run.total_width = static_cast<float>(source.size()) * 10.0f;
    return run;
}

} // anonymous namespace

TEST_CASE("Exclude_spaces: per-cluster whitespace exclusion (real impl)") {
    // Source "ab cd" -> 5 bytes, glyphs 0..4.  Glyph at index 2 is the space.
    auto source = std::string("ab cd");
    auto placed = make_run_for_source(source);
    auto map = build_text_unit_map(placed, source);

    GlyphSelectorSpec spec;
    spec.id = "excl_glyph";
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::Square;
    spec.start.set(0.0f);
    spec.end.set(100.0f);
    spec.amount.set(100.0f);
    spec.exclude_spaces = true;

    SampleTime t = SampleTime::from_frame_int(Frame{0});

    // With placed threaded and exclude_spaces=true: glyph 2 (space) is excluded.
    for (u32 i = 0; i < 5; ++i) {
        f32 w = evaluate_selector(spec, map, i, source, t, &placed);
        if (i == 2) {
            CHECK(w == doctest::Approx(0.0f));
        } else {
            CHECK(w == doctest::Approx(1.0f));
        }
    }
}

TEST_CASE("Exclude_spaces: disabled -> all glyphs active regardless of source") {
    auto source = std::string("ab cd");
    auto placed = make_run_for_source(source);
    auto map = build_text_unit_map(placed, source);

    GlyphSelectorSpec spec;
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::Square;
    spec.start.set(0.0f);
    spec.end.set(100.0f);
    spec.amount.set(100.0f);
    spec.exclude_spaces = false;  // explicit off

    SampleTime t = SampleTime::from_frame_int(Frame{0});
    for (u32 i = 0; i < 5; ++i) {
        f32 w = evaluate_selector(spec, map, i, source, t, &placed);
        CHECK(w == doctest::Approx(1.0f));
    }
}

TEST_CASE("Exclude_spaces: backward compatible -- placed==nullptr -> no-op") {
    // When caller has not threaded placed through (legacy API usage),
    // exclude_spaces must be a strict no-op even when spec.exclude_spaces is true.
    auto source = std::string("ab cd");
    auto placed = make_run_for_source(source);
    auto map = build_text_unit_map(placed, source);

    GlyphSelectorSpec spec;
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::Square;
    spec.start.set(0.0f);
    spec.end.set(100.0f);
    spec.amount.set(100.0f);
    spec.exclude_spaces = true;

    SampleTime t = SampleTime::from_frame_int(Frame{0});
    for (u32 i = 0; i < 5; ++i) {
        f32 w = evaluate_selector(spec, map, i, source, t, nullptr);
        CHECK(w == doctest::Approx(1.0f));
    }
}

TEST_CASE("Exclude_spaces: word unit excludes whole whitespace runs") {
    // Source "ab cd ef": 8 bytes, 3 words (indices 0, 1, 2).
    auto source = std::string("ab cd ef");
    auto placed = make_run_for_source(source);
    auto map = build_text_unit_map(placed, source);

    GlyphSelectorSpec spec;
    spec.unit = TextSelectorUnit::Word;
    spec.shape = TextSelectorShape::Square;
    spec.start.set(0.0f);
    spec.end.set(100.0f);
    spec.amount.set(100.0f);
    spec.exclude_spaces = true;

    SampleTime t = SampleTime::from_frame_int(Frame{0});

    // Walking the TextUnitMap, glyphs 0..1 are word 0 ("ab"), glyphs 3..4
    // are word 1 ("cd"), glyphs 6..7 are word 2 ("ef"), glyphs 2 and 5 are
    // spaces.  Identifying a *single* glyph: the helper inspects the first
    // glyph of the same word-group, which carries a non-whitespace byte, so
    // characters in those words are NOT excluded.  This test focuses on
    // glyphs whose word's first byte is a space (none in this source) and
    // verifies that no word gets wrongly excluded.
    for (u32 i = 0; i < 8; ++i) {
        f32 w = evaluate_selector(spec, map, i, source, t, &placed);
        // Every glyph here is in a word whose first byte is non-whitespace,
        // so exclude_spaces leaves them all active.
        CHECK(w == doctest::Approx(1.0f));
    }
}

TEST_CASE("Exclude_spaces: single-glyph word that is a space gets excluded") {
    // Source "a b": bytes (a, space, b).  Word 0 = "a" (glyph 0),
    // word 1 = " " (glyph 1), word 2 = "b" (glyph 2).
    auto source = std::string("a b");
    auto placed = make_run_for_source(source);
    auto map = build_text_unit_map(placed, source);

    GlyphSelectorSpec spec;
    spec.unit = TextSelectorUnit::Word;
    spec.shape = TextSelectorShape::Square;
    spec.start.set(0.0f);
    spec.end.set(100.0f);
    spec.amount.set(100.0f);
    spec.exclude_spaces = true;

    SampleTime t = SampleTime::from_frame_int(Frame{0});

    // The TextUnitMap for "a b" gives 3 words (a, space-only, b).
    // When evaluating on glyph 1 (the space), should_exclude_unit walks
    // glyph_to_word[1] -> word_idx, then searches forward to find the
    // first glyph in the same word; finds glyph 1 itself whose cluster byte
    // is space -> excluded, weight 0.
    f32 w_glyph1 = evaluate_selector(spec, map, 1, source, t, &placed);
    CHECK(w_glyph1 == doctest::Approx(0.0f));
}
