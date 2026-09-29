#include <doctest/doctest.h>

#include <src/render_graph/nodes/text_run/gpu_text_run.hpp>
#include <chronon3d/text/animation/text_animator_stack.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_color_packing.hpp>
#include <chronon3d/runtime/gpu_text_selector_metadata.hpp>

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

TEST_CASE("GPU text lowers Z-only rotation and samples it per glyph") {
    auto layout = std::make_shared<TextRunLayout>();
    layout->placed = make_test_placed_run(3);

    TextRunShape shape;
    shape.layout = layout;
    shape.glyphs = make_initial_glyph_states(layout->placed);

    TextAnimatorSpec animator;
    animator.selectors.push_back(full_glyph_selector());
    animator.properties.push_back(
        RotationProperty{AnimatedValue<Vec3>{Vec3{0.0f, 0.0f, 12.0f}}});
    shape.animators.push_back(std::move(animator));

    REQUIRE(can_lower_gpu_text_animation(shape));

    runtime::TextRunDynamic run;
    sample_gpu_text_animation(
        shape, SampleTime::from_frame_int(Frame{5}, FrameRate{30, 1}), run);
    CHECK(run.animator_rotation_z == doctest::Approx(12.0f));
}

TEST_CASE("GPU text lowers canonical selector shapes with CPU-equivalent per-glyph weights") {
    const std::array<TextSelectorShape, 6> shapes{
        TextSelectorShape::Square, TextSelectorShape::RampUp,
        TextSelectorShape::RampDown, TextSelectorShape::Triangle,
        TextSelectorShape::Round, TextSelectorShape::Smooth};

    for (const auto selector_shape : shapes) {
        auto layout = std::make_shared<TextRunLayout>();
        layout->placed = make_test_placed_run(5);
        layout->units.glyph_to_grapheme = {0u, 1u, 2u, 3u, 4u};
        layout->units.grapheme_count = 5u;

        TextRunShape shape;
        shape.layout = layout;
        shape.glyphs = make_initial_glyph_states(layout->placed);

        GlyphSelectorSpec selector = full_glyph_selector();
        selector.shape = selector_shape;
        selector.start = AnimatedValue<f32>(0.0f);
        selector.start.add_keyframe(Frame{0}, 0.0f);
        selector.start.add_keyframe(Frame{60}, 80.0f);
        selector.end = AnimatedValue<f32>(100.0f);
        selector.amount = AnimatedValue<f32>(100.0f);

        TextAnimatorSpec animator;
        animator.selectors.push_back(selector);
        animator.properties.push_back(OpacityProperty{0.0f});
        shape.animators.push_back(std::move(animator));

        CAPTURE(static_cast<int>(selector_shape));
        REQUIRE(can_lower_gpu_text_animation(shape));

        runtime::TextRunDynamic run;
        const auto time = SampleTime::from_frame_int(Frame{30}, FrameRate{30, 1});
        sample_gpu_text_animation(shape, time, run);
        CHECK(runtime::unpack_text_selector_shape(run.flags) == selector_shape);

        const auto params = bind_selector_runtime(selector, 5u, time);
        for (std::uint32_t glyph = 0; glyph < 5; ++glyph) {
            CHECK(runtime::gpu_text_selector_weight(run, glyph) ==
                  doctest::Approx(selector_weight(params, glyph)));
        }
    }
}

TEST_CASE("GPU text rejects resolved X/Y rotation outside the in-plane MTSDF path") {
    auto layout = std::make_shared<TextRunLayout>();
    layout->placed = make_test_placed_run(1);

    TextRunShape shape;
    shape.layout = layout;
    shape.glyphs = make_initial_glyph_states(layout->placed);

    shape.glyphs[0].rotation.x = 10.0f;

    CHECK_FALSE(can_lower_gpu_text_animation(shape));
}

