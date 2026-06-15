#include <doctest/doctest.h>
#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/math/glm_types.hpp>

using namespace chronon3d;
using namespace chronon3d::graphics;

// ===========================================================================
// f32 expression tests (existing)
// ===========================================================================

TEST_CASE("AnimatedValue expression uses default value as value variable") {
    AnimatedValue<f32> v{10.0f};
    v.expression("value + 5");

    CHECK(v.evaluate(0) == doctest::Approx(15.0f));
}

TEST_CASE("AnimatedValue expression receives frame variable") {
    AnimatedValue<f32> v{10.0f};
    v.expression("frame * 2");

    CHECK(v.evaluate(7) == doctest::Approx(14.0f));
}

TEST_CASE("AnimatedValue expression receives time variable using fps") {
    AnimatedValue<f32> v{0.0f};
    v.expression("time * 100");

    AnimationEvalContext ctx;
    ctx.fps = 25.0f;

    CHECK(v.evaluate(25, ctx) == doctest::Approx(100.0f));
}

TEST_CASE("AnimatedValue expression combines keyframed base value") {
    AnimatedValue<f32> v{0.0f};
    v.key(0, 0.0f).key(10, 100.0f);
    v.expression("value + 10");

    CHECK(v.evaluate(5) == doctest::Approx(60.0f));
}

TEST_CASE("AnimatedValue expression falls back to base value on parser error") {
    AnimatedValue<f32> v{42.0f};
    v.expression("unknown_var + 1");

    CHECK(v.evaluate(0) == doctest::Approx(42.0f));
}

TEST_CASE("AnimatedValue expression does not affect non-f32 values in V1") {
    AnimatedValue<Vec3> v{Vec3{10.0f, 20.0f, 30.0f}};
    v.expression("frame * 2");

    auto out = v.evaluate(7);
    CHECK(out.x == doctest::Approx(10.0f));
    CHECK(out.y == doctest::Approx(20.0f));
    CHECK(out.z == doctest::Approx(30.0f));
}

// ===========================================================================
// AnimatedValue<FillStyle> — basic keyframe interpolation
// ===========================================================================

TEST_CASE("AnimatedValue<FillStyle> — interpolates solid to solid") {
    const FillStyle red   = FillStyle::solid({1.0f, 0.0f, 0.0f, 1.0f});
    const FillStyle blue  = FillStyle::solid({0.0f, 0.0f, 1.0f, 1.0f});

    AnimatedValue<FillStyle> v(red);
    v.key(0, red);
    v.key(60, blue);

    const FillStyle mid = v.evaluate(Frame{30});
    CHECK(mid.is_solid());
    CHECK(mid.solid_color.r == doctest::Approx(0.5f).epsilon(0.01f));
    CHECK(mid.solid_color.g == doctest::Approx(0.0f).epsilon(0.01f));
    CHECK(mid.solid_color.b == doctest::Approx(0.5f).epsilon(0.01f));
}

TEST_CASE("AnimatedValue<FillStyle> — constant value returns same") {
    const FillStyle teal = FillStyle::solid({0.0f, 0.5f, 0.5f, 1.0f});
    AnimatedValue<FillStyle> v(teal);

    const FillStyle out = v.evaluate(Frame{99});
    CHECK(out.is_solid());
    CHECK(out.solid_color.g == doctest::Approx(0.5f));
}

TEST_CASE("AnimatedValue<FillStyle> — before first keyframe returns first") {
    const FillStyle a = FillStyle::solid({1.0f, 0.0f, 0.0f, 1.0f});
    const FillStyle b = FillStyle::solid({0.0f, 0.0f, 1.0f, 1.0f});

    AnimatedValue<FillStyle> v(a);
    v.key(10, a);
    v.key(60, b);

    const FillStyle out = v.evaluate(Frame{5});
    CHECK(out.is_solid());
    CHECK(out.solid_color.r == doctest::Approx(1.0f));
}

TEST_CASE("AnimatedValue<FillStyle> — after last keyframe returns last") {
    const FillStyle a = FillStyle::solid({1.0f, 0.0f, 0.0f, 1.0f});
    const FillStyle b = FillStyle::solid({0.0f, 0.0f, 1.0f, 1.0f});

    AnimatedValue<FillStyle> v(a);
    v.key(10, a);
    v.key(60, b);

    const FillStyle out = v.evaluate(Frame{100});
    CHECK(out.is_solid());
    CHECK(out.solid_color.b == doctest::Approx(1.0f));
}

// ===========================================================================
// AnimatedValue<FillStyle> — loop modes
// ===========================================================================

