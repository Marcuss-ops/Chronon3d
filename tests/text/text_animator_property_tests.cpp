#include <doctest/doctest.h>
#include <chronon3d/text/text_animator_property.hpp>

using namespace chronon3d;

// ── Helpers ────────────────────────────────────────────────────────────────

namespace {

PlacedGlyphRun make_test_placed_run_prop(size_t count) {
    PlacedGlyphRun run;
    for (size_t i = 0; i < count; ++i) {
        PlacedGlyph g;
        g.glyph_id = static_cast<u32>(i + 1);
        g.cluster = static_cast<u32>(i);
        g.is_cluster_start = true;
        g.advance_x = 10.0f;
        g.x = static_cast<float>(i) * 10.0f;
        g.y = 0.0f;
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
    run.total_width = static_cast<float>(count) * 10.0f;
    run.total_height = 16.0f;
    run.ascent = 12.0f;
    run.descent = 4.0f;
    run.baseline = 12.0f;
    run.font_size = 16.0f;
    return run;
}

std::string make_source_prop(size_t count) {
    std::string s;
    s.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        s.push_back(static_cast<char>('a' + (i % 26)));
    }
    return s;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// Initial state factory tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Property: make_initial_glyph_states creates correct count") {
    auto placed = make_test_placed_run_prop(5);
    auto states = make_initial_glyph_states(placed);
    CHECK(states.size() == 5);
}

TEST_CASE("Property: initial states have identity values") {
    auto placed = make_test_placed_run_prop(3);
    auto states = make_initial_glyph_states(placed);

    for (const auto& gs : states) {
        CHECK(gs.opacity == doctest::Approx(1.0f));
        CHECK(gs.scale.x == doctest::Approx(1.0f));
        CHECK(gs.scale.y == doctest::Approx(1.0f));
        CHECK(gs.position.x == doctest::Approx(0.0f));
        CHECK(gs.rotation.x == doctest::Approx(0.0f));
        CHECK(gs.blur == doctest::Approx(0.0f));
        CHECK(gs.fill.r == doctest::Approx(1.0f));
        CHECK(gs.stroke.a == doctest::Approx(0.0f)); // stroke disabled
    }
}

TEST_CASE("Property: layout positions copied from placed run") {
    auto placed = make_test_placed_run_prop(3);
    auto states = make_initial_glyph_states(placed);

    CHECK(states[0].layout_position.x == doctest::Approx(placed.glyphs[0].x));
    CHECK(states[1].layout_position.x == doctest::Approx(placed.glyphs[1].x));
}

// ═══════════════════════════════════════════════════════════════════════════
// Single property application
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Property: OpacityProperty reduces opacity") {
    TextAnimatorSpec spec;
    spec.id = "fade";
    spec.selectors.push_back({});
    spec.selectors.back().amount.set(100.0f);  // full weight
    spec.properties.push_back(OpacityProperty{0.0f});

    auto placed = make_test_placed_run_prop(1);
    auto source = make_source_prop(1);
    SampleTime t = SampleTime::from_frame_int(Frame{0});

    auto states = evaluate_animator_stack({spec}, placed, source, t);

    CHECK(states[0].opacity == doctest::Approx(0.0f));
}

TEST_CASE("Property: PositionProperty offsets glyph") {
    TextAnimatorSpec spec;
    spec.id = "move";
    spec.selectors.push_back({});
    spec.selectors.back().amount.set(100.0f);
    spec.properties.push_back(PositionProperty{{0.0f, 60.0f, 0.0f}});

    auto placed = make_test_placed_run_prop(1);
    auto source = make_source_prop(1);
    SampleTime t = SampleTime::from_frame_int(Frame{0});

    auto states = evaluate_animator_stack({spec}, placed, source, t);

    CHECK(states[0].position.y == doctest::Approx(60.0f));
    CHECK(states[0].position.x == doctest::Approx(0.0f));
}

