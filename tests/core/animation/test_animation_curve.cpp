#include <doctest/doctest.h>
#include <chronon3d/animation/core/animated_value.hpp>
using namespace chronon3d;


// ── AnimatedValue<f32> Basic Tests ──────────────────────────────────────────

TEST_CASE("AnimatedValue: empty curve returns default value") {
    AnimatedValue<f32> val(42.0f);
    CHECK(val.evaluate(Frame{0}) == doctest::Approx(42.0f));
    CHECK(val.evaluate(Frame{100}) == doctest::Approx(42.0f));
    CHECK(!val.is_animated());
}

TEST_CASE("AnimatedValue: single keyframe returns that value") {
    AnimatedValue<f32> val;
    val.add_keyframe(Frame{0}, 10.0f);
    CHECK(val.evaluate(Frame{0}) == doctest::Approx(10.0f));
    CHECK(val.evaluate(Frame{50}) == doctest::Approx(10.0f));
}

TEST_CASE("AnimatedValue: linear interpolation (Easing mode)") {
    AnimatedValue<f32> val;
    val.add_keyframe(Frame{0}, 0.0f, EasingCurve{Easing::Linear});
    val.add_keyframe(Frame{100}, 100.0f, EasingCurve{Easing::Linear});

    CHECK(val.evaluate(Frame{0}) == doctest::Approx(0.0f));
    CHECK(val.evaluate(Frame{50}) == doctest::Approx(50.0f));
    CHECK(val.evaluate(Frame{100}) == doctest::Approx(100.0f));
    CHECK(val.evaluate(Frame{25}) == doctest::Approx(25.0f));
}

TEST_CASE("AnimatedValue: hold interpolation") {
    AnimatedValue<f32> val;
    val.add_keyframe(Frame{0}, 10.0f, InterpMode::Hold, 0, 0, 0, 0);
    val.add_keyframe(Frame{50}, 90.0f, InterpMode::Hold, 0, 0, 0, 0);

    CHECK(val.evaluate(Frame{0}) == doctest::Approx(10.0f));
    CHECK(val.evaluate(Frame{10}) == doctest::Approx(10.0f));
    CHECK(val.evaluate(Frame{49}) == doctest::Approx(10.0f));
    // At exact keyframe 50, we get the value at 50
    CHECK(val.evaluate(Frame{50}) == doctest::Approx(90.0f));
}

TEST_CASE("AnimatedValue: before first and after last keyframe") {
    AnimatedValue<f32> val;
    val.add_keyframe(Frame{20}, 20.0f, EasingCurve{Easing::Linear});
    val.add_keyframe(Frame{80}, 80.0f, EasingCurve{Easing::Linear});

    // Before first keyframe → first keyframe value
    CHECK(val.evaluate(Frame{0}) == doctest::Approx(20.0f));
    CHECK(val.evaluate(Frame{10}) == doctest::Approx(20.0f));
    // After last keyframe → last keyframe value
    CHECK(val.evaluate(Frame{90}) == doctest::Approx(80.0f));
    CHECK(val.evaluate(Frame{200}) == doctest::Approx(80.0f));
}

// ── Temporal Bezier Tests ──────────────────────────────────────────────────

TEST_CASE("AnimatedValue: bezier interpolation with temporal tangent handles") {
    AnimatedValue<f32> val;
    // Keyframe at 0 with outgoing tangent pointing forward/up
    val.add_keyframe(Frame{0}, 0.0f, InterpMode::Bezier,
                     0.0f, 0.0f, 10.0f, 10.0f);  // out: dx=10, dy=10
    // Keyframe at 100 with incoming tangent pointing backward/down
    val.add_keyframe(Frame{100}, 100.0f, InterpMode::Bezier,
                     -10.0f, -10.0f, 0.0f, 0.0f);  // in: dx=-10, dy=-10

    // Values should still be monotonic and bounded
    CHECK(val.evaluate(Frame{0}) == doctest::Approx(0.0f));
    CHECK(val.evaluate(Frame{100}) == doctest::Approx(100.0f));

    // At midpoint, value should be around 50 (with slight curve from handles)
    f32 mid = val.evaluate(Frame{50});
    CHECK(mid > 30.0f);
    CHECK(mid < 70.0f);
}

