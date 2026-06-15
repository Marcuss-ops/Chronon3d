#include <doctest/doctest.h>
#include <chronon3d/animation/core/animation_curve.hpp>
#include <chronon3d/animation/core/animated_value.hpp>

using namespace chronon3d;

// ── AnimationCurve Basic Tests ───────────────────────────────────────────────

TEST_CASE("AnimationCurve: empty curve returns default value") {
    AnimationCurve curve(42.0f);
    CHECK(curve.evaluate(Frame{0}) == doctest::Approx(42.0f));
    CHECK(curve.evaluate(Frame{100}) == doctest::Approx(42.0f));
    CHECK(!curve.is_animated());
}

TEST_CASE("AnimationCurve: single keyframe returns that value") {
    AnimationCurve curve;
    curve.key(Frame{0}, 10.0f);
    CHECK(curve.evaluate(Frame{0}) == doctest::Approx(10.0f));
    CHECK(curve.evaluate(Frame{50}) == doctest::Approx(10.0f));
}

TEST_CASE("AnimationCurve: linear interpolation") {
    AnimationCurve curve;
    curve.key(Frame{0}, 0.0f, KeyInterp::Linear);
    curve.key(Frame{100}, 100.0f, KeyInterp::Linear);

    CHECK(curve.evaluate(Frame{0}) == doctest::Approx(0.0f));
    CHECK(curve.evaluate(Frame{50}) == doctest::Approx(50.0f));
    CHECK(curve.evaluate(Frame{100}) == doctest::Approx(100.0f));
    CHECK(curve.evaluate(Frame{25}) == doctest::Approx(25.0f));
}

TEST_CASE("AnimationCurve: hold interpolation") {
    AnimationCurve curve;
    curve.key(Frame{0}, 10.0f, KeyInterp::Hold);
    curve.key(Frame{50}, 90.0f, KeyInterp::Hold);

    CHECK(curve.evaluate(Frame{0}) == doctest::Approx(10.0f));
    CHECK(curve.evaluate(Frame{10}) == doctest::Approx(10.0f));
    CHECK(curve.evaluate(Frame{49}) == doctest::Approx(10.0f));
    // At exact keyframe 50, we get the value at 50
    CHECK(curve.evaluate(Frame{50}) == doctest::Approx(90.0f));
}

TEST_CASE("AnimationCurve: before first and after last keyframe") {
    AnimationCurve curve;
    curve.key(Frame{20}, 20.0f, KeyInterp::Linear);
    curve.key(Frame{80}, 80.0f, KeyInterp::Linear);

    // Before first keyframe → first keyframe value
    CHECK(curve.evaluate(Frame{0}) == doctest::Approx(20.0f));
    CHECK(curve.evaluate(Frame{10}) == doctest::Approx(20.0f));
    // After last keyframe → last keyframe value
    CHECK(curve.evaluate(Frame{90}) == doctest::Approx(80.0f));
    CHECK(curve.evaluate(Frame{200}) == doctest::Approx(80.0f));
}

TEST_CASE("AnimationCurve: bezier interpolation with tangent handles") {
    AnimationCurve curve;
    // Keyframe at 0 with outgoing tangent pointing forward/up
    // Keyframe at 100 with incoming tangent pointing backward/down
    curve.key(Frame{0}, 0.0f, KeyInterp::Bezier,
              Tangent{0.0f, 0.0f}, Tangent{10.0f, 10.0f});
    curve.key(Frame{100}, 100.0f, KeyInterp::Bezier,
              Tangent{-10.0f, -10.0f}, Tangent{0.0f, 0.0f});

    // Values should still be monotonic and bounded
    CHECK(curve.evaluate(Frame{0}) == doctest::Approx(0.0f));
    CHECK(curve.evaluate(Frame{100}) == doctest::Approx(100.0f));

    // At midpoint, value should be around 50 (with slight curve from handles)
    f32 mid = curve.evaluate(Frame{50});
    CHECK(mid > 30.0f);
    CHECK(mid < 70.0f);
}

