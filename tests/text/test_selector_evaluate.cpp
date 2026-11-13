#include "glyph_selector_helpers.hpp"
#include <chronon3d/text/selector_expression.hpp>
#include <limits>
using namespace chronon3d;
using namespace test_glyph_sel;

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

    SampleTime t = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1});

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
    spec.end.set(50.0f);
    spec.offset.set(0.0f);

    auto placed = make_test_placed_run(4);
    auto source = make_test_source(4);
    auto map = build_text_unit_map(placed, source);

    SampleTime t = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1});

    f32 w0 = evaluate_selector(spec, map, 0, source, t);
    f32 w2 = evaluate_selector(spec, map, 2, source, t);
    CHECK(w0 > 0.0f);
    CHECK(w2 == doctest::Approx(0.0f));

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
    spec.amount.set(50.0f);

    auto placed = make_test_placed_run(3);
    auto source = make_test_source(3);
    auto map = build_text_unit_map(placed, source);

    SampleTime t = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1});

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

    SampleTime t = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1});

    f32 w0 = evaluate_selector(spec, map, 0, source, t);
    f32 w2 = evaluate_selector(spec, map, 2, source, t);
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

    SampleTime t = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1});

    f32 w0 = evaluate_selector(spec, map, 0, source, t);
    f32 w2 = evaluate_selector(spec, map, 2, source, t);
    CHECK(w0 > w2);
}

TEST_CASE("Evaluate: RTL grapheme units follow logical source order") {
    // HarfBuzz emits RTL glyphs in visual order: cluster byte offsets descend
    // while the glyph array grows left-to-right.  The unit map must still
    // number graphemes in source order, otherwise Forward animates the visually
    // leading glyph instead of the logically first character.
    auto placed = make_test_placed_run(4);
    for (size_t i = 0; i < placed.glyphs.size(); ++i) {
        const u32 logical_cluster = static_cast<u32>(placed.glyphs.size() - 1 - i);
        placed.glyphs[i].cluster = logical_cluster;
        placed.glyphs[i].byte_offset = logical_cluster;
    }
    const std::string source = "abcd";
    auto map = build_text_unit_map(placed, source);

    REQUIRE(map.glyph_to_grapheme == std::vector<u32>{3, 2, 1, 0});
    REQUIRE(map.grapheme_count == 4);

    GlyphSelectorSpec spec;
    spec.id = "rtl_logical_order";
    spec.unit = TextSelectorUnit::Grapheme;
    spec.shape = TextSelectorShape::RampUp;
    spec.order = TextSelectorOrder::Forward;
    spec.start.set(0.0f);
    spec.end.set(100.0f);

    const SampleTime t = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1});
    // GLYPH 3 is the first source character; GLYPH 0 is the last one.
    const f32 logical_first = evaluate_selector(spec, map, 3, source, t);
    const f32 logical_last = evaluate_selector(spec, map, 0, source, t);
    CHECK(logical_last > logical_first);

    spec.order = TextSelectorOrder::Reverse;
    const f32 reverse_first = evaluate_selector(spec, map, 3, source, t);
    const f32 reverse_last = evaluate_selector(spec, map, 0, source, t);
    CHECK(reverse_first > reverse_last);
}