TEST_CASE("AnimatedValue<FillStyle> — Loop mode") {
    const FillStyle a = FillStyle::solid({1.0f, 0.0f, 0.0f, 1.0f});
    const FillStyle b = FillStyle::solid({0.0f, 0.0f, 1.0f, 1.0f});

    AnimatedValue<FillStyle> v(a);
    v.key(0,  a);
    v.key(60, b);
    v.loop_mode(LoopMode::Loop);

    // At frame 60, loops back to start (red)
    const FillStyle at60 = v.evaluate(Frame{60});
    CHECK(at60.is_solid());
    CHECK(at60.solid_color.r == doctest::Approx(1.0f).epsilon(0.01f));

    // At frame 90, halfway through cycle (purple)
    const FillStyle at90 = v.evaluate(Frame{90});
    CHECK(at90.is_solid());
    CHECK(at90.solid_color.r == doctest::Approx(0.5f).epsilon(0.01f));
    CHECK(at90.solid_color.b == doctest::Approx(0.5f).epsilon(0.01f));
}

TEST_CASE("AnimatedValue<FillStyle> — PingPong mode") {
    const FillStyle a = FillStyle::solid({1.0f, 0.0f, 0.0f, 1.0f});
    const FillStyle b = FillStyle::solid({0.0f, 0.0f, 1.0f, 1.0f});

    AnimatedValue<FillStyle> v(a);
    v.key(0,  a);
    v.key(60, b);
    v.loop_mode(LoopMode::PingPong);

    // At 60 → blue (end of forward)
    const FillStyle at60 = v.evaluate(Frame{60});
    CHECK(at60.solid_color.b == doctest::Approx(1.0f).epsilon(0.01f));

    // At 90 → bouncing back toward red
    const FillStyle at90 = v.evaluate(Frame{90});
    CHECK(at90.solid_color.r == doctest::Approx(0.5f).epsilon(0.01f));
    CHECK(at90.solid_color.b == doctest::Approx(0.5f).epsilon(0.01f));
}

// ===========================================================================
// AnimatedValue<FillStyle> — roving keyframes
// ===========================================================================

TEST_CASE("AnimatedValue<FillStyle> — roving distributes frames evenly") {
    const FillStyle a = FillStyle::solid({1.0f, 0.0f, 0.0f, 1.0f});
    const FillStyle b = FillStyle::solid({0.0f, 1.0f, 0.0f, 1.0f});
    const FillStyle c = FillStyle::solid({0.0f, 0.0f, 1.0f, 1.0f});

    AnimatedValue<FillStyle> v(a);
    v.add_keyframe(Frame{0},   a, EasingCurve{Easing::Linear}, false);  // anchor
    v.add_keyframe(Frame{30},  b, EasingCurve{Easing::Linear}, true);   // roving
    v.add_keyframe(Frame{60},  c, EasingCurve{Easing::Linear}, false);  // anchor
    v.add_keyframe(Frame{90},  a, EasingCurve{Easing::Linear}, true);   // roving
    v.add_keyframe(Frame{120}, b, EasingCurve{Easing::Linear}, false);  // anchor

    // Roving should distribute roving keyframes evenly between anchors.
    // First roving group: 0↔60 with roving at 30 → stays at 30 (even with 1 roving)
    // Second roving group: 60↔120 with roving at 90 → stays at 90 (even with 1 roving)
    const FillStyle at30 = v.evaluate(Frame{30});
    CHECK(at30.solid_color.g == doctest::Approx(1.0f).epsilon(0.01f));

    // At 45: lerp between green (frame 30) and blue (frame 60), t=0.5
    const FillStyle at45 = v.evaluate(Frame{45});
    CHECK(at45.solid_color.r == doctest::Approx(0.0f).epsilon(0.01f));
    CHECK(at45.solid_color.g == doctest::Approx(0.5f).epsilon(0.01f));
    CHECK(at45.solid_color.b == doctest::Approx(0.5f).epsilon(0.01f));
}

// ===========================================================================
// AnimatedValue<FillStyle> — expression support
// ===========================================================================

TEST_CASE("AnimatedValue<FillStyle> — solid(r,g,b,a) expression") {
    AnimatedValue<FillStyle> v(FillStyle::solid({0.0f, 0.0f, 0.0f, 1.0f}));
    v.expression("solid(1.0, 0.5, 0.0, 1.0)");

    const FillStyle out = v.evaluate(Frame{0});
    CHECK(out.is_solid());
    CHECK(out.solid_color.r == doctest::Approx(1.0f));
    CHECK(out.solid_color.g == doctest::Approx(0.5f));
    CHECK(out.solid_color.b == doctest::Approx(0.0f));
    CHECK(out.solid_color.a == doctest::Approx(1.0f));
}