TEST_CASE("AnimationCurve: auto-bezier tangent computation") {
    AnimationCurve curve;
    curve.key(Frame{0}, 0.0f, KeyInterp::AutoBezier);
    curve.key(Frame{50}, 100.0f, KeyInterp::AutoBezier);
    curve.key(Frame{100}, 0.0f, KeyInterp::AutoBezier);

    curve.compute_roving();  // triggers auto-bezier computation

    CHECK(curve.evaluate(Frame{0}) == doctest::Approx(0.0f));
    CHECK(curve.evaluate(Frame{50}) == doctest::Approx(100.0f));
    CHECK(curve.evaluate(Frame{100}) == doctest::Approx(0.0f));

    // At 25, value should be > 0 and < 100 (smooth curve)
    f32 q1 = curve.evaluate(Frame{25});
    CHECK(q1 > 10.0f);
    CHECK(q1 < 80.0f);

    // Symmetry: value at 75 should mirror value at 25
    f32 q3 = curve.evaluate(Frame{75});
    CHECK(q3 == doctest::Approx(q1).epsilon(5.0f));
}

// ── Tangent Tests ────────────────────────────────────────────────────────────

TEST_CASE("Tangent: default is zero") {
    Tangent t;
    CHECK(t.dx == doctest::Approx(0.0f));
    CHECK(t.dy == doctest::Approx(0.0f));
    CHECK(t.is_zero());
}

TEST_CASE("Tangent: non-zero check") {
    Tangent t(5.0f, 10.0f);
    CHECK(!t.is_zero());
}

// ── AnimationKeyframe Tests ──────────────────────────────────────────────────

TEST_CASE("AnimationKeyframe: default construction") {
    AnimationKeyframe kf;
    CHECK(kf.frame == Frame{0});
    CHECK(kf.value == doctest::Approx(0.0f));
    CHECK(kf.interp == KeyInterp::Linear);
    CHECK(!kf.roving);
}

TEST_CASE("AnimationKeyframe: sorting by frame") {
    AnimationKeyframe a(Frame{30}, 1.0f);
    AnimationKeyframe b(Frame{10}, 2.0f);
    AnimationKeyframe c(Frame{20}, 3.0f);

    std::vector<AnimationKeyframe> kfs = {a, b, c};
    std::sort(kfs.begin(), kfs.end());

    CHECK(kfs[0].frame == Frame{10});
    CHECK(kfs[1].frame == Frame{20});
    CHECK(kfs[2].frame == Frame{30});
}

// ── Roving Keyframe Tests ────────────────────────────────────────────────────

TEST_CASE("AnimationCurve: roving keyframe maintains constant velocity") {
    AnimationCurve curve;
    curve.key(Frame{0}, 0.0f, KeyInterp::Linear);
    curve.key(Frame{50}, 50.0f, KeyInterp::Linear).roving();  // should auto-adjust
    curve.key(Frame{100}, 200.0f, KeyInterp::Linear);

    curve.compute_roving();

    // Roving keyframe should be repositioned for constant velocity
    // Velocity should be constant: 200/100 = 2.0 units/frame
    // So the roving keyframe at value=50 should be at frame = 50/2.0 = 25
    const auto& kfs = curve.keyframes();
    REQUIRE(kfs.size() == 3);

    // The roving keyframe's frame should have changed
    // (it was at 50, should now be around 25 for constant velocity)
    f32 roving_frame = static_cast<f32>(kfs[1].frame);
    CHECK(roving_frame == doctest::Approx(25.0f).epsilon(2.0f));
}

TEST_CASE("AnimationCurve: roving between equal values distributes evenly") {
    AnimationCurve curve;
    curve.key(Frame{0}, 50.0f, KeyInterp::Linear);
    curve.key(Frame{50}, 50.0f, KeyInterp::Linear).roving();  // same value
    curve.key(Frame{100}, 50.0f, KeyInterp::Linear);

    curve.compute_roving();

    const auto& kfs = curve.keyframes();
    REQUIRE(kfs.size() == 3);

    // Zero value change → distribute frames evenly
    CHECK(kfs[1].frame == Frame{50});
}

