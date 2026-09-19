#include <doctest/doctest.h>

#include <src/render_graph/nodes/text_run/gpu_text_run.hpp>
#include <chronon3d/text/animation/text_animator_stack.hpp>
#include <chronon3d/text/font_engine.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::graph::text_run;

namespace {

PlacedGlyphRun make_test_placed_run(std::size_t count) {
    PlacedGlyphRun placed;
    placed.glyphs.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        PlacedGlyph glyph;
        glyph.glyph_id = static_cast<u32>(i + 1);
        glyph.cluster = static_cast<u32>(i);
        glyph.is_cluster_start = true;
        glyph.x = 42.0f + static_cast<float>(i) * 27.0f;
        glyph.y = 18.0f;
        glyph.advance_x = 27.0f;
        glyph.byte_offset = i;
        glyph.byte_len = 1;
        placed.glyphs.push_back(glyph);
    }
    return placed;
}

GlyphSelectorSpec full_glyph_selector() {
    GlyphSelectorSpec selector;
    selector.unit = TextSelectorUnit::Glyph;
    selector.shape = TextSelectorShape::Square;
    selector.order = TextSelectorOrder::Forward;
    selector.combine = SelectorCombineMode::Replace;
    selector.start = AnimatedValue<f32>(0.0f);
    selector.end = AnimatedValue<f32>(100.0f);
    selector.amount = AnimatedValue<f32>(100.0f);
    return selector;
}

} // namespace

TEST_CASE("GPU MTSDF per-glyph scale stays anchored to layout position") {
    GlyphInstanceState glyph;
    glyph.layout_position = {500.0f, 120.0f};
    glyph.position = {3.0f, -2.0f, 0.0f};
    glyph.baseline_shift = 4.0f;
    glyph.anchor = {10.0f, 5.0f, 0.0f};
    glyph.scale = {0.96f, 1.02f, 1.0f};

    const auto placement = make_gpu_glyph_placement(
        glyph, 100.0f, 200.0f, 1.5f, 2.0f);

    // The prepared plane already contains the immutable layout position. The
    // run origin compensates for its scaled layout/anchor origin so the
    // animated scale affects only the glyph-local ink.
    CHECK(placement.tx == doctest::Approx(135.1f));
    CHECK(placement.ty == doctest::Approx(199.0f));
    CHECK(placement.sx == doctest::Approx(1.44f));
    CHECK(placement.sy == doctest::Approx(2.04f));
}

TEST_CASE("GPU MTSDF identity scale preserves the animated glyph offset") {
    GlyphInstanceState glyph;
    glyph.layout_position = {500.0f, 120.0f};
    glyph.position = {3.0f, -2.0f, 0.0f};
    glyph.baseline_shift = 4.0f;
    glyph.anchor = {10.0f, 5.0f, 0.0f};

    const auto placement = make_gpu_glyph_placement(
        glyph, 100.0f, 200.0f, 1.5f, 2.0f);

    CHECK(placement.tx == doctest::Approx(104.5f));
    CHECK(placement.ty == doctest::Approx(204.0f));
    CHECK(placement.sx == doctest::Approx(1.5f));
    CHECK(placement.sy == doctest::Approx(2.0f));
}

TEST_CASE("GPU MTSDF scale keeps the glyph anchor invariant across emphasis scales") {
    GlyphInstanceState glyph;
    glyph.layout_position = {500.0f, 120.0f};
    glyph.position = {3.0f, -2.0f, 0.0f};
    glyph.baseline_shift = 4.0f;
    glyph.anchor = {10.0f, 5.0f, 0.0f};

    constexpr float run_tx = 300.0f;
    constexpr float run_ty = 200.0f;
    constexpr float run_sx = 1.5f;
    constexpr float run_sy = 2.0f;
    const float expected_x = run_tx + run_sx *
        (glyph.position.x + glyph.layout_position.x + glyph.anchor.x);
    const float expected_y = run_ty + run_sy *
        (glyph.position.y + glyph.baseline_shift +
         glyph.layout_position.y + glyph.anchor.y);

    for (const float scale : {0.75f, 1.0f, 1.25f, 2.0f}) {
        glyph.scale = {scale, scale, 1.0f};
        const auto placement = make_gpu_glyph_placement(
            glyph, run_tx, run_ty, run_sx, run_sy);
        CHECK(placement.tx + placement.sx *
                  (glyph.layout_position.x + glyph.anchor.x)
              == doctest::Approx(expected_x));
        CHECK(placement.ty + placement.sy *
                  (glyph.layout_position.y + glyph.anchor.y)
              == doctest::Approx(expected_y));
    }
}