TEST_CASE("Evaluate: sub-frame evaluation consistency") {
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

TEST_CASE("Evaluate: Wiggly selector is deterministic, seeded, and bounded") {
    GlyphSelectorSpec spec;
    spec.id = "wiggly";
    spec.shape = TextSelectorShape::Wiggly;
    spec.start.set(0.0f);
    spec.end.set(100.0f);
    spec.wiggly.frequency = 0.75f;
    spec.wiggly.amplitude = 0.4f;
    spec.wiggly.seed = 1234;

    auto placed = make_test_placed_run(12);
    auto source = make_test_source(12);
    auto map = build_text_unit_map(placed, source);
    const auto time = SampleTime::from_frame_int(Frame{4}, FrameRate{30, 1});

    std::vector<f32> first;
    for (u32 i = 0; i < 12; ++i) {
        const f32 weight = evaluate_selector(spec, map, i, source, time);
        CHECK(weight >= 0.0f);
        CHECK(weight <= 1.0f);
        first.push_back(weight);
    }
    for (u32 i = 0; i < 12; ++i)
        CHECK(evaluate_selector(spec, map, i, source, time) == doctest::Approx(first[i]));

    spec.wiggly.seed++;
    bool changed = false;
    for (u32 i = 0; i < 12; ++i)
        changed = changed || std::abs(evaluate_selector(spec, map, i, source, time) - first[i]) > 1e-5f;
    CHECK(changed);
}

TEST_CASE("Evaluate: Wiggly selector rejects non-finite or out-of-range parameters") {
    GlyphSelectorSpec spec;
    spec.shape = TextSelectorShape::Wiggly;
    auto placed = make_test_placed_run(2);
    auto source = make_test_source(2);
    auto map = build_text_unit_map(placed, source);
    const auto time = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1});

    spec.wiggly.frequency = 0.0f;
    CHECK_THROWS(static_cast<void>(evaluate_selector(spec, map, 0, source, time)));
    spec.wiggly.frequency = std::numeric_limits<f32>::infinity();
    CHECK_THROWS(static_cast<void>(evaluate_selector(spec, map, 0, source, time)));
    spec.wiggly.frequency = 1000.1f;
    CHECK_THROWS(static_cast<void>(evaluate_selector(spec, map, 0, source, time)));
    spec.wiggly.frequency = 1.0f;
    spec.wiggly.amplitude = 1.01f;
    CHECK_THROWS(static_cast<void>(evaluate_selector(spec, map, 0, source, time)));
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

    SampleTime t1 = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1});
    SampleTime t2 = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1});

    for (u32 i = 0; i < 5; ++i) {
        f32 w1 = evaluate_selector(spec, map, i, source, t1);
        f32 w2 = evaluate_selector(spec, map, i, source, t2);
        CHECK(w1 == doctest::Approx(w2));
    }

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
// Animated keyframe tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Animated: start sweeps across glyphs over time") {
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

    SampleTime t0 = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1});
    f32 w0_first = evaluate_selector(spec, map, 0, source, t0);
    CHECK(w0_first == doctest::Approx(1.0f));

    SampleTime t30 = SampleTime::from_frame_int(Frame{30}, FrameRate{30, 1});
    f32 w30_first = evaluate_selector(spec, map, 0, source, t30);
    f32 w30_last  = evaluate_selector(spec, map, 9, source, t30);
    CHECK(w30_first == doctest::Approx(0.0f));
    CHECK(w30_last == doctest::Approx(1.0f));

    SampleTime t60 = SampleTime::from_frame_int(Frame{60}, FrameRate{30, 1});
    f32 w60 = evaluate_selector(spec, map, 0, source, t60);
    CHECK(w60 == doctest::Approx(0.0f));
}

TEST_CASE("Animated: offset slides the window with easing") {
    GlyphSelectorSpec spec;
    spec.id = "slide";
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::Square;
    spec.start.set(0.0f);
    spec.end.set(40.0f);
    spec.offset.key(Frame{0}, 0.0f).key(Frame{30}, 100.0f, EasingCurve{Easing::Linear});
    spec.amount.set(100.0f);

    auto placed = make_test_placed_run(8);
    auto source = make_test_source(8);
    auto map = build_text_unit_map(placed, source);

    SampleTime t0 = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1});
    f32 w0_first = evaluate_selector(spec, map, 0, source, t0);
    f32 w0_last  = evaluate_selector(spec, map, 7, source, t0);
    CHECK(w0_first > 0.0f);
    CHECK(w0_last == doctest::Approx(0.0f));

    SampleTime t30 = SampleTime::from_frame_int(Frame{30}, FrameRate{30, 1});
    f32 w30_first = evaluate_selector(spec, map, 0, source, t30);
    CHECK(w30_first == doctest::Approx(w0_first));

    SampleTime t15 = SampleTime::from_frame_int(Frame{15}, FrameRate{30, 1});
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

    SampleTime t0 = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1});
    f32 w0 = evaluate_selector(spec, map, 5, source, t0);
    CHECK(w0 == doctest::Approx(1.0f));

    SampleTime t20 = SampleTime::from_frame_int(Frame{20}, FrameRate{30, 1});
    f32 w20_first = evaluate_selector(spec, map, 0, source, t20);
    f32 w20_last  = evaluate_selector(spec, map, 9, source, t20);
    CHECK(w20_first == doctest::Approx(1.0f));
    CHECK(w20_last == doctest::Approx(0.0f));

    SampleTime t40 = SampleTime::from_frame_int(Frame{40}, FrameRate{30, 1});
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

    SampleTime t0  = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1});
    SampleTime t15 = SampleTime::from_frame_int(Frame{15}, FrameRate{30, 1});
    SampleTime t30 = SampleTime::from_frame_int(Frame{30}, FrameRate{30, 1});

    f32 w0  = evaluate_selector(spec, map, 0, source, t0);
    f32 w15 = evaluate_selector(spec, map, 0, source, t15);
    f32 w30 = evaluate_selector(spec, map, 0, source, t30);

    CHECK(w0  == doctest::Approx(1.0f));
    CHECK(w15 < 0.8f);
    CHECK(w30 == doctest::Approx(0.0f));
    CHECK(w15 > 0.0f);
}