TEST_CASE("AnimatedValue<FillStyle> — expression with time variable") {
    AnimatedValue<FillStyle> v(FillStyle::solid({0.0f, 0.0f, 0.0f, 1.0f}));
    v.expression("solid(time * 0.1, 0.5, 0.8, 1.0)");

    AnimationEvalContext ctx;
    ctx.fps = 30.0f;  // frame 30 = 1.0s, so time = 1.0

    const FillStyle out = v.evaluate(Frame{30}, ctx);
    CHECK(out.is_solid());
    CHECK(out.solid_color.r == doctest::Approx(0.1f).epsilon(0.01f));
    CHECK(out.solid_color.g == doctest::Approx(0.5f));
    CHECK(out.solid_color.b == doctest::Approx(0.8f));
}

TEST_CASE("AnimatedValue<FillStyle> — expression with frame variable") {
    AnimatedValue<FillStyle> v(FillStyle::solid({0.0f, 0.0f, 0.0f, 1.0f}));
    v.expression("solid(frame / 60.0, 0.0, 0.0, 1.0)");

    // At frame 30, red channel = 30/60 = 0.5
    const FillStyle out = v.evaluate(Frame{30});
    CHECK(out.is_solid());
    CHECK(out.solid_color.r == doctest::Approx(0.5f).epsilon(0.01f));
}

TEST_CASE("AnimatedValue<FillStyle> — expression falls back on parser error") {
    const FillStyle default_val = FillStyle::solid({0.5f, 0.5f, 0.5f, 1.0f});
    AnimatedValue<FillStyle> v(default_val);
    v.expression("unknown_var + 1");  // invalid for FillStyle

    const FillStyle out = v.evaluate(Frame{0});
    CHECK(out.solid_color.r == doctest::Approx(0.5f));
}

TEST_CASE("AnimatedValue<FillStyle> — expression with complex math") {
    AnimatedValue<FillStyle> v(FillStyle::solid({0.0f, 0.0f, 0.0f, 1.0f}));
    v.expression("solid(0.5 + 0.5*sin(frame * 0.1), 0.3, 0.8, 1.0)");

    AnimationEvalContext ctx;
    const FillStyle out = v.evaluate(Frame{15}, ctx);
    CHECK(out.is_solid());
    // sin(15 * 0.1) = sin(1.5) ≈ 0.997, so r ≈ 0.5 + 0.5*0.997 = 0.999
    CHECK(out.solid_color.r == doctest::Approx(0.997f).epsilon(0.01f));
    CHECK(out.solid_color.g == doctest::Approx(0.3f));
}

TEST_CASE("AnimatedValue<FillStyle> — expression with clamping") {
    AnimatedValue<FillStyle> v(FillStyle::solid({0.0f, 0.0f, 0.0f, 1.0f}));
    // Test that values outside [0,1] are clamped
    v.expression("solid(2.0, -0.5, 0.5, 1.0)");

    const FillStyle out = v.evaluate(Frame{0});
    CHECK(out.solid_color.r == doctest::Approx(1.0f));  // clamped
    CHECK(out.solid_color.g == doctest::Approx(0.0f));  // clamped
}

TEST_CASE("AnimatedValue<FillStyle> — expression with keyframes") {
    const FillStyle red  = FillStyle::solid({1.0f, 0.0f, 0.0f, 1.0f});
    const FillStyle blue = FillStyle::solid({0.0f, 0.0f, 1.0f, 1.0f});

    AnimatedValue<FillStyle> v(red);
    v.key(0, red);
    v.key(60, blue);
    // Expression overrides keyframe interpolation with fixed color
    v.expression("solid(0.0, 1.0, 0.0, 1.0)");

    // Expression takes priority: should always return green
    const FillStyle at0  = v.evaluate(Frame{0});
    const FillStyle at30 = v.evaluate(Frame{30});
    const FillStyle at60 = v.evaluate(Frame{60});

    CHECK(at0.solid_color.g  == doctest::Approx(1.0f));
    CHECK(at30.solid_color.g == doctest::Approx(1.0f));
    CHECK(at60.solid_color.g == doctest::Approx(1.0f));
}