TEST_CASE("GPU MTSDF scale plus translation keeps the authored pivot fixed") {
    GlyphInstanceState glyph;
    glyph.layout_position = {240.0f, 160.0f};
    glyph.anchor = {12.0f, -8.0f, 0.0f};
    glyph.position = {0.0f, 0.0f, 0.0f};
    glyph.scale = {1.0f, 1.0f, 1.0f};

    for (const float scale : {0.8f, 1.2f}) {
        glyph.scale = {scale, scale, 1.0f};
        const auto placement = make_gpu_glyph_placement(
            glyph, 300.0f, 200.0f, 1.0f, 1.0f);
        CHECK(placement.tx + placement.sx *
                  (glyph.layout_position.x + glyph.anchor.x)
              == doctest::Approx(552.0f));
        CHECK(placement.ty + placement.sy *
                  (glyph.layout_position.y + glyph.anchor.y)
              == doctest::Approx(352.0f));
    }
}

TEST_CASE("GPU text does not lower rotation into the scale-only MTSDF path") {
    auto layout = std::make_shared<TextRunLayout>();
    layout->placed = make_test_placed_run(1);

    TextRunShape shape;
    shape.layout = layout;
    shape.glyphs = make_initial_glyph_states(layout->placed);

    TextAnimatorSpec animator;
    animator.selectors.push_back(full_glyph_selector());
    animator.properties.push_back(RotationProperty{{0.0f, 0.0f, 12.0f}});
    shape.animators.push_back(std::move(animator));

    CHECK_FALSE(can_lower_gpu_text_animation(shape));
}

TEST_CASE("Typewriter reveal keeps already shaped glyph positions stable") {
    const auto placed = make_test_placed_run(7);
    const std::string source = "CHRONON";

    TextAnimatorSpec reveal;
    GlyphSelectorSpec selector = full_glyph_selector();
    selector.unit = TextSelectorUnit::Grapheme;
    selector.shape = TextSelectorShape::RampDown;
    selector.start = AnimatedValue<f32>(0.0f);
    selector.start.add_keyframe(Frame{0}, 0.0f);
    selector.start.add_keyframe(Frame{60}, 100.0f);
    selector.end = AnimatedValue<f32>(100.0f);
    reveal.selectors.push_back(std::move(selector));
    reveal.properties.push_back(OpacityProperty{0.0f});
    reveal.transform_mode = TextPropertyBlendMode::Multiply;

    const auto early = evaluate_animator_stack(
        {reveal}, placed, source,
        SampleTime::from_frame_int(Frame{10}, FrameRate{30, 1}));
    const auto late = evaluate_animator_stack(
        {reveal}, placed, source,
        SampleTime::from_frame_int(Frame{60}, FrameRate{30, 1}));

    REQUIRE(early.size() == late.size());
    for (std::size_t i = 0; i < early.size(); ++i) {
        CHECK(early[i].layout_position.x ==
              doctest::Approx(late[i].layout_position.x));
        CHECK(early[i].layout_position.y ==
              doctest::Approx(late[i].layout_position.y));
    }
}

TEST_CASE("Text animation evaluation is deterministic under random access") {
    const auto placed = make_test_placed_run(6);
    const std::string source = "CHRONO";

    TextAnimatorSpec scale;
    scale.selectors.push_back(full_glyph_selector());
    ScaleProperty property{Vec3{1.0f, 1.0f, 1.0f}};
    property.value.add_keyframe(Frame{0}, Vec3{0.8f, 0.8f, 1.0f});
    property.value.add_keyframe(Frame{60}, Vec3{1.2f, 1.2f, 1.0f});
    scale.properties.push_back(std::move(property));

    const std::array<int, 6> sequential{0, 10, 20, 40, 50, 60};
    const std::array<int, 6> random{40, 0, 60, 20, 10, 50};
    std::vector<std::vector<GlyphInstanceState>> expected;
    expected.reserve(sequential.size());
    for (const int frame : sequential) {
        expected.push_back(evaluate_animator_stack(
            {scale}, placed, source,
            SampleTime::from_frame_int(Frame{frame}, FrameRate{30, 1})));
    }

    for (const int frame : random) {
        const auto actual = evaluate_animator_stack(
            {scale}, placed, source,
            SampleTime::from_frame_int(Frame{frame}, FrameRate{30, 1}));
        const auto it = std::find(sequential.begin(), sequential.end(), frame);
        REQUIRE(it != sequential.end());
        const auto& baseline = expected[static_cast<std::size_t>(
            std::distance(sequential.begin(), it))];
        REQUIRE(actual.size() == baseline.size());
        for (std::size_t i = 0; i < actual.size(); ++i) {
            CHECK(actual[i].scale.x == doctest::Approx(baseline[i].scale.x));
            CHECK(actual[i].scale.y == doctest::Approx(baseline[i].scale.y));
            CHECK(actual[i].layout_position.x ==
                  doctest::Approx(baseline[i].layout_position.x));
        }
    }
}