TEST_CASE("AnimationCurve: multiple consecutive roving keyframes") {
    AnimationCurve curve;
    curve.key(Frame{0}, 0.0f, KeyInterp::Linear);
    curve.key(Frame{30}, 30.0f, KeyInterp::Linear).roving();
    curve.key(Frame{60}, 60.0f, KeyInterp::Linear).roving();
    curve.key(Frame{100}, 200.0f, KeyInterp::Linear);

    curve.compute_roving();

    const auto& kfs = curve.keyframes();
    REQUIRE(kfs.size() == 4);

    // Velocity = 200 / 100 = 2.0
    // Value 30 at t = 30/2.0 = 15
    // Value 60 at t = 60/2.0 = 30
    CHECK(kfs[1].frame == doctest::Approx(15.0f).epsilon(2.0f));
    CHECK(kfs[2].frame == doctest::Approx(30.0f).epsilon(2.0f));
}

TEST_CASE("AnimationCurve: first keyframe cannot be roving") {
    AnimationCurve curve;
    curve.key(Frame{0}, 0.0f, KeyInterp::Linear).roving();
    curve.key(Frame{100}, 100.0f, KeyInterp::Linear);

    curve.compute_roving();

    const auto& kfs = curve.keyframes();
    // First keyframe is unmarked from roving since there's no left anchor
    CHECK(!kfs[0].roving);
}

TEST_CASE("AnimationCurve: last keyframe cannot be roving") {
    AnimationCurve curve;
    curve.key(Frame{0}, 0.0f, KeyInterp::Linear);
    curve.key(Frame{100}, 100.0f, KeyInterp::Linear).roving();

    curve.compute_roving();

    const auto& kfs = curve.keyframes();
    // Last keyframe is unmarked from roving since there's no right anchor
    CHECK(!kfs[1].roving);
}

TEST_CASE("AnimationCurve: compute_roving is idempotent") {
    AnimationCurve curve;
    curve.key(Frame{0}, 0.0f, KeyInterp::Linear);
    curve.key(Frame{50}, 50.0f, KeyInterp::Linear).roving();
    curve.key(Frame{100}, 200.0f, KeyInterp::Linear);

    curve.compute_roving();
    auto frame_first = curve.keyframes()[1].frame;

    curve.compute_roving();  // second call should be no-op
    auto frame_second = curve.keyframes()[1].frame;

    CHECK(frame_first == frame_second);
}

// ── AnimatedValue Roving Tests ───────────────────────────────────────────────

TEST_CASE("AnimatedValue: roving flag on keyframes") {
    AnimatedValue<f32> val(0.0f);
    val.key(Frame{0}, 0.0f);
    val.key(Frame{50}, 50.0f);
    val.roving();  // marks last keyframe as roving
    val.key(Frame{100}, 200.0f);

    val.compute_roving();

    // After roving, the keyframe at value=50 should have its frame adjusted
    // Velocity = 200/100 = 2.0, so value 50 at frame 25
    f32 v_at_25 = val.evaluate(Frame{25});
    CHECK(v_at_25 == doctest::Approx(50.0f).epsilon(2.0f));
}

TEST_CASE("AnimatedValue: roving with Vec3 type") {
    AnimatedValue<Vec3> val(Vec3{0.0f, 0.0f, 0.0f});
    val.key(Frame{0}, Vec3{0.0f, 0.0f, 0.0f});
    val.key(Frame{50}, Vec3{50.0f, 50.0f, 0.0f}).roving();
    val.key(Frame{100}, Vec3{200.0f, 200.0f, 0.0f});

    val.compute_roving();

    // Vec3 uses spatial-distance roving (glm::length): the keyframe at value 50
    // moves to frame 25 (frac = 70.71/282.84 = 0.25), so evaluate at frame 50
    // interpolates between (50,50,0) at frame 25 and (200,200,0) at frame 100 → (100,100,0)
    Vec3 v = val.evaluate(Frame{50});
    CHECK(v.x == doctest::Approx(100.0f));
}

// ── Fluent API Tests ─────────────────────────────────────────────────────────