TEST_CASE("AnimatedValue: auto-bezier tangent computation") {
    AnimatedValue<f32> val;
    val.add_keyframe(Frame{0}, 0.0f, InterpMode::AutoBezier, 0, 0, 0, 0);
    val.add_keyframe(Frame{50}, 100.0f, InterpMode::AutoBezier, 0, 0, 0, 0);
    val.add_keyframe(Frame{100}, 0.0f, InterpMode::AutoBezier, 0, 0, 0, 0);

    val.compute_auto_beziers();

    CHECK(val.evaluate(Frame{0}) == doctest::Approx(0.0f));
    CHECK(val.evaluate(Frame{50}) == doctest::Approx(100.0f));
    CHECK(val.evaluate(Frame{100}) == doctest::Approx(0.0f));

    // At 25, value should be > 0 and < 100 (smooth curve)
    f32 q1 = val.evaluate(Frame{25});
    CHECK(q1 > 10.0f);
    CHECK(q1 < 80.0f);

    // Symmetry: value at 75 should mirror value at 25
    f32 q3 = val.evaluate(Frame{75});
    CHECK(q3 == doctest::Approx(q1).epsilon(5.0f));
}

TEST_CASE("AnimatedValue: bezier with zero tangents behaves like linear") {
    AnimatedValue<f32> val;
    val.add_keyframe(Frame{0}, 0.0f, InterpMode::Bezier, 0, 0, 0, 0);
    val.add_keyframe(Frame{100}, 100.0f, InterpMode::Bezier, 0, 0, 0, 0);

    // With zero tangent handles, bezier should approximate linear
    CHECK(val.evaluate(Frame{0}) == doctest::Approx(0.0f));
    CHECK(val.evaluate(Frame{50}) == doctest::Approx(50.0f).epsilon(2.0f));
    CHECK(val.evaluate(Frame{100}) == doctest::Approx(100.0f));
}

TEST_CASE("AnimatedValue: bezier with strong tangent creates ease-in-out") {
    AnimatedValue<f32> val;
    // Strong out-tangent at start (fast start)
    val.add_keyframe(Frame{0}, 0.0f, InterpMode::Bezier,
                     0, 0, 20.0f, 30.0f);   // out: dx=20, dy=30
    // Strong in-tangent at end (slow end)
    val.add_keyframe(Frame{100}, 100.0f, InterpMode::Bezier,
                     -20.0f, -30.0f, 0, 0);  // in: dx=-20, dy=-30

    f32 v25 = val.evaluate(Frame{25});
    f32 v75 = val.evaluate(Frame{75});

    // With ease-in-out, v25 should be > 25 (fast start) and v75 should be < 75 (slow end)
    CHECK(v25 > 25.0f);
    CHECK(v75 < 75.0f);
}

// ── Mixed Interpolation Modes ──────────────────────────────────────────────

TEST_CASE("AnimatedValue: mixed linear and hold segments") {
    AnimatedValue<f32> val;
    val.add_keyframe(Frame{0}, 0.0f, EasingCurve{Easing::Linear});
    val.add_keyframe(Frame{30}, 60.0f, InterpMode::Hold, 0, 0, 0, 0);
    val.add_keyframe(Frame{60}, 60.0f, EasingCurve{Easing::Linear});
    val.add_keyframe(Frame{100}, 100.0f, EasingCurve{Easing::Linear});

    CHECK(val.evaluate(Frame{0}) == doctest::Approx(0.0f));
    CHECK(val.evaluate(Frame{15}) == doctest::Approx(30.0f));
    CHECK(val.evaluate(Frame{30}) == doctest::Approx(60.0f));
    // Hold: value at 45 should still be 60 (Hold on keyframe at 30)
    CHECK(val.evaluate(Frame{45}) == doctest::Approx(60.0f));
    // After hold segment, linear resumes
    CHECK(val.evaluate(Frame{60}) == doctest::Approx(60.0f));
    CHECK(val.evaluate(Frame{100}) == doctest::Approx(100.0f));
}