TEST_CASE("Animated: sub-frame interpolation produces different weights") {
    GlyphSelectorSpec spec;
    spec.id = "subframe_interp";
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::Square;
    spec.start.key(Frame{0}, 0.0f).key(Frame{10}, 100.0f, EasingCurve{Easing::Linear});
    spec.end.set(100.0f);

    auto placed = make_test_placed_run(10);
    auto source = make_test_source(10);
    auto map = build_text_unit_map(placed, source);

    SampleTime t5_0 = SampleTime::from_frame(5.0, FrameRate{30, 1});
    SampleTime t5_5 = SampleTime::from_frame(5.5, FrameRate{30, 1});

    f32 w5_0 = evaluate_selector(spec, map, 4, source, t5_0);
    f32 w5_5 = evaluate_selector(spec, map, 4, source, t5_5);

    CHECK(w5_0 >= 0.0f);
    CHECK(w5_5 >= 0.0f);
}

TEST_CASE("Animated: motion-blur sub-frame stability") {
    GlyphSelectorSpec spec;
    spec.id = "mb_stable";
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::Smooth;
    spec.start.key(Frame{0}, 0.0f).key(Frame{24}, 100.0f, EasingCurve{Easing::InOutCubic});
    spec.end.set(100.0f);

    auto placed = make_test_placed_run(10);
    auto source = make_test_source(10);
    auto map = build_text_unit_map(placed, source);

    const FrameRate fps{30, 1};
    std::vector<f32> weights;
    for (int s = 0; s < 8; ++s) {
        double sub = 12.0 + static_cast<double>(s) / 8.0;
        SampleTime t = SampleTime::from_frame(sub, fps);
        weights.push_back(evaluate_selector(spec, map, 5, source, t));
    }

    for (int s = 0; s < 8; ++s) {
        double sub = 12.0 + static_cast<double>(s) / 8.0;
        SampleTime t = SampleTime::from_frame(sub, fps);
        f32 w = evaluate_selector(spec, map, 5, source, t);
        CHECK(w == doctest::Approx(weights[static_cast<size_t>(s)]));
    }

    bool any_different = false;
    for (size_t s = 1; s < weights.size(); ++s) {
        if (std::abs(weights[s] - weights[0]) > 0.0001f) any_different = true;
    }
    CHECK(any_different);
}

