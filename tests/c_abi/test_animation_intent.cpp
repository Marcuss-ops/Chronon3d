// tests/c_abi/test_animation_intent.cpp — locks the render-plan animation
// intent → text selector/animator lowering (unit word/glyph/line + enter/exit
// frame durations) so the contract cannot silently drift from Chronon's text
// pipeline.

#include <chronon3d/render_plan/animation_intent.hpp>

#include <chronon3d/text/animation/text_animator_properties.hpp>  // OpacityProperty

#include <doctest/doctest.h>

#include <variant>

using chronon3d::render_plan::selector_unit;
using chronon3d::render_plan::build_unit_reveal_animator;
using chronon3d::render_plan::resolve_animation;
using chronon3d::render_plan::ResolvedAnimation;
using chronon3d::render_plan::LayerPlan;
using chronon3d::render_plan::AnimationTiming;
using chronon3d::registry::AnimationSpec;
using chronon3d::Frame;
using chronon3d::TextSelectorUnit;
using chronon3d::OpacityProperty;

namespace {

AnimationSpec preset_animation(
    std::string preset = "fade_in",
    std::string unit = "line",
    std::optional<int> enter = 8,
    std::optional<int> exit = 6) {
    return AnimationSpec{.preset = std::move(preset), .unit = std::move(unit),
                         .enter_duration_frames = enter,
                         .exit_duration_frames = exit};
}

LayerPlan layer_with_animation(std::optional<AnimationTiming> animation) {
    LayerPlan layer;
    layer.id = "test_layer";
    layer.type = chronon3d::render_plan::LayerType::Text;
    layer.start_frame = Frame{30};
    layer.duration_frames = Frame{60};
    layer.animation = std::move(animation);
    return layer;
}

} // namespace

TEST_CASE("selector_unit maps word/glyph/line and defaults unknown to word") {
    CHECK(selector_unit("word") == TextSelectorUnit::Word);
    CHECK(selector_unit("glyph") == TextSelectorUnit::Glyph);
    CHECK(selector_unit("line") == TextSelectorUnit::Line);
    CHECK(selector_unit("") == TextSelectorUnit::Word);
    CHECK(selector_unit("nonsense") == TextSelectorUnit::Word);
}

TEST_CASE("build_unit_reveal_animator scopes the selector to the requested unit") {
    const auto word = build_unit_reveal_animator("word", Frame{10}, Frame{40});
    REQUIRE(word.selectors.size() == 1u);
    CHECK(word.selectors.front().unit == TextSelectorUnit::Word);
    CHECK(word.selectors.front().exclude_spaces);
    REQUIRE(word.properties.size() == 1u);
    CHECK(std::holds_alternative<OpacityProperty>(word.properties.front()));

    const auto glyph = build_unit_reveal_animator("glyph", Frame{0}, Frame{40});
    REQUIRE(glyph.selectors.size() == 1u);
    CHECK(glyph.selectors.front().unit == TextSelectorUnit::Glyph);
    CHECK_FALSE(glyph.selectors.front().exclude_spaces);

    const auto line = build_unit_reveal_animator("line", Frame{0}, Frame{40});
    CHECK(line.selectors.front().unit == TextSelectorUnit::Line);
}

TEST_CASE("build_unit_reveal_animator enter+exit keyframes span the window") {
    const auto animator = build_unit_reveal_animator(
        "line", Frame{20}, Frame{40}, Frame{8}, Frame{6});
    const auto& opacity = std::get<OpacityProperty>(animator.properties.front());
    // enter ramp 20→28 + exit ramp 54→60 = 4 monotonic keyframes.
    CHECK(opacity.value.size() == 4u);
    CHECK(opacity.value.first_keyframe_time() == Frame{20});
    CHECK(opacity.value.last_keyframe_time() == Frame{60});
    CHECK(opacity.value.evaluate(20.0) == doctest::Approx(0.0f));
    CHECK(opacity.value.evaluate(40.0) == doctest::Approx(1.0f));  // stable gap
    CHECK(opacity.value.evaluate(60.0) == doctest::Approx(0.0f));
}

TEST_CASE("build_unit_reveal_animator clamps short windows to enter-only") {
    const auto animator = build_unit_reveal_animator(
        "word", Frame{0}, Frame{10}, Frame{8}, Frame{6});
    const auto& opacity = std::get<OpacityProperty>(animator.properties.front());
    // enter(8) + exit(6) >= total(10) → exit dropped, only 2 keyframes.
    CHECK(opacity.value.size() == 2u);
    CHECK(opacity.value.first_keyframe_time() == Frame{0});
    CHECK(opacity.value.last_keyframe_time() == Frame{8});
}

TEST_CASE("resolve_animation: preset defaults fill absent plan fields") {
    // No explicit animation block: everything comes from the registry
    // descriptor (preset + unit + enter/exit durations).
    const auto layer = layer_with_animation(std::nullopt);
    const auto resolved = resolve_animation(preset_animation(), layer, Frame{90});

    CHECK(resolved.preset == "fade_in");
    CHECK(resolved.unit == "line");
    CHECK(resolved.enter_duration == Frame{8});
    CHECK(resolved.exit_duration == Frame{6});
    CHECK(resolved.layer_start == Frame{30});
    CHECK(resolved.layer_duration == Frame{60});
    CHECK(resolved.text_intent);
}