TEST_CASE("Property: ScaleProperty scales down glyph") {
    TextAnimatorSpec spec;
    spec.id = "shrink";
    spec.selectors.push_back({});
    spec.selectors.back().amount.set(100.0f);
    spec.properties.push_back(ScaleProperty{{0.5f, 0.5f, 1.0f}});

    auto placed = make_test_placed_run_prop(1);
    auto source = make_source_prop(1);
    SampleTime t = SampleTime::from_frame_int(Frame{0});

    auto states = evaluate_animator_stack({spec}, placed, source, t);

    CHECK(states[0].scale.x == doctest::Approx(0.5f));
    CHECK(states[0].scale.y == doctest::Approx(0.5f));
}

TEST_CASE("Property: RotationProperty adds rotation") {
    TextAnimatorSpec spec;
    spec.id = "spin";
    spec.selectors.push_back({});
    spec.selectors.back().amount.set(100.0f);
    spec.properties.push_back(RotationProperty{{0.0f, 0.0f, 90.0f}});

    auto placed = make_test_placed_run_prop(1);
    auto source = make_source_prop(1);
    SampleTime t = SampleTime::from_frame_int(Frame{0});

    auto states = evaluate_animator_stack({spec}, placed, source, t);

    CHECK(states[0].rotation.z == doctest::Approx(90.0f));
}

TEST_CASE("Property: BlurProperty adds blur") {
    TextAnimatorSpec spec;
    spec.id = "soft";
    spec.selectors.push_back({});
    spec.selectors.back().amount.set(100.0f);
    spec.properties.push_back(BlurProperty{8.0f});

    auto placed = make_test_placed_run_prop(1);
    auto source = make_source_prop(1);
    SampleTime t = SampleTime::from_frame_int(Frame{0});

    auto states = evaluate_animator_stack({spec}, placed, source, t);

    CHECK(states[0].blur == doctest::Approx(8.0f));
}

TEST_CASE("Property: FillColorProperty changes color") {
    TextAnimatorSpec spec;
    spec.id = "paint";
    spec.selectors.push_back({});
    spec.selectors.back().amount.set(100.0f);
    spec.properties.push_back(FillColorProperty{{0.2f, 0.5f, 0.8f, 1.0f}});

    auto placed = make_test_placed_run_prop(1);
    auto source = make_source_prop(1);
    SampleTime t = SampleTime::from_frame_int(Frame{0});

    auto states = evaluate_animator_stack({spec}, placed, source, t);

    CHECK(states[0].fill.r == doctest::Approx(0.2f));
    CHECK(states[0].fill.g == doctest::Approx(0.5f));
    CHECK(states[0].fill.b == doctest::Approx(0.8f));
}