TEST_CASE("Runtime selector binding owns animated window state") {
    GlyphSelectorSpec spec;
    spec.id = "binding";
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::Smooth;
    spec.order = TextSelectorOrder::Reverse;
    spec.start.key(Frame{0}, 0.0f).key(Frame{30}, 100.0f,
                                      EasingCurve{Easing::Linear});
    spec.end.set(100.0f);
    spec.offset.set(12.0f);
    spec.amount.set(75.0f);

    auto placed = make_test_placed_run(4);
    auto source = make_test_source(4);
    auto map = build_text_unit_map(placed, source);
    auto compiled = compile_selector(spec, map, source, &placed);
    const auto time = SampleTime::from_frame_int(Frame{15}, FrameRate{30, 1});

    const auto run_params = bind_selector_runtime(compiled, map, time);
    CHECK(run_params.shape == TextSelectorShape::Smooth);
    CHECK(run_params.start == doctest::Approx(50.0f));
    CHECK(run_params.end == doctest::Approx(100.0f));
    CHECK(run_params.offset == doctest::Approx(12.0f));
    CHECK(run_params.amount == doctest::Approx(75.0f));
    CHECK(run_params.total_units == 4u);
    CHECK(run_params.enabled);
    CHECK_FALSE(run_params.window_completed);

    for (u32 glyph = 0; glyph < 4; ++glyph) {
        const auto glyph_params = bind_selector_runtime(
            compiled, map, glyph, time);
        CHECK(glyph_params.use_ordered_index);
        CHECK(selector_weight(compiled, map, glyph, time) ==
              doctest::Approx(selector_weight(glyph_params, glyph)));
        CHECK(selector_weight(
                  run_params, compiled, map, glyph) ==
              doctest::Approx(selector_weight(compiled, map, glyph, time)));
    }

    spec.start.set(100.0f);
    spec.end.set(100.0f);
    compiled = compile_selector(spec, map, source, &placed);
    const auto terminal = bind_selector_runtime(compiled, map, time);
    CHECK(terminal.window_completed);
    CHECK(selector_weight(terminal, 0u) == doctest::Approx(0.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// AE Range Selector: Units = Index + Smoothness
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Range Units: index selects exact logical units") {
    GlyphSelectorSpec spec;
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::Square;
    spec.range_units = TextSelectorRangeUnits::Index;
    spec.start.set(0.0f);
    spec.end.set(3.0f);

    auto placed = make_test_placed_run(10);
    auto source = make_test_source(10);
    auto map = build_text_unit_map(placed, source);
    const auto time = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1});

    CHECK(evaluate_selector(spec, map, 0, source, time) == doctest::Approx(1.0f));
    CHECK(evaluate_selector(spec, map, 1, source, time) == doctest::Approx(1.0f));
    CHECK(evaluate_selector(spec, map, 2, source, time) == doctest::Approx(1.0f));
    CHECK(evaluate_selector(spec, map, 3, source, time) == doctest::Approx(0.0f));
    CHECK(evaluate_selector(spec, map, 9, source, time) == doctest::Approx(0.0f));

    const auto compiled = compile_selector(spec, map, source, &placed);
    const auto bound = bind_selector_runtime(compiled, map, time);
    CHECK(bound.start == doctest::Approx(0.0f));
    CHECK(bound.end == doctest::Approx(30.0f));
}

TEST_CASE("Range Units: index offset shifts the window by units") {
    GlyphSelectorSpec spec;
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::Square;
    spec.range_units = TextSelectorRangeUnits::Index;
    spec.start.set(0.0f);
    spec.end.set(3.0f);
    spec.offset.set(3.0f);

    auto placed = make_test_placed_run(10);
    auto source = make_test_source(10);
    auto map = build_text_unit_map(placed, source);
    const auto time = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1});

    CHECK(evaluate_selector(spec, map, 0, source, time) == doctest::Approx(0.0f));
    CHECK(evaluate_selector(spec, map, 2, source, time) == doctest::Approx(0.0f));
    CHECK(evaluate_selector(spec, map, 3, source, time) == doctest::Approx(1.0f));
    CHECK(evaluate_selector(spec, map, 5, source, time) == doctest::Approx(1.0f));
    CHECK(evaluate_selector(spec, map, 6, source, time) == doctest::Approx(0.0f));
}

TEST_CASE("Range Smoothness: feathers square edges without changing the hard default") {
    GlyphSelectorSpec spec;
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::Square;
    spec.start.set(0.0f);
    spec.end.set(100.0f);
    spec.smoothness = 50.0f;

    auto placed = make_test_placed_run(11);
    auto source = make_test_source(11);
    auto map = build_text_unit_map(placed, source);
    const auto time = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1});

    const f32 first = evaluate_selector(spec, map, 0, source, time);
    const f32 middle = evaluate_selector(spec, map, 5, source, time);
    const f32 last = evaluate_selector(spec, map, 10, source, time);
    CHECK(first < 0.1f);
    CHECK(middle > 0.99f);
    CHECK(last < 0.1f);
    CHECK(first == doctest::Approx(last).epsilon(0.001f));

    spec.smoothness = 100.0f;
    CHECK(evaluate_selector(spec, map, 5, source, time) ==
          doctest::Approx(0.5f).epsilon(0.01f));

    spec.smoothness = 0.0f;
    CHECK(evaluate_selector(spec, map, 0, source, time) == doctest::Approx(1.0f));
    CHECK(evaluate_selector(spec, map, 10, source, time) == doctest::Approx(1.0f));
}

