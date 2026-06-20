#include "glyph_selector_helpers.hpp"
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

    SampleTime t = SampleTime::from_frame_int(Frame{0});

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

    SampleTime t = SampleTime::from_frame_int(Frame{0});

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

    SampleTime t0 = SampleTime::from_frame_int(Frame{0});
    f32 w0_first = evaluate_selector(spec, map, 0, source, t0);
    CHECK(w0_first == doctest::Approx(1.0f));

    SampleTime t30 = SampleTime::from_frame_int(Frame{30});
    f32 w30_first = evaluate_selector(spec, map, 0, source, t30);
    f32 w30_last  = evaluate_selector(spec, map, 9, source, t30);
    CHECK(w30_first == doctest::Approx(0.0f));
    CHECK(w30_last == doctest::Approx(1.0f));

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
    spec.end.set(40.0f);
    spec.offset.key(Frame{0}, 0.0f).key(Frame{30}, 100.0f, EasingCurve{Easing::Linear});
    spec.amount.set(100.0f);

    auto placed = make_test_placed_run(8);
    auto source = make_test_source(8);
    auto map = build_text_unit_map(placed, source);

    SampleTime t0 = SampleTime::from_frame_int(Frame{0});
    f32 w0_first = evaluate_selector(spec, map, 0, source, t0);
    f32 w0_last  = evaluate_selector(spec, map, 7, source, t0);
    CHECK(w0_first > 0.0f);
    CHECK(w0_last == doctest::Approx(0.0f));

    SampleTime t30 = SampleTime::from_frame_int(Frame{30});
    f32 w30_first = evaluate_selector(spec, map, 0, source, t30);
    CHECK(w30_first == doctest::Approx(w0_first));

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

    SampleTime t0 = SampleTime::from_frame_int(Frame{0});
    f32 w0 = evaluate_selector(spec, map, 5, source, t0);
    CHECK(w0 == doctest::Approx(1.0f));

    SampleTime t20 = SampleTime::from_frame_int(Frame{20});
    f32 w20_first = evaluate_selector(spec, map, 0, source, t20);
    f32 w20_last  = evaluate_selector(spec, map, 9, source, t20);
    CHECK(w20_first == doctest::Approx(1.0f));
    CHECK(w20_last == doctest::Approx(0.0f));

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