TEST_CASE("resolve_animation: explicit plan overrides win over preset defaults") {
    AnimationTiming timing;
    timing.preset = "soft_pop";
    timing.unit = "word";
    timing.enter_duration_frames = Frame{12};
    timing.exit_duration_frames = Frame{3};
    const auto resolved =
        resolve_animation(preset_animation(), layer_with_animation(timing), Frame{90});

    CHECK(resolved.preset == "soft_pop");
    CHECK(resolved.unit == "word");
    CHECK(resolved.enter_duration == Frame{12});
    CHECK(resolved.exit_duration == Frame{3});
}

TEST_CASE("resolve_animation: bare layer motion stays one-shot (no exit)") {
    // Legacy plan: `animation: {preset: "fade_in"}` with no durations and no
    // visual preset.  fade_in must remain a pure entry animation — it is
    // never converted into a fade_in_and_out.
    AnimationTiming timing;
    timing.preset = "fade_in";
    const auto resolved =
        resolve_animation(std::nullopt, layer_with_animation(timing), Frame{90});

    CHECK(resolved.preset == "fade_in");
    CHECK(resolved.unit.empty());
    CHECK(resolved.enter_duration == Frame{8});
    CHECK(resolved.exit_duration == Frame{0});
    CHECK_FALSE(resolved.text_intent);
}

TEST_CASE("resolve_animation: short window drops the exit (enter-only clamp)") {
    // enter(8) + exit(6) >= window(10) → deterministic clamp: exit = 0.
    AnimationTiming timing;
    timing.preset = "fade_in";
    timing.enter_duration_frames = Frame{8};
    timing.exit_duration_frames = Frame{6};
    auto layer = layer_with_animation(timing);
    layer.duration_frames = Frame{10};
    const auto resolved = resolve_animation(std::nullopt, layer, Frame{90});

    CHECK(resolved.enter_duration == Frame{8});
    CHECK(resolved.exit_duration == Frame{0});
}

TEST_CASE("resolve_animation: layer durations default to the composition") {
    AnimationTiming timing;
    timing.preset = "fade_in";
    timing.exit_duration_frames = Frame{6};
    auto layer = layer_with_animation(timing);
    layer.start_frame.reset();
    layer.duration_frames.reset();
    const auto resolved = resolve_animation(std::nullopt, layer, Frame{120});

    CHECK(resolved.layer_start == Frame{0});
    CHECK(resolved.layer_duration == Frame{120});
    CHECK(resolved.enter_duration == Frame{8});
    CHECK(resolved.exit_duration == Frame{6});
}

TEST_CASE("animation routing: word/glyph/line → per-unit text animator; motion preset → layer-only") {
    // The render-plan `unit` is the routing key.  word/glyph/line must lower
    // through the TEXT pipeline (selector scoped to the unit), never be
    // simulated by moving the whole layer.
    for (const auto& [unit, selector] : std::vector<std::pair<std::string, TextSelectorUnit>>{
             {"word", TextSelectorUnit::Word},
             {"glyph", TextSelectorUnit::Glyph},
             {"line", TextSelectorUnit::Line}}) {
        CAPTURE(unit);
        const auto resolved = resolve_animation(
            preset_animation("fade_in", unit),
            layer_with_animation(std::nullopt), Frame{90});
        CHECK(resolved.unit == unit);
        CHECK(resolved.text_intent);  // per-unit animator WILL be emitted

        const auto animator = build_unit_reveal_animator(
            unit, Frame{0}, Frame{60});
        REQUIRE(animator.selectors.size() == 1u);
        CHECK(animator.selectors.front().unit == selector);
    }

    // fade / slide / pop / scale stay LAYER motion: no selector unit → the
    // text run is static and the motion runs via LayerBuilder::motion().
    for (const auto& preset : {"fade_in", "slide_in", "soft_pop", "scale_drop"}) {
        CAPTURE(preset);
        AnimationTiming timing;
        timing.preset = preset;
        const auto resolved = resolve_animation(
            std::nullopt, layer_with_animation(timing), Frame{90});
        CHECK(resolved.preset == preset);
        CHECK(resolved.unit.empty());
        CHECK_FALSE(resolved.text_intent);  // no per-unit text animator
    }
}

TEST_CASE("build_unit_reveal_animator is deterministic") {
    const auto a = build_unit_reveal_animator("line", Frame{3}, Frame{50},
                                              Frame{8}, Frame{6});
    const auto b = build_unit_reveal_animator("line", Frame{3}, Frame{50},
                                              Frame{8}, Frame{6});
    CHECK(a.id == b.id);
    REQUIRE(a.selectors.size() == b.selectors.size());
    CHECK(a.selectors.front().unit == b.selectors.front().unit);
    REQUIRE(a.properties.size() == b.properties.size());
    const auto& oa = std::get<OpacityProperty>(a.properties.front());
    const auto& ob = std::get<OpacityProperty>(b.properties.front());
    CHECK(oa.value.size() == ob.value.size());
    CHECK(oa.value.first_keyframe_time() == ob.value.first_keyframe_time());
    CHECK(oa.value.last_keyframe_time() == ob.value.last_keyframe_time());
}