TEST_CASE("Range Smoothness: bounded, symmetric, and monotone contract") {
    GlyphSelectorSpec spec;
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::Square;
    spec.start.set(10.0f);
    spec.end.set(90.0f);

    auto placed = make_test_placed_run(41);
    auto source = make_test_source(41);
    auto map = build_text_unit_map(placed, source);
    const auto time = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1});

    for (const f32 smoothness : {0.0f, 25.0f, 50.0f, 100.0f}) {
        spec.smoothness = smoothness;
        std::vector<f32> weights;
        weights.reserve(41);
        for (u32 i = 0; i < 41; ++i)
            weights.push_back(evaluate_selector(spec, map, i, source, time));

        for (const f32 weight : weights) {
            CHECK(std::isfinite(weight));
            CHECK(weight >= 0.0f);
            CHECK(weight <= 1.0f);
        }
        for (u32 i = 0; i < 41; ++i)
            CHECK(weights[i] == doctest::Approx(weights[40 - i]).epsilon(0.0001f));

        if (smoothness == 0.0f) {
            CHECK(weights[0] == doctest::Approx(0.0f));
            CHECK(weights[20] == doctest::Approx(1.0f));
            CHECK(weights[40] == doctest::Approx(0.0f));
        } else {
            CHECK(weights[0] == doctest::Approx(0.0f).epsilon(0.0001f));
            CHECK(weights[40] == doctest::Approx(0.0f).epsilon(0.0001f));
            for (u32 i = 1; i <= 20; ++i)
                CHECK(weights[i] >= weights[i - 1] - 1e-6f);
            for (u32 i = 21; i < 41; ++i)
                CHECK(weights[i] <= weights[i - 1] + 1e-6f);
        }
    }

    spec.smoothness = 100.0f;
    CHECK(evaluate_selector(spec, map, 20, source, time) ==
          doctest::Approx(0.5f).epsilon(0.01f));
}

// ═══════════════════════════════════════════════════════════════════════════
// AE Wiggly Selector: temporal controls
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Wiggly: Wiggles/Second animates in time and zero freezes it") {
    GlyphSelectorSpec spec;
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::Wiggly;
    spec.start.set(0.0f);
    spec.end.set(100.0f);
    spec.wiggly.frequency = 1.0f;
    spec.wiggly.amplitude = 1.0f;
    spec.wiggly.seed = 11;
    spec.wiggly.wiggles_per_second = 3.0f;

    auto placed = make_test_placed_run(8);
    auto source = make_test_source(8);
    auto map = build_text_unit_map(placed, source);
    const auto t0 = SampleTime::from_seconds(0.0, FrameRate{30, 1});
    const auto t1 = SampleTime::from_seconds(0.5, FrameRate{30, 1});

    bool any_changed = false;
    for (u32 i = 0; i < 8; ++i) {
        const f32 a = evaluate_selector(spec, map, i, source, t0);
        const f32 b = evaluate_selector(spec, map, i, source, t1);
        any_changed = any_changed || std::abs(a - b) > 1e-4f;
    }
    CHECK(any_changed);

    spec.wiggly.wiggles_per_second = 0.0f;
    for (u32 i = 0; i < 8; ++i) {
        const f32 a = evaluate_selector(spec, map, i, source, t0);
        const f32 b = evaluate_selector(spec, map, i, source, t1);
        CHECK(a == doctest::Approx(b));
    }
}

TEST_CASE("Wiggly: temporal and spatial phases shift the noise lattice") {
    GlyphSelectorSpec base;
    base.unit = TextSelectorUnit::Glyph;
    base.shape = TextSelectorShape::Wiggly;
    base.start.set(0.0f);
    base.end.set(100.0f);
    base.wiggly.frequency = 1.0f;
    base.wiggly.amplitude = 1.0f;
    base.wiggly.seed = 21;
    base.wiggly.wiggles_per_second = 1.0f;

    auto placed = make_test_placed_run(12);
    auto source = make_test_source(12);
    auto map = build_text_unit_map(placed, source);
    const auto time = SampleTime::from_seconds(0.25, FrameRate{30, 1});

    bool temporal_changed = false;
    auto temporal = base;
    temporal.wiggly.temporal_phase = 180.0f;
    for (u32 i = 0; i < 12; ++i) {
        const f32 a = evaluate_selector(base, map, i, source, time);
        const f32 b = evaluate_selector(temporal, map, i, source, time);
        temporal_changed = temporal_changed || std::abs(a - b) > 1e-4f;
    }
    CHECK(temporal_changed);

    bool spatial_changed = false;
    auto spatial = base;
    spatial.wiggly.spatial_phase = 180.0f;
    for (u32 i = 0; i < 12; ++i) {
        const f32 a = evaluate_selector(base, map, i, source, time);
        const f32 b = evaluate_selector(spatial, map, i, source, time);
        spatial_changed = spatial_changed || std::abs(a - b) > 1e-4f;
    }
    CHECK(spatial_changed);
}