TEST_CASE("GPU text accepts resolved states from stacked and non-square selectors") {
    auto layout = std::make_shared<TextRunLayout>();
    layout->placed = make_test_placed_run(3);

    TextRunShape shape;
    shape.layout = layout;
    shape.glyphs = make_initial_glyph_states(layout->placed);
    for (std::size_t i = 0; i < shape.glyphs.size(); ++i) {
        shape.glyphs[i].position.y = static_cast<float>(i * 4);
        shape.glyphs[i].scale = {0.9f + static_cast<float>(i) * 0.1f, 1.0f, 1.0f};
        shape.glyphs[i].opacity = static_cast<float>(i) / 2.0f;
    }
    TextAnimatorSpec first;
    first.selectors.push_back(full_glyph_selector());
    first.properties.push_back(OpacityProperty{0.0f});
    shape.animators.push_back(first);

    GlyphSelectorSpec wiggly = full_glyph_selector();
    wiggly.shape = TextSelectorShape::Wiggly;
    TextAnimatorSpec second;
    second.selectors.push_back(wiggly);
    second.properties.push_back(PositionProperty{Vec3{0.0f, 4.0f, 0.0f}});
    shape.animators.push_back(second);

    CHECK(can_lower_gpu_text_animation(shape));
}

TEST_CASE("GPU text lowers fill_color and packs the per-glyph target") {
    auto layout = std::make_shared<TextRunLayout>();
    layout->placed = make_test_placed_run(3);

    TextRunShape shape;
    shape.layout = layout;
    shape.glyphs = make_initial_glyph_states(layout->placed);

    const Color target{0.25f, 0.5f, 0.75f, 1.0f};
    TextAnimatorSpec animator;
    animator.selectors.push_back(full_glyph_selector());
    animator.properties.push_back(FillColorProperty{target});
    shape.animators.push_back(std::move(animator));

    REQUIRE(can_lower_gpu_text_animation(shape));

    runtime::TextRunDynamic run;
    sample_gpu_text_animation(
        shape, SampleTime::from_frame_int(Frame{5}, FrameRate{30, 1}), run);
    CHECK((run.flags & runtime::kTextRunFlagAnimatorFill) != 0u);
    CHECK(runtime::unpack_text_selector_shape(run.flags) ==
          TextSelectorShape::Square);
    CHECK(run.animator_fill_color ==
          chronon3d::text::pack_text_color(target));
}

TEST_CASE("GPU run-level fill_color lowers and lerps the packed run color") {
    auto layout = std::make_shared<TextRunLayout>();
    layout->units.word_count = 1;
    layout->placed = make_test_placed_run(3);

    TextRunShape shape;
    shape.layout = layout;
    shape.glyphs = make_initial_glyph_states(layout->placed);

    GlyphSelectorSpec whole_word;
    whole_word.unit = TextSelectorUnit::Word;
    whole_word.shape = TextSelectorShape::Square;
    whole_word.order = TextSelectorOrder::Forward;
    whole_word.combine = SelectorCombineMode::Replace;
    whole_word.start = AnimatedValue<f32>(0.0f);
    whole_word.end = AnimatedValue<f32>(100.0f);
    whole_word.amount = AnimatedValue<f32>(100.0f);

    TextAnimatorSpec animator;
    animator.selectors.push_back(std::move(whole_word));
    animator.properties.push_back(
        FillColorProperty{Color{1.0f, 0.0f, 0.0f, 1.0f}});
    shape.animators.push_back(std::move(animator));

    REQUIRE(can_lower_gpu_run_level_animation(shape));

    runtime::TextRunDynamic run;
    run.color = chronon3d::text::pack_text_color(Color{0.0f, 0.0f, 1.0f, 1.0f});
    sample_gpu_run_level_animation(
        shape, SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1}), run);
    // Full-run selector weight 1.0 lands the fill exactly on the target.
    CHECK(run.color ==
          chronon3d::text::pack_text_color(Color{1.0f, 0.0f, 0.0f, 1.0f}));
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