TEST_CASE("Property: property with zero selector weight has no effect") {
    TextAnimatorSpec spec;
    spec.id = "weak";
    spec.selectors.push_back({});
    spec.selectors.back().amount.set(0.0f);  // zero weight
    spec.properties.push_back(PositionProperty{{100.0f, 100.0f, 0.0f}});

    auto placed = make_test_placed_run_prop(1);
    auto source = make_source_prop(1);
    SampleTime t = SampleTime::from_frame_int(Frame{0});

    auto states = evaluate_animator_stack({spec}, placed, source, t);

    CHECK(states[0].position.x == doctest::Approx(0.0f));
    CHECK(states[0].position.y == doctest::Approx(0.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// Multiple animators with blend modes
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Stack: two animators combine position additively") {
    TextAnimatorSpec spec1;
    spec1.id = "up";
    spec1.selectors.push_back({});
    spec1.selectors.back().amount.set(100.0f);
    spec1.properties.push_back(PositionProperty{{0.0f, 50.0f, 0.0f}});
    spec1.transform_mode = TextPropertyBlendMode::Add;

    TextAnimatorSpec spec2;
    spec2.id = "right";
    spec2.selectors.push_back({});
    spec2.selectors.back().amount.set(100.0f);
    spec2.properties.push_back(PositionProperty{{30.0f, 0.0f, 0.0f}});
    spec2.transform_mode = TextPropertyBlendMode::Add;

    auto placed = make_test_placed_run_prop(1);
    auto source = make_source_prop(1);
    SampleTime t = SampleTime::from_frame_int(Frame{0});

    auto states = evaluate_animator_stack({spec1, spec2}, placed, source, t);

    CHECK(states[0].position.x == doctest::Approx(30.0f));
    CHECK(states[0].position.y == doctest::Approx(50.0f));
}

TEST_CASE("Stack: Replace mode overwrites previous animator") {
    TextAnimatorSpec spec1;
    spec1.id = "first";
    spec1.selectors.push_back({});
    spec1.selectors.back().amount.set(100.0f);
    spec1.properties.push_back(OpacityProperty{0.5f});

    TextAnimatorSpec spec2;
    spec2.id = "second";
    spec2.selectors.push_back({});
    spec2.selectors.back().amount.set(100.0f);
    spec2.properties.push_back(OpacityProperty{0.0f});

    auto placed = make_test_placed_run_prop(1);
    auto source = make_source_prop(1);
    SampleTime t = SampleTime::from_frame_int(Frame{0});

    // Default is Add for transforms, but opacity uses multiply semantics.
    // Let's test explicit Replace for opacity.
    spec2.transform_mode = TextPropertyBlendMode::Replace;

    auto states = evaluate_animator_stack({spec1, spec2}, placed, source, t);

    // With Replace, spec2 opacity of 0.0 overwrites
    CHECK(states[0].opacity == doctest::Approx(0.0f));
}

TEST_CASE("Stack: selector controls per-glyph weight") {
    // Animator that fades from left to right using RampUp
    TextAnimatorSpec spec;
    spec.id = "wipe";
    spec.selectors.push_back({});
    spec.selectors.back().unit = TextSelectorUnit::Glyph;
    spec.selectors.back().shape = TextSelectorShape::RampUp;
    spec.selectors.back().start.set(0.0f);
    spec.selectors.back().end.set(100.0f);
    spec.selectors.back().amount.set(100.0f);
    spec.properties.push_back(OpacityProperty{0.0f});

    auto placed = make_test_placed_run_prop(10);
    auto source = make_source_prop(10);
    SampleTime t = SampleTime::from_frame_int(Frame{0});

    auto states = evaluate_animator_stack({spec}, placed, source, t);

    // First glyphs: low opacity (RampUp at start → weight near 0)
    // Last glyphs: high opacity (RampUp at end → weight near 1)
    CHECK(states[0].opacity > 0.7f);  // Almost fully visible (high weight = close to identity)
    // Actually: OpacityProperty{0.0f} with low weight → opacity near 1.0
    // With high weight → opacity near 0.0
    // So first glyphs should be MORE visible (weight low, close to identity)
    // Last glyphs should be LESS visible (weight high, close to target 0.0)
    // Wait — RampUp means weight increases from left to right
    // So rightmost glyphs have weight ~1.0 → opacity = lerp(1.0, 0.0, 1.0) = 0.0
    // Leftmost glyphs have weight ~0.05 → opacity = lerp(1.0, 0.0, 0.05) = 0.95
    CHECK(states[0].opacity > states[9].opacity);
}

TEST_CASE("Stack: multiple properties in one animator") {
    TextAnimatorSpec spec;
    spec.id = "combo";
    spec.selectors.push_back({});
    spec.selectors.back().amount.set(100.0f);
    spec.properties.push_back(PositionProperty{{0.0f, 60.0f, 0.0f}});
    spec.properties.push_back(OpacityProperty{0.3f});
    spec.properties.push_back(BlurProperty{12.0f});

    auto placed = make_test_placed_run_prop(1);
    auto source = make_source_prop(1);
    SampleTime t = SampleTime::from_frame_int(Frame{0});

    auto states = evaluate_animator_stack({spec}, placed, source, t);

    CHECK(states[0].position.y == doctest::Approx(60.0f));
    CHECK(states[0].opacity == doctest::Approx(0.3f));
    CHECK(states[0].blur == doctest::Approx(12.0f));
}

TEST_CASE("Stack: disabled animator is skipped") {
    TextAnimatorSpec spec;
    spec.id = "ghost";
    spec.enabled = false;
    spec.selectors.push_back({});
    spec.selectors.back().amount.set(100.0f);
    spec.properties.push_back(PositionProperty{{100.0f, 0.0f, 0.0f}});

    auto placed = make_test_placed_run_prop(1);
    auto source = make_source_prop(1);
    SampleTime t = SampleTime::from_frame_int(Frame{0});

    auto states = evaluate_animator_stack({spec}, placed, source, t);

    CHECK(states[0].position.x == doctest::Approx(0.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// BaselineShift, StrokeColor, Scale Add vs Multiply
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Property: BaselineShiftProperty shifts glyph vertically") {
    TextAnimatorSpec spec;
    spec.id = "raise";
    spec.selectors.push_back({});
    spec.selectors.back().amount.set(100.0f);
    spec.properties.push_back(BaselineShiftProperty{8.0f});

    auto placed = make_test_placed_run_prop(1);
    auto source = make_source_prop(1);
    SampleTime t = SampleTime::from_frame_int(Frame{0});

    auto states = evaluate_animator_stack({spec}, placed, source, t);

    CHECK(states[0].baseline_shift == doctest::Approx(8.0f));
}

TEST_CASE("Property: BaselineShiftProperty with partial weight") {
    TextAnimatorSpec spec;
    spec.id = "half_raise";
    spec.selectors.push_back({});
    spec.selectors.back().amount.set(50.0f);  // 50% weight
    spec.properties.push_back(BaselineShiftProperty{10.0f});

    auto placed = make_test_placed_run_prop(1);
    auto source = make_source_prop(1);
    SampleTime t = SampleTime::from_frame_int(Frame{0});

    auto states = evaluate_animator_stack({spec}, placed, source, t);

    CHECK(states[0].baseline_shift == doctest::Approx(5.0f));  // 10 * 0.5
}

TEST_CASE("Property: StrokeColorProperty sets stroke") {
    TextAnimatorSpec spec;
    spec.id = "outline";
    spec.color_mode = TextPropertyBlendMode::Replace;
    spec.selectors.push_back({});
    spec.selectors.back().amount.set(100.0f);
    spec.properties.push_back(StrokeColorProperty{{0.8f, 0.2f, 0.1f, 1.0f}});

    auto placed = make_test_placed_run_prop(1);
    auto source = make_source_prop(1);
    SampleTime t = SampleTime::from_frame_int(Frame{0});

    auto states = evaluate_animator_stack({spec}, placed, source, t);

    CHECK(states[0].stroke.r == doctest::Approx(0.8f));
    CHECK(states[0].stroke.g == doctest::Approx(0.2f));
    CHECK(states[0].stroke.b == doctest::Approx(0.1f));
    CHECK(states[0].stroke.a == doctest::Approx(1.0f));
}

TEST_CASE("Property: StrokeColorProperty with partial weight") {
    TextAnimatorSpec spec;
    spec.id = "faded_outline";
    spec.color_mode = TextPropertyBlendMode::Replace;
    spec.selectors.push_back({});
    spec.selectors.back().amount.set(50.0f);  // half weight
    // Initial stroke is {0,0,0,0} (alpha=0 = disabled).
    // Lerp from {0,0,0,0} to white {1,1,1,1} at 50% = {0.5,0.5,0.5,0.5}
    spec.properties.push_back(StrokeColorProperty{{1.0f, 1.0f, 1.0f, 1.0f}});

    auto placed = make_test_placed_run_prop(1);
    auto source = make_source_prop(1);
    SampleTime t = SampleTime::from_frame_int(Frame{0});

    auto states = evaluate_animator_stack({spec}, placed, source, t);

    // Halfway from {0,0,0,0} to {1,1,1,1}
    CHECK(states[0].stroke.r == doctest::Approx(0.5f));
    CHECK(states[0].stroke.g == doctest::Approx(0.5f));
    CHECK(states[0].stroke.b == doctest::Approx(0.5f));
    CHECK(states[0].stroke.a == doctest::Approx(0.5f));
}

TEST_CASE("Property: ScaleProperty Add accumulates deltas") {
    // Two animators each scaling by 50% in Add mode → 1.0 + (-0.5) + (-0.5) = 0.0
    TextAnimatorSpec spec1;
    spec1.id = "shrink1";
    spec1.transform_mode = TextPropertyBlendMode::Add;
    spec1.selectors.push_back({});
    spec1.selectors.back().amount.set(100.0f);
    spec1.properties.push_back(ScaleProperty{{0.5f, 1.0f, 1.0f}});

    TextAnimatorSpec spec2;
    spec2.id = "shrink2";
    spec2.transform_mode = TextPropertyBlendMode::Add;
    spec2.selectors.push_back({});
    spec2.selectors.back().amount.set(100.0f);
    spec2.properties.push_back(ScaleProperty{{0.5f, 1.0f, 1.0f}});

    auto placed = make_test_placed_run_prop(1);
    auto source = make_source_prop(1);
    SampleTime t = SampleTime::from_frame_int(Frame{0});

    auto states = evaluate_animator_stack({spec1, spec2}, placed, source, t);

    // Add: 1.0 + (0.5 - 1.0) + (0.5 - 1.0) = 1.0 - 0.5 - 0.5 = 0.0
    CHECK(states[0].scale.x == doctest::Approx(0.0f));
    CHECK(states[0].scale.y == doctest::Approx(1.0f));  // y not affected
}

TEST_CASE("Property: ScaleProperty Multiply composes factors") {
    // Two animators each scaling by 50% in Multiply mode → 1.0 * 0.5 * 0.5 = 0.25
    TextAnimatorSpec spec1;
    spec1.id = "mul1";
    spec1.transform_mode = TextPropertyBlendMode::Multiply;
    spec1.selectors.push_back({});
    spec1.selectors.back().amount.set(100.0f);
    spec1.properties.push_back(ScaleProperty{{0.5f, 1.0f, 1.0f}});

    TextAnimatorSpec spec2;
    spec2.id = "mul2";
    spec2.transform_mode = TextPropertyBlendMode::Multiply;
    spec2.selectors.push_back({});
    spec2.selectors.back().amount.set(100.0f);
    spec2.properties.push_back(ScaleProperty{{0.5f, 1.0f, 1.0f}});

    auto placed = make_test_placed_run_prop(1);
    auto source = make_source_prop(1);
    SampleTime t = SampleTime::from_frame_int(Frame{0});

    auto states = evaluate_animator_stack({spec1, spec2}, placed, source, t);

    // Multiply: 1.0 * 0.5 * 0.5 = 0.25
    CHECK(states[0].scale.x == doctest::Approx(0.25f));
    CHECK(states[0].scale.y == doctest::Approx(1.0f));
}

TEST_CASE("Property: ScaleProperty Add vs Multiply produce different results") {
    // Same single 50% scale, Add vs Multiply should differ from identity in different ways
    auto placed = make_test_placed_run_prop(1);
    auto source = make_source_prop(1);
    SampleTime t = SampleTime::from_frame_int(Frame{0});

    TextAnimatorSpec add_spec;
    add_spec.id = "add";
    add_spec.transform_mode = TextPropertyBlendMode::Add;
    add_spec.selectors.push_back({});
    add_spec.selectors.back().amount.set(100.0f);
    add_spec.properties.push_back(ScaleProperty{{0.5f, 0.5f, 1.0f}});
    auto add_states = evaluate_animator_stack({add_spec}, placed, source, t);

    TextAnimatorSpec mul_spec;
    mul_spec.id = "mul";
    mul_spec.transform_mode = TextPropertyBlendMode::Multiply;
    mul_spec.selectors.push_back({});
    mul_spec.selectors.back().amount.set(100.0f);
    mul_spec.properties.push_back(ScaleProperty{{0.5f, 0.5f, 1.0f}});
    auto mul_states = evaluate_animator_stack({mul_spec}, placed, source, t);

    // Add: 1.0 + (0.5 - 1.0) = 0.5
    // Multiply: 1.0 * 0.5 = 0.5
    // Single animator: same result in both modes (identity * factor = identity + delta)
    CHECK(add_states[0].scale.x == doctest::Approx(0.5f));
    CHECK(mul_states[0].scale.x == doctest::Approx(0.5f));
}

TEST_CASE("Property: TrackingProperty applies horizontal position offset") {
    TextAnimatorSpec spec;
    spec.id = "track";
    spec.selectors.push_back({});
    spec.selectors.back().amount.set(100.0f);
    spec.properties.push_back(TrackingProperty{14.0f});

    auto placed = make_test_placed_run_prop(1);
    auto source = make_source_prop(1);
    SampleTime t = SampleTime::from_frame_int(Frame{0});

    auto states = evaluate_animator_stack({spec}, placed, source, t);

    CHECK(states[0].position.x == doctest::Approx(14.0f));
}