TEST_CASE("Wiggly: correlation 100 shares one spatial sample across units") {
    GlyphSelectorSpec spec;
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::Wiggly;
    spec.start.set(0.0f);
    spec.end.set(100.0f);
    spec.wiggly.frequency = 1.0f;
    spec.wiggly.amplitude = 1.0f;
    spec.wiggly.seed = 5;
    spec.wiggly.wiggles_per_second = 2.0f;
    spec.wiggly.correlation = 100.0f;

    auto placed = make_test_placed_run(8);
    auto source = make_test_source(8);
    auto map = build_text_unit_map(placed, source);
    const auto t0 = SampleTime::from_seconds(0.0, FrameRate{30, 1});
    const auto t1 = SampleTime::from_seconds(0.37, FrameRate{30, 1});

    const f32 a0 = evaluate_selector(spec, map, 2, source, t0);
    const f32 b0 = evaluate_selector(spec, map, 5, source, t0);
    const f32 a1 = evaluate_selector(spec, map, 2, source, t1);
    const f32 b1 = evaluate_selector(spec, map, 5, source, t1);
    REQUIRE(b0 > 1e-5f);
    REQUIRE(b1 > 1e-5f);
    CHECK(a0 / b0 == doctest::Approx(a1 / b1).epsilon(0.002f));

    auto independent = spec;
    independent.wiggly.correlation = 0.0f;
    bool differs = false;
    for (u32 i = 0; i < 8; ++i) {
        const f32 shared = evaluate_selector(spec, map, i, source, t0);
        const f32 own = evaluate_selector(independent, map, i, source, t0);
        differs = differs || std::abs(shared - own) > 1e-4f;
    }
    CHECK(differs);
}

TEST_CASE("Wiggly: min/max amount bounds the modulation") {
    GlyphSelectorSpec spec;
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::Wiggly;
    spec.start.set(0.0f);
    spec.end.set(100.0f);
    spec.wiggly.frequency = 1.0f;
    spec.wiggly.amplitude = 1.0f;
    spec.wiggly.seed = 17;
    spec.wiggly.correlation = 0.0f;
    spec.wiggly.min_amount = 20.0f;
    spec.wiggly.max_amount = 80.0f;

    auto placed = make_test_placed_run(16);
    auto source = make_test_source(16);
    auto map = build_text_unit_map(placed, source);
    const auto time = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1});

    f32 maximum = 0.0f;
    for (u32 i = 0; i < 16; ++i) {
        const f32 weight = evaluate_selector(spec, map, i, source, time);
        CHECK(weight >= 0.0f);
        CHECK(weight <= 0.8001f);
        maximum = std::max(maximum, weight);
    }
    CHECK(maximum > 0.2f);
}

TEST_CASE("Wiggly: Lock Dimensions selects one channel or three averaged channels") {
    GlyphSelectorSpec locked;
    locked.unit = TextSelectorUnit::Glyph;
    locked.shape = TextSelectorShape::Wiggly;
    locked.start.set(0.0f);
    locked.end.set(100.0f);
    locked.wiggly.frequency = 1.0f;
    locked.wiggly.amplitude = 1.0f;
    locked.wiggly.seed = 33;
    locked.wiggly.lock_dimensions = true;

    auto unlocked = locked;
    unlocked.wiggly.lock_dimensions = false;

    auto placed = make_test_placed_run(12);
    auto source = make_test_source(12);
    auto map = build_text_unit_map(placed, source);
    const auto time = SampleTime::from_frame_int(Frame{4}, FrameRate{30, 1});

    bool differs = false;
    for (u32 i = 0; i < 12; ++i) {
        const f32 a = evaluate_selector(locked, map, i, source, time);
        const f32 b = evaluate_selector(unlocked, map, i, source, time);
        CHECK(a >= 0.0f);
        CHECK(a <= 1.0f);
        CHECK(b >= 0.0f);
        CHECK(b <= 1.0f);
        differs = differs || std::abs(a - b) > 1e-4f;
    }
    CHECK(differs);
}