TEST_CASE("AnimatedValue<FillStyle> — no expression returns keyframed value") {
    const FillStyle red  = FillStyle::solid({1.0f, 0.0f, 0.0f, 1.0f});
    const FillStyle blue = FillStyle::solid({0.0f, 0.0f, 1.0f, 1.0f});

    AnimatedValue<FillStyle> v(red);
    v.key(0, red);
    v.key(60, blue);

    // No expression set — should use keyframe interpolation
    const FillStyle at30 = v.evaluate(Frame{30});
    CHECK(at30.solid_color.r == doctest::Approx(0.5f).epsilon(0.01f));
    CHECK(at30.solid_color.b == doctest::Approx(0.5f).epsilon(0.01f));
}

TEST_CASE("AnimatedValue<FillStyle> — evaluate with AnimationEvalContext") {
    const FillStyle black = FillStyle::solid({0.0f, 0.0f, 0.0f, 1.0f});

    AnimatedValue<FillStyle> v(black);
    v.expression("solid(index / 10.0, 0.0, 0.0, 1.0)");

    AnimationEvalContext ctx;
    ctx.index = 5;

    const FillStyle out = v.evaluate(Frame{0}, ctx);
    CHECK(out.solid_color.r == doctest::Approx(0.5f).epsilon(0.01f));
}

// ===========================================================================
// AnimatedValue<StrokeStyle> — expression support
// ===========================================================================

TEST_CASE("AnimatedValue<StrokeStyle> — solid(r,g,b,a) expression") {
    AnimatedValue<StrokeStyle> v(StrokeStyle::solid({0.0f, 0.0f, 0.0f, 1.0f}));
    v.expression("solid(1.0, 0.5, 0.0, 1.0)");

    const StrokeStyle out = v.evaluate(Frame{0});
    CHECK(out.color.r == doctest::Approx(1.0f));
    CHECK(out.color.g == doctest::Approx(0.5f));
    CHECK(out.color.b == doctest::Approx(0.0f));
    CHECK(out.color.a == doctest::Approx(1.0f));
    // Other fields should remain at defaults
    CHECK(out.width == doctest::Approx(1.0f));
}

TEST_CASE("AnimatedValue<StrokeStyle> — expression with frame variable") {
    AnimatedValue<StrokeStyle> v(StrokeStyle::solid({0.0f, 0.0f, 0.0f, 1.0f}));
    v.expression("solid(frame / 60.0, 0.0, 0.0, 1.0)");

    const StrokeStyle out = v.evaluate(Frame{30});
    CHECK(out.color.r == doctest::Approx(0.5f).epsilon(0.01f));
}

TEST_CASE("AnimatedValue<StrokeStyle> — expression with time variable") {
    AnimatedValue<StrokeStyle> v(StrokeStyle::solid({0.0f, 0.0f, 0.0f, 1.0f}));
    v.expression("solid(time * 0.1, 0.5, 0.8, 1.0)");

    AnimationEvalContext ctx;
    ctx.fps = 30.0f;

    const StrokeStyle out = v.evaluate(Frame{30}, ctx);
    CHECK(out.color.r == doctest::Approx(0.1f).epsilon(0.01f));
    CHECK(out.color.g == doctest::Approx(0.5f));
    CHECK(out.color.b == doctest::Approx(0.8f));
}

TEST_CASE("AnimatedValue<StrokeStyle> — expression with index variable") {
    AnimatedValue<StrokeStyle> v(StrokeStyle::solid({0.0f, 0.0f, 0.0f, 1.0f}));
    v.expression("solid(index / 10.0, 0.0, 0.0, 1.0)");

    AnimationEvalContext ctx;
    ctx.index = 5;

    const StrokeStyle out = v.evaluate(Frame{0}, ctx);
    CHECK(out.color.r == doctest::Approx(0.5f).epsilon(0.01f));
}

TEST_CASE("AnimatedValue<StrokeStyle> — expression falls back on parser error") {
    const StrokeStyle default_val = StrokeStyle::solid({0.5f, 0.5f, 0.5f, 1.0f}, 3.0f);
    AnimatedValue<StrokeStyle> v(default_val);
    v.expression("unknown_var + 1");

    const StrokeStyle out = v.evaluate(Frame{0});
    CHECK(out.color.r == doctest::Approx(0.5f));
    CHECK(out.width == doctest::Approx(3.0f));
}

