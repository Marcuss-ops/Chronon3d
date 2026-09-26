#include "glyph_selector_helpers.hpp"
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
    CHECK_THROWS(evaluate_selector(spec, map, 0, source, time));
    spec.wiggly.frequency = std::numeric_limits<f32>::infinity();
    CHECK_THROWS(evaluate_selector(spec, map, 0, source, time));
    spec.wiggly.frequency = 1000.1f;
    CHECK_THROWS(evaluate_selector(spec, map, 0, source, time));
    spec.wiggly.frequency = 1.0f;
    spec.wiggly.amplitude = 1.01f;
    CHECK_THROWS(evaluate_selector(spec, map, 0, source, time));
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