TEST_CASE("Wiggly: mode selects smooth, corner, or jagged interpolation") {
    GlyphSelectorSpec smooth;
    smooth.unit = TextSelectorUnit::Glyph;
    smooth.shape = TextSelectorShape::Wiggly;
    smooth.start.set(0.0f);
    smooth.end.set(100.0f);
    smooth.wiggly.frequency = 0.37f;
    smooth.wiggly.amplitude = 1.0f;
    smooth.wiggly.seed = 44;

    auto corner = smooth;
    corner.wiggly.mode = WigglySelectorMode::Corner;
    auto jagged = smooth;
    jagged.wiggly.mode = WigglySelectorMode::Jagged;

    auto placed = make_test_placed_run(10);
    auto source = make_test_source(10);
    auto map = build_text_unit_map(placed, source);
    const auto time = SampleTime::from_frame_int(Frame{2}, FrameRate{30, 1});

    bool differs = false;
    for (u32 i = 0; i < 10; ++i) {
        const f32 a = evaluate_selector(smooth, map, i, source, time);
        const f32 b = evaluate_selector(corner, map, i, source, time);
        CHECK(evaluate_selector(jagged, map, i, source, time) ==
              doctest::Approx(evaluate_selector(jagged, map, i, source, time)));
        differs = differs || std::abs(a - b) > 1e-4f;
    }
    CHECK(differs);
}

// ═══════════════════════════════════════════════════════════════════════════
// AE Expression Selector
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Expression: textIndex, textTotal, selectorValue, time, and clamping") {
    GlyphSelectorSpec spec;
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::Square;
    spec.start.set(0.0f);
    spec.end.set(100.0f);

    auto placed = make_test_placed_run(4);
    auto source = make_test_source(4);
    auto map = build_text_unit_map(placed, source);
    const auto time = SampleTime::from_seconds(0.0, FrameRate{30, 1});

    spec.expression = "100 * textIndex / textTotal";
    for (u32 i = 0; i < 4; ++i) {
        CHECK(evaluate_selector(spec, map, i, source, time) ==
              doctest::Approx(static_cast<f32>(i) * 0.25f));
    }

    spec.expression = "selectorValue";
    for (u32 i = 0; i < 4; ++i)
        CHECK(evaluate_selector(spec, map, i, source, time) == doctest::Approx(1.0f));

    spec.expression = "selectorValue * 0.5";
    for (u32 i = 0; i < 4; ++i)
        CHECK(evaluate_selector(spec, map, i, source, time) == doctest::Approx(0.5f));

    spec.expression = "textIndex == 3 ? 100 : 0";
    CHECK(evaluate_selector(spec, map, 0, source, time) == doctest::Approx(0.0f));
    CHECK(evaluate_selector(spec, map, 3, source, time) == doctest::Approx(1.0f));

    spec.expression = "50 + 50 * sin(time * 6.283185307179586)";
    const auto quarter = SampleTime::from_seconds(0.25, FrameRate{30, 1});
    CHECK(evaluate_selector(spec, map, 0, source, time) == doctest::Approx(0.5f));
    CHECK(evaluate_selector(spec, map, 0, source, quarter) == doctest::Approx(1.0f));

    spec.expression = "-50";
    CHECK(evaluate_selector(spec, map, 0, source, time) == doctest::Approx(0.0f));
    spec.expression = "500";
    CHECK(evaluate_selector(spec, map, 0, source, time) == doctest::Approx(1.0f));
    spec.expression = "not_a_real_variable";
    CHECK_THROWS(static_cast<void>(evaluate_selector(spec, map, 0, source, time)));
}