TEST_CASE("AnimatedValue<StrokeStyle> — expression with keyframes") {
    const StrokeStyle red  = StrokeStyle::solid({1.0f, 0.0f, 0.0f, 1.0f});
    const StrokeStyle blue = StrokeStyle::solid({0.0f, 0.0f, 1.0f, 1.0f});

    AnimatedValue<StrokeStyle> v(red);
    v.key(0, red);
    v.key(60, blue);
    v.expression("solid(0.0, 1.0, 0.0, 1.0)");

    // Expression takes priority: should always return green
    const StrokeStyle at0  = v.evaluate(Frame{0});
    const StrokeStyle at30 = v.evaluate(Frame{30});
    const StrokeStyle at60 = v.evaluate(Frame{60});

    CHECK(at0.color.g  == doctest::Approx(1.0f));
    CHECK(at30.color.g == doctest::Approx(1.0f));
    CHECK(at60.color.g == doctest::Approx(1.0f));
}

TEST_CASE("AnimatedValue<StrokeStyle> — no expression returns keyframed value") {
    const StrokeStyle red  = StrokeStyle::solid({1.0f, 0.0f, 0.0f, 1.0f});
    const StrokeStyle blue = StrokeStyle::solid({0.0f, 0.0f, 1.0f, 1.0f});

    AnimatedValue<StrokeStyle> v(red);
    v.key(0, red);
    v.key(60, blue);

    const StrokeStyle at30 = v.evaluate(Frame{30});
    CHECK(at30.color.r == doctest::Approx(0.5f).epsilon(0.01f));
    CHECK(at30.color.b == doctest::Approx(0.5f).epsilon(0.01f));
}

TEST_CASE("AnimatedValue<StrokeStyle> — expression with clamping") {
    AnimatedValue<StrokeStyle> v(StrokeStyle::solid({0.0f, 0.0f, 0.0f, 1.0f}));
    v.expression("solid(2.0, -0.5, 0.5, 1.0)");

    const StrokeStyle out = v.evaluate(Frame{0});
    CHECK(out.color.r == doctest::Approx(1.0f));
    CHECK(out.color.g == doctest::Approx(0.0f));
}

TEST_CASE("AnimatedValue<StrokeStyle> — constant value returns same") {
    const StrokeStyle teal = StrokeStyle::solid({0.0f, 0.5f, 0.5f, 1.0f}, 2.5f);
    AnimatedValue<StrokeStyle> v(teal);

    const StrokeStyle out = v.evaluate(Frame{99});
    CHECK(out.color.g == doctest::Approx(0.5f));
    CHECK(out.width == doctest::Approx(2.5f));
}

TEST_CASE("AnimatedValue<StrokeStyle> — default constructed returns default") {
    AnimatedValue<StrokeStyle> v;
    const StrokeStyle out = v.evaluate(Frame{0});
    CHECK_FALSE(out.enabled);  // StrokeStyle default enabled=false
    CHECK(out.width == doctest::Approx(1.0f));
}

TEST_CASE("AnimatedValue<StrokeStyle> — set() clears keyframes and expression") {
    const StrokeStyle a = StrokeStyle::solid({1.0f, 0.0f, 0.0f, 1.0f});
    const StrokeStyle b = StrokeStyle::solid({0.0f, 0.0f, 1.0f, 1.0f});

    AnimatedValue<StrokeStyle> v(b);
    v.key(0, a);
    v.key(60, b);
    v.expression("solid(0, 1, 0, 1)");
    CHECK(v.is_animated());

    v.set(a);
    CHECK_FALSE(v.is_animated());
    CHECK_FALSE(v.has_expression());

    const StrokeStyle out = v.evaluate(Frame{99});
    CHECK(out.color.r == doctest::Approx(1.0f));
}

// ===========================================================================
// AnimatedValue<FillStyle> — edge cases
// ===========================================================================

TEST_CASE("AnimatedValue<FillStyle> — default constructed returns default") {
    AnimatedValue<FillStyle> v;
    const FillStyle out = v.evaluate(Frame{0});
    CHECK(out.enabled == true);
    CHECK(out.is_solid());
}

TEST_CASE("AnimatedValue<FillStyle> — set() clears keyframes and expression") {
    const FillStyle a = FillStyle::solid({1.0f, 0.0f, 0.0f, 1.0f});
    const FillStyle b = FillStyle::solid({0.0f, 0.0f, 1.0f, 1.0f});

    AnimatedValue<FillStyle> v(b);
    v.key(0, a);
    v.key(60, b);
    v.expression("solid(0, 1, 0, 1)");
    CHECK(v.is_animated());

    v.set(a);
    CHECK_FALSE(v.is_animated());
    CHECK_FALSE(v.has_expression());

    const FillStyle out = v.evaluate(Frame{99});
    CHECK(out.solid_color.r == doctest::Approx(1.0f));
}