TEST_CASE("AnimationCurve: fluent chaining") {
    AnimationCurve curve;
    auto& ref = curve
        .key(Frame{0}, 0.0f)
        .key(Frame{60}, 100.0f, KeyInterp::Bezier, Tangent{-5, 0}, Tangent{5, 0})
        .key(Frame{120}, 0.0f)
        .roving()
        .set(50.0f);

    // set() changes default but doesn't affect keyframes
    CHECK(ref.is_animated());
    CHECK(ref.size() == 3);
}

TEST_CASE("AnimationCurve: shift offsets all keyframes") {
    AnimationCurve curve;
    curve.key(Frame{0}, 0.0f);
    curve.key(Frame{50}, 50.0f);
    curve.key(Frame{100}, 100.0f);

    curve.shift(Frame{20});

    const auto& kfs = curve.keyframes();
    CHECK(kfs[0].frame == Frame{20});
    CHECK(kfs[1].frame == Frame{70});
    CHECK(kfs[2].frame == Frame{120});

    // Evaluation should still work at shifted positions
    CHECK(curve.evaluate(Frame{20}) == doctest::Approx(0.0f));
    CHECK(curve.evaluate(Frame{70}) == doctest::Approx(50.0f));
}

// ── Cubic Bezier with Tangent Handles ────────────────────────────────────────

TEST_CASE("AnimationCurve: bezier with zero tangents behaves like linear") {
    AnimationCurve curve;
    curve.key(Frame{0}, 0.0f, KeyInterp::Bezier, Tangent{0, 0}, Tangent{0, 0});
    curve.key(Frame{100}, 100.0f, KeyInterp::Bezier, Tangent{0, 0}, Tangent{0, 0});

    // With zero tangent handles, bezier should approximate linear
    CHECK(curve.evaluate(Frame{0}) == doctest::Approx(0.0f));
    CHECK(curve.evaluate(Frame{50}) == doctest::Approx(50.0f).epsilon(2.0f));
    CHECK(curve.evaluate(Frame{100}) == doctest::Approx(100.0f));
}

TEST_CASE("AnimationCurve: bezier with strong tangent creates ease-in-out") {
    AnimationCurve curve;
    // Strong out-tangent at start (fast start)
    // Strong in-tangent at end (slow end)
    curve.key(Frame{0}, 0.0f, KeyInterp::Bezier,
              Tangent{0, 0}, Tangent{20, 30.0f});
    curve.key(Frame{100}, 100.0f, KeyInterp::Bezier,
              Tangent{-20, -30.0f}, Tangent{0, 0});

    f32 v25 = curve.evaluate(Frame{25});
    f32 v75 = curve.evaluate(Frame{75});

    // With ease-in-out, v25 should be > 25 (fast start) and v75 should be < 75 (slow end)
    // This creates an S-curve effect
    CHECK(v25 > 25.0f);
    CHECK(v75 < 75.0f);
}

// ── Mixed Interpolation Modes ────────────────────────────────────────────────

TEST_CASE("AnimationCurve: mixed linear and hold segments") {
    AnimationCurve curve;
    curve.key(Frame{0}, 0.0f, KeyInterp::Linear);
    curve.key(Frame{30}, 60.0f, KeyInterp::Hold);    // hold from 30 → value stays 60
    curve.key(Frame{60}, 60.0f, KeyInterp::Linear);  // resume linear
    curve.key(Frame{100}, 100.0f, KeyInterp::Linear);

    CHECK(curve.evaluate(Frame{0}) == doctest::Approx(0.0f));
    CHECK(curve.evaluate(Frame{15}) == doctest::Approx(30.0f));
    CHECK(curve.evaluate(Frame{30}) == doctest::Approx(60.0f));
    // Hold: value at 45 should still be 60 (Hold on keyframe at 30)
    CHECK(curve.evaluate(Frame{45}) == doctest::Approx(60.0f));
    // After hold segment, linear resumes
    CHECK(curve.evaluate(Frame{60}) == doctest::Approx(60.0f));
    CHECK(curve.evaluate(Frame{100}) == doctest::Approx(100.0f));
}