TEST_CASE("Expression: textIndex stays the logical index under Reverse order") {
    GlyphSelectorSpec spec;
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::Square;
    spec.order = TextSelectorOrder::Reverse;
    spec.start.set(0.0f);
    spec.end.set(100.0f);
    spec.expression = "100 * textIndex / textTotal";

    auto placed = make_test_placed_run(4);
    auto source = make_test_source(4);
    auto map = build_text_unit_map(placed, source);
    const auto time = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1});

    CHECK(evaluate_selector(spec, map, 0, source, time) == doctest::Approx(0.0f));
    CHECK(evaluate_selector(spec, map, 3, source, time) == doctest::Approx(0.75f));
}

TEST_CASE("Expression profile: closed selector runtime rejects AE host access") {
    CHECK(validate_selector_expression("100 * textIndex / textTotal"));
    CHECK(validate_selector_expression("selectorValue * 0.5"));
    CHECK(validate_selector_expression("clamp(50 + 50 * sin(time), 0, 100)"));
    CHECK(validate_selector_expression("linear(time, 0, 1, 0, 100)"));
    CHECK(validate_selector_expression("wiggle(1, 10)"));
    CHECK_FALSE(validate_selector_expression("thisComp.width"));
    CHECK_FALSE(validate_selector_expression("thisLayer.opacity"));
    CHECK_FALSE(validate_selector_expression("layer(\"bg\").opacity"));
    CHECK_FALSE(validate_selector_expression("loopOut(\"cycle\")"));
    CHECK_FALSE(validate_selector_expression("valueAtTime(0)"));
    CHECK_FALSE(validate_selector_expression("toComp([0, 0])"));
    CHECK_FALSE(validate_selector_expression("posterizeTime(12)"));
    CHECK_FALSE(validate_selector_expression("not_a_real_variable"));

    GlyphSelectorSpec spec;
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::Square;
    auto placed = make_test_placed_run(2);
    auto source = make_test_source(2);
    auto map = build_text_unit_map(placed, source);

    for (const char* expression : {
             "thisComp.width", "thisLayer.opacity", "layer(\"bg\").opacity",
             "loopOut(\"cycle\")", "valueAtTime(0)", "toComp([0, 0])",
             "posterizeTime(12)", "not_a_real_variable"}) {
        spec.expression = expression;
        CHECK_THROWS_AS(compile_selector(spec, map, source, &placed),
                        std::invalid_argument);
    }

    spec.expression = "textIndex == 0 ? 100 : 0";
    CHECK_NOTHROW(compile_selector(spec, map, source, &placed));
}

TEST_CASE("Selector compile rejects invalid AE selector parameters") {
    GlyphSelectorSpec spec;
    spec.unit = TextSelectorUnit::Glyph;
    spec.shape = TextSelectorShape::Square;

    auto placed = make_test_placed_run(2);
    auto source = make_test_source(2);
    auto map = build_text_unit_map(placed, source);
    const auto time = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1});

    spec.smoothness = 101.0f;
    CHECK_THROWS(static_cast<void>(evaluate_selector(spec, map, 0, source, time)));

    spec.smoothness = 0.0f;
    spec.shape = TextSelectorShape::Wiggly;
    spec.wiggly.min_amount = 90.0f;
    spec.wiggly.max_amount = 20.0f;
    CHECK_THROWS(static_cast<void>(evaluate_selector(spec, map, 0, source, time)));

    spec.wiggly.min_amount = 0.0f;
    spec.wiggly.max_amount = 100.0f;
    spec.wiggly.correlation = 101.0f;
    CHECK_THROWS(static_cast<void>(evaluate_selector(spec, map, 0, source, time)));

    spec.wiggly.correlation = 0.0f;
    spec.wiggly.wiggles_per_second = 1001.0f;
    CHECK_THROWS(static_cast<void>(evaluate_selector(spec, map, 0, source, time)));

    spec.wiggly.wiggles_per_second = 0.0f;
    spec.shape = TextSelectorShape::Square;
    spec.expression = std::string{};
    CHECK_THROWS(static_cast<void>(evaluate_selector(spec, map, 0, source, time)));

    spec.expression = std::string(4097, '1');
    CHECK_THROWS(static_cast<void>(evaluate_selector(spec, map, 0, source, time)));

    spec.expression.reset();
    spec.range_units = static_cast<TextSelectorRangeUnits>(9);
    CHECK_THROWS(static_cast<void>(evaluate_selector(spec, map, 0, source, time)));
}