// ── Keyframe<f32> Tests ────────────────────────────────────────────────────

TEST_CASE("Keyframe<f32>: default construction") {
    Keyframe<f32> kf(Frame{0}, 0.0f);
    CHECK(kf.frame == Frame{0});
    CHECK(kf.value == doctest::Approx(0.0f));
    CHECK(kf.interp == InterpMode::Easing);
    CHECK(!kf.roving);
}

TEST_CASE("Keyframe<f32>: sorting by frame") {
    Keyframe<f32> a(Frame{30}, 1.0f);
    Keyframe<f32> b(Frame{10}, 2.0f);
    Keyframe<f32> c(Frame{20}, 3.0f);

    std::vector<Keyframe<f32>> kfs = {a, b, c};
    std::sort(kfs.begin(), kfs.end());

    CHECK(kfs[0].frame == Frame{10});
    CHECK(kfs[1].frame == Frame{20});
    CHECK(kfs[2].frame == Frame{30});
}

// ── Roving Keyframe Tests ──────────────────────────────────────────────────

TEST_CASE("AnimatedValue: roving keyframe maintains constant velocity") {
    AnimatedValue<f32> val;
    val.add_keyframe(Frame{0}, 0.0f, EasingCurve{Easing::Linear});
    val.add_keyframe(Frame{50}, 50.0f, EasingCurve{Easing::Linear}, true);  // roving
    val.add_keyframe(Frame{100}, 200.0f, EasingCurve{Easing::Linear});

    val.compute_roving();

    // Roving keyframe should be repositioned for constant velocity
    // Velocity should be constant: 200/100 = 2.0 units/frame
    // So the roving keyframe at value=50 should be at frame = 50/2.0 = 25
    const auto& kfs = val.keyframes();
    REQUIRE(kfs.size() == 3);

    f32 roving_frame = static_cast<f32>(kfs[1].frame);
    CHECK(roving_frame == doctest::Approx(25.0f).epsilon(2.0f));
}

TEST_CASE("AnimatedValue: roving between equal values distributes evenly") {
    AnimatedValue<f32> val;
    val.add_keyframe(Frame{0}, 50.0f, EasingCurve{Easing::Linear});
    val.add_keyframe(Frame{50}, 50.0f, EasingCurve{Easing::Linear}, true);
    val.add_keyframe(Frame{100}, 50.0f, EasingCurve{Easing::Linear});

    val.compute_roving();

    const auto& kfs = val.keyframes();
    REQUIRE(kfs.size() == 3);
    CHECK(kfs[1].frame == Frame{50});
}

TEST_CASE("AnimatedValue: multiple consecutive roving keyframes") {
    AnimatedValue<f32> val;
    val.add_keyframe(Frame{0}, 0.0f, EasingCurve{Easing::Linear});
    val.add_keyframe(Frame{30}, 30.0f, EasingCurve{Easing::Linear}, true);
    val.add_keyframe(Frame{60}, 60.0f, EasingCurve{Easing::Linear}, true);
    val.add_keyframe(Frame{100}, 200.0f, EasingCurve{Easing::Linear});

    val.compute_roving();

    const auto& kfs = val.keyframes();
    REQUIRE(kfs.size() == 4);

    // Velocity = 200 / 100 = 2.0
    // Value 30 at t = 30/2.0 = 15
    // Value 60 at t = 60/2.0 = 30
    CHECK(static_cast<f32>(kfs[1].frame) == doctest::Approx(15.0f).epsilon(2.0f));
    CHECK(static_cast<f32>(kfs[2].frame) == doctest::Approx(30.0f).epsilon(2.0f));
}

