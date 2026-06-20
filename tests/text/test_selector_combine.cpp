#include "glyph_selector_helpers.hpp"
using namespace chronon3d;
using namespace test_glyph_sel;

// ═══════════════════════════════════════════════════════════════════════════
// Selector combination tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Combine: Add mode sums weights") {
    GlyphSelectorSpec spec1;
    spec1.id = "s1";
    spec1.combine = SelectorCombineMode::Replace;
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
    CHECK(w == doctest::Approx(0.75f));
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
    CHECK(w == doctest::Approx(0.7f));
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
    CHECK(w == doctest::Approx(0.4f));
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
    CHECK(w == doctest::Approx(0.7f));
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
    CHECK(w == doctest::Approx(0.2f));
}

// ═══════════════════════════════════════════════════════════════════════════
// Multi-selector composition with animated keyframes
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Multi-sel: two selectors with different easings over time") {
    GlyphSelectorSpec spec1;
    spec1.id = "ramp";
    spec1.unit = TextSelectorUnit::Glyph;
    spec1.shape = TextSelectorShape::Square;
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
    spec2.end.set(60.0f);
    spec2.amount.set(30.0f);

    auto placed = make_test_placed_run(10);
    auto source = make_test_source(10);
    auto map = build_text_unit_map(placed, source);

    SampleTime t0 = SampleTime::from_frame_int(Frame{0});
    f32 w0_mid = evaluate_selectors({spec1, spec2}, map, 5, source, t0);
    CHECK(w0_mid == doctest::Approx(0.3f).epsilon(0.01f));

    SampleTime t20 = SampleTime::from_frame_int(Frame{20});
    f32 w20_first = evaluate_selectors({spec1, spec2}, map, 0, source, t20);
    f32 w20_mid   = evaluate_selectors({spec1, spec2}, map, 5, source, t20);
    f32 w20_last  = evaluate_selectors({spec1, spec2}, map, 9, source, t20);

    CHECK(w20_first == doctest::Approx(0.7f).epsilon(0.02f));
    CHECK(w20_mid > 0.9f);
    CHECK(w20_last == doctest::Approx(0.7f).epsilon(0.02f));
}

TEST_CASE("Multi-sel: offset-sweep selector composed with Subtract") {
    GlyphSelectorSpec spec1;
    spec1.id = "base";
    spec1.shape = TextSelectorShape::Square;
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

    SampleTime t0 = SampleTime::from_frame_int(Frame{0});
    f32 w0_first = evaluate_selectors({spec1, spec2}, map, 0, source, t0);
    f32 w0_last  = evaluate_selectors({spec1, spec2}, map, 9, source, t0);
    CHECK(w0_first == doctest::Approx(0.5f).epsilon(0.01f));
    CHECK(w0_last == doctest::Approx(1.0f));

    SampleTime t20 = SampleTime::from_frame_int(Frame{20});
    f32 w20_mid   = evaluate_selectors({spec1, spec2}, map, 5, source, t20);
    f32 w20_first = evaluate_selectors({spec1, spec2}, map, 0, source, t20);
    CHECK(w20_mid == doctest::Approx(0.5f).epsilon(0.01f));
    CHECK(w20_first == doctest::Approx(1.0f));
}

TEST_CASE("Multi-sel: three selectors with Intersect → narrow active window") {
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

    SampleTime t0 = SampleTime::from_frame_int(Frame{0});
    f32 w0 = evaluate_selectors({spec1, spec2, spec3}, map, 5, source, t0);
    CHECK(w0 == doctest::Approx(0.0f));

    SampleTime t15 = SampleTime::from_frame_int(Frame{15});
    f32 w15_first = evaluate_selectors({spec1, spec2, spec3}, map, 0, source, t15);
    f32 w15_mid   = evaluate_selectors({spec1, spec2, spec3}, map, 5, source, t15);
    f32 w15_last  = evaluate_selectors({spec1, spec2, spec3}, map, 9, source, t15);

    CHECK(w15_first == doctest::Approx(0.0f));
    CHECK(w15_mid > 0.0f);
    CHECK(w15_last == doctest::Approx(0.0f));
}

TEST_CASE("Multi-sel: Max mode picks the stronger selector over time") {
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

    SampleTime t0 = SampleTime::from_frame_int(Frame{0});
    f32 w0_first = evaluate_selectors({spec1, spec2}, map, 0, source, t0);
    f32 w0_last  = evaluate_selectors({spec1, spec2}, map, 9, source, t0);
    CHECK(w0_first < 0.1f);
    CHECK(w0_last > 0.9f);

    SampleTime t15 = SampleTime::from_frame_int(Frame{15});
    f32 w15_first = evaluate_selectors({spec1, spec2}, map, 0, source, t15);
    f32 w15_last  = evaluate_selectors({spec1, spec2}, map, 9, source, t15);
    CHECK(w15_first > 0.2f);
    CHECK(w15_last > 0.2f);

    SampleTime t30 = SampleTime::from_frame_int(Frame{30});
    f32 w30_first = evaluate_selectors({spec1, spec2}, map, 0, source, t30);
    f32 w30_last  = evaluate_selectors({spec1, spec2}, map, 9, source, t30);
    CHECK(w30_first > 0.9f);
    CHECK(w30_last < 0.1f);
}