TEST_CASE("AnimatedValue: first keyframe cannot be roving") {
    AnimatedValue<f32> val;
    val.add_keyframe(Frame{0}, 0.0f, EasingCurve{Easing::Linear}, true);
    val.add_keyframe(Frame{100}, 100.0f, EasingCurve{Easing::Linear});

    val.compute_roving();

    const auto& kfs = val.keyframes();
    CHECK(!kfs[0].roving);
}

TEST_CASE("AnimatedValue: last keyframe cannot be roving") {
    AnimatedValue<f32> val;
    val.add_keyframe(Frame{0}, 0.0f, EasingCurve{Easing::Linear});
    val.add_keyframe(Frame{100}, 100.0f, EasingCurve{Easing::Linear}, true);

    val.compute_roving();

    const auto& kfs = val.keyframes();
    CHECK(!kfs[1].roving);
}

TEST_CASE("AnimatedValue: compute_roving is idempotent") {
    AnimatedValue<f32> val;
    val.add_keyframe(Frame{0}, 0.0f, EasingCurve{Easing::Linear});
    val.add_keyframe(Frame{50}, 50.0f, EasingCurve{Easing::Linear}, true);
    val.add_keyframe(Frame{100}, 200.0f, EasingCurve{Easing::Linear});

    val.compute_roving();
    auto frame_first = val.keyframes()[1].frame;

    val.compute_roving();  // second call should be no-op
    auto frame_second = val.keyframes()[1].frame;

    CHECK(frame_first == frame_second);
}

// ── Spatial Roving Tests ───────────────────────────────────────────────────

TEST_CASE("AnimatedValue: roving with Vec3 type") {
    AnimatedValue<Vec3> val(Vec3{0.0f, 0.0f, 0.0f});
    val.add_keyframe(Frame{0}, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::Linear});
    val.add_keyframe(Frame{50}, Vec3{50.0f, 50.0f, 0.0f}, EasingCurve{Easing::Linear}, true);
    val.add_keyframe(Frame{100}, Vec3{200.0f, 200.0f, 0.0f}, EasingCurve{Easing::Linear});

    val.compute_roving();

    // Vec3 uses spatial-distance roving
    Vec3 v = val.evaluate(Frame{50});
    CHECK(v.x == doctest::Approx(100.0f));
}

// ── Fluent API Tests ───────────────────────────────────────────────────────

TEST_CASE("AnimatedValue: fluent chaining") {
    AnimatedValue<f32> val;
    auto& ref = val
        .key(Frame{0}, 0.0f)
        .key(Frame{60}, 100.0f)
        .key(Frame{120}, 0.0f)
        .set(50.0f);

    // set() changes default but doesn't affect keyframes
    CHECK(ref.is_animated());
    CHECK(ref.keyframes().size() == 3);
    CHECK(ref.keyframes()[1].value == doctest::Approx(100.0f));
}

TEST_CASE("AnimatedValue: shift offsets all keyframes") {
    AnimatedValue<f32> val;
    val.add_keyframe(Frame{0}, 0.0f);
    val.add_keyframe(Frame{50}, 50.0f);
    val.add_keyframe(Frame{100}, 100.0f);

    val.shift(Frame{20});

    const auto& kfs = val.keyframes();
    CHECK(kfs[0].frame == Frame{20});
    CHECK(kfs[1].frame == Frame{70});
    CHECK(kfs[2].frame == Frame{120});

    // Evaluation should still work at shifted positions
    CHECK(val.evaluate(Frame{20}) == doctest::Approx(0.0f));
    CHECK(val.evaluate(Frame{70}) == doctest::Approx(50.0f));
}
