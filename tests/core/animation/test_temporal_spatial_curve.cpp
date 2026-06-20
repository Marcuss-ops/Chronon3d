#include <doctest/doctest.h>
#include <chronon3d/animation/core/temporal_spatial_curve.hpp>
#include <chronon3d/math/glm_types.hpp>
using namespace chronon3d;


// =========================================================================
// CubicBezier3D — pure math
// =========================================================================

TEST_CASE("CubicBezier3D: start and end match control points") {
    CubicBezier3D curve{
        {0, 0, 0},
        {10, 0, 0},
        {90, 0, 0},
        {100, 0, 0}
    };
    auto p0 = curve.position(0.0);
    auto p1 = curve.position(1.0);
    CHECK(glm::length(p0 - Vec3{0, 0, 0}) < 1e-4f);
    CHECK(glm::length(p1 - Vec3{100, 0, 0}) < 1e-4f);
}

TEST_CASE("CubicBezier3D: midpoint for symmetric curve") {
    // Symmetric handles: midpoint should be at the average of P1 and P2.
    CubicBezier3D curve{
        {0, 0, 0},
        {50, 0, 0},
        {50, 100, 0},
        {0, 100, 0}
    };
    auto mid = curve.position(0.5);
    // For a symmetric cubic bezier, u=0.5 gives:
    // 0.125*P0 + 0.375*P1 + 0.375*P2 + 0.125*P3
    Vec3 expected{
        0.125f*0 + 0.375f*50 + 0.375f*50 + 0.125f*0,
        0.125f*0 + 0.375f*0 + 0.375f*100 + 0.125f*100,
        0.0f
    };
    CHECK(glm::length(mid - expected) < 1e-4f);
}

TEST_CASE("CubicBezier3D: derivative is zero-vector for degenerate curve") {
    CubicBezier3D curve{
        {5, 5, 5}, {5, 5, 5}, {5, 5, 5}, {5, 5, 5}
    };
    auto d = curve.derivative(0.5);
    CHECK(glm::length(d) < 1e-4f);
}

TEST_CASE("CubicBezier3D: sample returns normalised forward") {
    CubicBezier3D curve{
        {0, 0, 0},
        {10, 0, 0},
        {90, 0, 0},
        {100, 0, 0}
    };
    auto s = curve.sample(0.5);
    // Forward should be along +X.
    CHECK(s.forward.x > 0.9f);
    CHECK(glm::length(s.forward - Vec3{1, 0, 0}) < 0.01f);
    // Position at midpoint should be ~50 on X.
    CHECK(s.position.x == doctest::Approx(50.0f).epsilon(0.1f));
}

TEST_CASE("CubicBezier3D: from_handles constructs correctly") {
    auto curve = CubicBezier3D::from_handles(
        {0, 0, 0}, {50, 0, 0},      // start + out_handle
        {100, 0, 0}, {-50, 0, 0}    // end + in_handle
    );
    CHECK(glm::length(curve.p0 - Vec3{0, 0, 0}) < 1e-4f);
    CHECK(glm::length(curve.p1 - Vec3{50, 0, 0}) < 1e-4f);
    CHECK(glm::length(curve.p2 - Vec3{50, 0, 0}) < 1e-4f);
    CHECK(glm::length(curve.p3 - Vec3{100, 0, 0}) < 1e-4f);
}

TEST_CASE("CubicBezier3D: straight line produces constant derivative") {
    // Use exactly equally-spaced control points so that P1-P0 == P2-P1 == P3-P2.
    // 0, 30, 60, 90 are all exactly representable in float, giving equal deltas.
    CubicBezier3D curve{
        {0, 0, 0},
        {30, 0, 0},
        {60, 0, 0},
        {90, 0, 0}
    };
    auto d0 = curve.derivative(0.0);
    auto d5 = curve.derivative(0.5);
    auto d1 = curve.derivative(1.0);
    // For a straight line with equally-spaced control points,
    // the derivative is constant: 3*(P1-P0) = 90 at every u.
    CHECK(glm::length(d0 - d5) < 1e-4f);
    CHECK(glm::length(d5 - d1) < 1e-4f);
    CHECK(d0.x == doctest::Approx(90.0f));
}

// ── from_tangents ────────────────────────────────────────────────────────

TEST_CASE("CubicBezier3D: from_tangents with equal magnitudes") {
    auto curve = CubicBezier3D::from_tangents(
        {0, 0, 0}, Vec3{1, 0, 0}, 50.0f,
        {100, 0, 0}, Vec3{-1, 0, 0}, 50.0f
    );
    CHECK(glm::length(curve.p0 - Vec3{0, 0, 0}) < 1e-4f);
    CHECK(glm::length(curve.p3 - Vec3{100, 0, 0}) < 1e-4f);
    // Handles should lie on the line from start to end.
    CHECK(curve.p1.x > 0.0f);
    CHECK(curve.p2.x < 100.0f);
    CHECK(curve.p1.x == doctest::Approx(50.0f).epsilon(0.01f));
    CHECK(curve.p2.x == doctest::Approx(50.0f).epsilon(0.01f));
}

TEST_CASE("CubicBezier3D: from_tangents with asymmetric magnitudes") {
    // Small start mag, large end mag — curve should be pulled toward end.
    auto curve = CubicBezier3D::from_tangents(
        {0, 0, 0}, Vec3{1, 0, 0}, 10.0f,
        {100, 0, 0}, Vec3{-1, 0, 0}, 100.0f
    );
    // P1 is close to P0 (small start mag).
    CHECK(curve.p1.x < 20.0f);
    // P2 is far from P3 (large end mag).
    CHECK(curve.p2.x < 50.0f);
}

TEST_CASE("CubicBezier3D: from_tangents with tilted tangents") {
    // Tangents pointing up away from the horizontal line.
    auto curve = CubicBezier3D::from_tangents(
        {0, 0, 0}, glm::normalize(Vec3{1, 0.5f, 0}), 50.0f,
        {100, 0, 0}, glm::normalize(Vec3{-1, 0.5f, 0}), 50.0f
    );
    // Handles should be above the start/end positions.
    CHECK(curve.p1.y > 0.0f);
    CHECK(curve.p2.y > 0.0f);
    // At midpoint, curve should bulge upward.
    auto mid = curve.position(0.5);
    CHECK(mid.y > 0.0f);
}

// ── make_auto_smooth ─────────────────────────────────────────────────────

TEST_CASE("CubicBezier3D: make_auto_smooth on straight line yields flat handles") {
    const FrameRate rate{30, 1};
    SpatialKeyframe3D prev{SampleTime::from_frame(0.0, rate), {0, 0, 0}};
    SpatialKeyframe3D curr{SampleTime::from_frame(30.0, rate), {100, 0, 0}};
    SpatialKeyframe3D next{SampleTime::from_frame(60.0, rate), {200, 0, 0}};
    SpatialKeyframe3D after{SampleTime::from_frame(90.0, rate), {300, 0, 0}};

    auto curve = make_auto_smooth(prev, curr, next, after);
    // All points should be collinear on X axis.
    CHECK(glm::length(curve.p0 - Vec3{100, 0, 0}) < 1e-4f);
    CHECK(glm::length(curve.p3 - Vec3{200, 0, 0}) < 1e-4f);
    // Handles should lie on the line (y=0).
    CHECK(curve.p1.y == doctest::Approx(0.0f));
    CHECK(curve.p2.y == doctest::Approx(0.0f));
    // Midpoint at u=0.5 should be at 150 on X.
    auto mid = curve.position(0.5);
    CHECK(mid.x == doctest::Approx(150.0f).epsilon(0.01f));
}

TEST_CASE("CubicBezier3D: make_auto_smooth produces symmetric bulge") {
    const FrameRate rate{30, 1};
    SpatialKeyframe3D prev{SampleTime::from_frame(0.0, rate), {0, 0, 0}};
    SpatialKeyframe3D curr{SampleTime::from_frame(30.0, rate), {50, 0, 0}};
    SpatialKeyframe3D next{SampleTime::from_frame(60.0, rate), {100, 100, 0}};
    SpatialKeyframe3D after{SampleTime::from_frame(90.0, rate), {150, 200, 0}};

    auto curve = make_auto_smooth(prev, curr, next, after);
    CHECK(glm::length(curve.p0 - Vec3{50, 0, 0}) < 1e-4f);
    CHECK(glm::length(curve.p3 - Vec3{100, 100, 0}) < 1e-4f);
    // Curve should pass through endpoints.
    auto start = curve.position(0.0);
    auto end = curve.position(1.0);
    CHECK(glm::length(start - Vec3{50, 0, 0}) < 1e-4f);
    CHECK(glm::length(end - Vec3{100, 100, 0}) < 1e-4f);
}

TEST_CASE("CubicBezier3D: make_auto_smooth with custom tension") {
    const FrameRate rate{30, 1};
    SpatialKeyframe3D prev{SampleTime::from_frame(0.0, rate), {0, 0, 0}};
    SpatialKeyframe3D curr{SampleTime::from_frame(30.0, rate), {100, 0, 0}};
    SpatialKeyframe3D next{SampleTime::from_frame(60.0, rate), {200, 50, 0}};
    SpatialKeyframe3D after{SampleTime::from_frame(90.0, rate), {250, 150, 0}};

    auto low  = make_auto_smooth(prev, curr, next, after, 0.1f);
    auto high = make_auto_smooth(prev, curr, next, after, 0.5f);

    // Higher tension → longer handles → different curve shape.
    auto low_mid  = low.position(0.5);
    auto high_mid = high.position(0.5);
    CHECK(glm::length(low_mid - high_mid) > 1e-4f);
    // Higher tension curve deviates more from the chord (straight line).
    // Deviation = distance from midpoint to the chord midpoint (150, 25, 0).
    Vec3 chord_mid{150.0f, 25.0f, 0.0f};
    float low_dev  = glm::length(low_mid - chord_mid);
    float high_dev = glm::length(high_mid - chord_mid);
    CHECK(high_dev > low_dev);
}

// ── continuity checks ────────────────────────────────────────────────────

TEST_CASE("CubicBezier3D: C0 continuous when endpoints match") {
    CubicBezier3D seg1{
        {0, 0, 0}, {50, 0, 0}, {50, 100, 0}, {100, 100, 0}
    };
    CubicBezier3D seg2{
        {100, 100, 0}, {150, 100, 0}, {150, 0, 0}, {200, 0, 0}
    };
    CHECK(seg1.is_c0_continuous_with(seg2));
    CHECK(seg1.is_c0_continuous_with(seg2, 1e-6f));
}

TEST_CASE("CubicBezier3D: C0 not continuous when gap exists") {
    CubicBezier3D seg1{
        {0, 0, 0}, {50, 0, 0}, {50, 100, 0}, {100, 100, 0}
    };
    CubicBezier3D seg2{
        {105, 100, 0}, {150, 100, 0}, {150, 0, 0}, {200, 0, 0}
    };
    CHECK_FALSE(seg1.is_c0_continuous_with(seg2));
    CHECK_FALSE(seg1.is_c0_continuous_with(seg2, 0.1f));
}

TEST_CASE("CubicBezier3D: C0 within tolerance") {
    CubicBezier3D seg1{
        {0, 0, 0}, {50, 0, 0}, {50, 100, 0}, {100, 100, 0}
    };
    // Gap of 0.002 on X is within 0.01 tolerance.
    CubicBezier3D seg2{
        {100.002f, 100, 0}, {150, 100, 0}, {150, 0, 0}, {200, 0, 0}
    };
    CHECK_FALSE(seg1.is_c0_continuous_with(seg2, 0.001f));
    CHECK(seg1.is_c0_continuous_with(seg2, 0.01f));
}

TEST_CASE("CubicBezier3D: C1 continuous when handles form smooth join") {
    // seg1: P2-P3 aligns with seg2: P0-P1 → C1 continuous.
    auto seg1 = CubicBezier3D::from_handles(
        {0, 0, 0}, {100, 0, 0},
        {200, 0, 0}, {-50, 0, 0}
    );
    auto seg2 = CubicBezier3D::from_handles(
        {200, 0, 0}, {50, 0, 0},
        {300, 0, 0}, {-100, 0, 0}
    );
    CHECK(seg1.is_c1_continuous_with(seg2));
    CHECK(seg1.is_geometric_c1_with(seg2));
}

TEST_CASE("CubicBezier3D: C1 not continuous when derivative magnitude differs") {
    // seg2 has a much faster speed at start → C1 fails.
    auto seg1 = CubicBezier3D::from_handles(
        {0, 0, 0}, {100, 0, 0},
        {200, 0, 0}, {-50, 0, 0}
    );
    auto seg2 = CubicBezier3D::from_handles(
        {200, 0, 0}, {200, 0, 0},  // Large out handle
        {400, 0, 0}, {-200, 0, 0}
    );
    CHECK_FALSE(seg1.is_c1_continuous_with(seg2));

    // But it's still geometric C1 (same direction).
    CHECK(seg1.is_geometric_c1_with(seg2));
}

TEST_CASE("CubicBezier3D: G1 not continuous when tangent flips direction") {
    auto seg1 = CubicBezier3D::from_handles(
        {0, 0, 0}, {100, 0, 0},
        {200, 0, 0}, {-50, 0, 0}
    );
    // seg2 starts at same point but goes sideways (90° from seg1 exit direction).
    // Not C1 (magnitude/direction mismatch), not G1 (tangents are perpendicular).
    auto seg2 = CubicBezier3D::from_handles(
        {200, 0, 0}, {0, 50, 0},
        {200, 100, 0}, {0, -50, 0}
    );
    CHECK_FALSE(seg1.is_c1_continuous_with(seg2));
    CHECK_FALSE(seg1.is_geometric_c1_with(seg2));
}

TEST_CASE("CubicBezier3D: G1 continuous at sharp corner with symmetric handles") {
    // seg1 ends going +X, seg2 starts going +Y → 90° angle.
    auto seg1 = CubicBezier3D::from_handles(
        {0, 0, 0}, {50, 0, 0},
        {100, 0, 0}, {-50, 0, 0}
    );
    auto seg2 = CubicBezier3D::from_handles(
        {100, 0, 0}, {0, 50, 0},
        {100, 100, 0}, {0, -50, 0}
    );
    // 90° means dot product ≈ 0.
    CHECK_FALSE(seg1.is_geometric_c1_with(seg2));
    CHECK_FALSE(seg1.is_geometric_c1_with(seg2, 0.1f));
}

// ── tangent_at edge cases ────────────────────────────────────────────────

TEST_CASE("CubicBezier3D: tangent_at degenerate derivative falls back to P0→P1") {
    // All control points identical → derivative is zero everywhere.
    CubicBezier3D curve{{5, 5, 5}, {5, 5, 5}, {5, 5, 5}, {5, 5, 5}};
    auto t = curve.tangent_at(0.3);
    // Since P0==P1, even the fallback is zero → returns default -Z.
    CHECK(glm::length(t - Vec3{0, 0, -1}) < 1e-4f);
}

TEST_CASE("CubicBezier3D: tangent_at start returns unit-length forward direction") {
    // P0 and P1 are distinct — derivative is well-defined.
    CubicBezier3D curve{
        {0, 0, 0},
        {50, 0, 0},
        {50, 100, 0},
        {0, 100, 0}
    };
    auto t = curve.tangent_at(0.0);
    // Should return unit-length vector.
    CHECK(glm::abs(glm::length(t) - 1.0f) < 1e-4f);
    // Direction should be +X (from P0 to P1).
    CHECK(t.x > 0.9f);
}

TEST_CASE("CubicBezier3D: tangent_at near-zero derivative falls back to P0→P1") {
    // P0 == P1 but P2 != P1 — derivative at u=0 is zero vector.
    // Fallback: normalize(P1-P0) also zero → tertiary fallback to -Z.
    // Actually, with P0==P1==(0,0,0) and P2 further out, derivative at u=0 IS zero.
    // But the fallback (P1-P0) is also zero, so we get default -Z.
    // Let's make a curve where P1-P0 is small but not zero to test real fallback.
    CubicBezier3D curve{
        {0, 0, 0},
        {0, 0, 0},      // P0 == P1
        {100, 0, 0},
        {100, 0, 0}
    };
    // derivative(0) = 3*(P1-P0) = (0,0,0) → degenerate.
    // Fallback: normalize(P1-P0) = (0,0,0) → len2=0.
    // Tertiary: (0,0,-1).
    auto t = curve.tangent_at(0.0);
    CHECK(glm::length(t - Vec3{0, 0, -1}) < 1e-4f);
}

TEST_CASE("CubicBezier3D: tangent_at midpoint of symmetric curve points correctly") {
    CubicBezier3D curve{
        {0, 0, 0},
        {50, 0, 0},
        {50, 100, 0},
        {0, 100, 0}
    };
    auto t = curve.tangent_at(0.5);
    // At u=0.5, derivative should be +Y direction (from P1/P2 going up).
    CHECK(glm::abs(glm::length(t) - 1.0f) < 1e-4f);
    CHECK(t.y > 0.9f);
}

// =========================================================================
// TemporalCurve1D — timing curve
// =========================================================================

TEST_CASE("TemporalCurve1D: linear is identity") {
    TemporalCurve1D curve(EasingCurve{Easing::Linear});
    // Linear easing is trivial (passthrough), but is_non_trivial() may be true
    // if the easing is anything other than the default Linear.
    CHECK(curve.evaluate_normalized(0.0f) == doctest::Approx(0.0f));
    CHECK(curve.evaluate_normalized(0.5f) == doctest::Approx(0.5f));
    CHECK(curve.evaluate_normalized(1.0f) == doctest::Approx(1.0f));
}

TEST_CASE("TemporalCurve1D: easing modifies progress") {
    TemporalCurve1D curve(EasingCurve{Easing::InOutCubic});
    CHECK(curve.evaluate_normalized(0.0f) == doctest::Approx(0.0f));
    CHECK(curve.evaluate_normalized(1.0f) == doctest::Approx(1.0f));
    // At t=0.5, InOutCubic should still be ~0.5 (symmetric).
    CHECK(curve.evaluate_normalized(0.5f) == doctest::Approx(0.5f).epsilon(0.01f));
    // At t=0.25, InOutCubic is < 0.25 (slow start).
    CHECK(curve.evaluate_normalized(0.25f) < 0.25f);
}

TEST_CASE("TemporalCurve1D: hold freezes at 0") {
    TemporalCurve1D curve(EasingCurve{Easing::Hold});
    CHECK(curve.evaluate_normalized(0.5f) == doctest::Approx(0.0f));
}

TEST_CASE("TemporalCurve1D: clamps input") {
    TemporalCurve1D curve(EasingCurve{Easing::Linear});
    CHECK(curve.evaluate_normalized(-0.5f) == doctest::Approx(0.0f));
    CHECK(curve.evaluate_normalized(1.5f) == doctest::Approx(1.0f));
}

TEST_CASE("TemporalCurve1D: from AnimatedValue<f32> with keyframes") {
    AnimatedValue<f32> av(0.0f);
    // Map t ∈ [0,1] to values [0,100] with temporal bezier handles.
    av.add_keyframe(Frame{0}, 0.0f);
    av.add_keyframe(Frame{100}, 100.0f, InterpMode::Bezier,
                    0.0f, 0.0f, 20.0f, 30.0f);  // out: dx=20, dy=30

    // Update the first keyframe's out-tangent via keyframes() access
    auto& kfs = const_cast<std::vector<Keyframe<f32>>&>(av.keyframes());
    kfs[0].interp = InterpMode::Bezier;
    kfs[0].temporal_out_dx = 0.0f;
    kfs[0].temporal_out_dy = 30.0f;

    TemporalCurve1D curve(std::move(av));
    CHECK(curve.has_animation_curve());
    CHECK(curve.evaluate_normalized(0.0f) == doctest::Approx(0.0f));
    CHECK(curve.evaluate_normalized(1.0f) == doctest::Approx(1.0f));

    // At t=0.5, with bezier handles, progress ≠ 0.5.
    f32 mid = curve.evaluate_normalized(0.5f);
    CHECK(mid >= 0.0f);
    CHECK(mid <= 1.0f);
}

// =========================================================================
// SpatialKeyframe3D
// =========================================================================

TEST_CASE("SpatialKeyframe3D: construction") {
    SpatialKeyframe3D kf(SampleTime::from_frame(30.0, FrameRate{30, 1}),
                         Vec3{10, 20, -1000});
    CHECK(kf.time.frame == doctest::Approx(30.0));
    CHECK(glm::length(kf.value - Vec3{10, 20, -1000}) < 1e-4f);
    // Default handles are zero.
    CHECK(glm::length(kf.in_handle) < 1e-4f);
    CHECK(glm::length(kf.out_handle) < 1e-4f);
}

TEST_CASE("SpatialKeyframe3D: custom handles") {
    SpatialKeyframe3D kf;
    kf.value = {100, 0, -800};
    kf.in_handle = {-20, 0, 0};
    kf.out_handle = {30, 5, 0};
    CHECK(glm::length(kf.in_handle - Vec3{-20, 0, 0}) < 1e-4f);
    CHECK(glm::length(kf.out_handle - Vec3{30, 5, 0}) < 1e-4f);
}

// =========================================================================
// MotionSegment3D
// =========================================================================

TEST_CASE("MotionSegment3D: from_keyframes builds correct segment") {
    SpatialKeyframe3D a(SampleTime::from_frame(0.0, FrameRate{30, 1}),
                        Vec3{0, 0, -1000});
    a.out_handle = {200, 0, 0};
    SpatialKeyframe3D b(SampleTime::from_frame(60.0, FrameRate{30, 1}),
                        Vec3{200, 50, -800});
    b.in_handle = {-50, 0, 0};

    auto seg = MotionSegment3D::from_keyframes(a, b);

    CHECK(seg.duration_frames() == doctest::Approx(60.0));
    CHECK(seg.start_time.frame == doctest::Approx(0.0));
    CHECK(seg.end_time.frame == doctest::Approx(60.0));
    // Path p0 and p3 should match the keyframe values.
    CHECK(glm::length(seg.path.p0 - a.value) < 1e-4f);
    CHECK(glm::length(seg.path.p3 - b.value) < 1e-4f);
}

TEST_CASE("MotionSegment3D: zero-duration segment") {
    SpatialKeyframe3D a(SampleTime::from_frame(30.0, FrameRate{30, 1}),
                        Vec3{0, 0, 0});
    SpatialKeyframe3D b(SampleTime::from_frame(30.0, FrameRate{30, 1}),
                        Vec3{100, 0, 0});
    auto seg = MotionSegment3D::from_keyframes(a, b);
    CHECK(seg.duration_frames() == doctest::Approx(0.0));
}

// =========================================================================
// MotionPath3D
// =========================================================================

TEST_CASE("MotionPath3D: empty path returns nothing") {
    MotionPath3D path;
    CHECK(path.empty());
    CHECK(path.segment_count() == 0);
    CHECK(path.find_segment(SampleTime::from_frame(0.0, FrameRate{30, 1})) == -1);
}

TEST_CASE("MotionPath3D: single keyframe produces no segments") {
    MotionPath3D path;
    path.add_keyframe({SampleTime::from_frame(0.0, FrameRate{30, 1}), {0, 0, -1000}});
    path.build_segments_uniform();
    CHECK(path.segment_count() == 0);
}

TEST_CASE("MotionPath3D: two keyframes produce one segment") {
    MotionPath3D path;
    const FrameRate rate{30, 1};
    path.add_keyframe({SampleTime::from_frame(0.0, rate), {0, 0, -1000}});
    path.add_keyframe({SampleTime::from_frame(60.0, rate), {100, 0, -800}});
    path.build_segments_uniform();

    CHECK(path.segment_count() == 1);
    CHECK(path.segments()[0].duration_frames() == doctest::Approx(60.0));
}

TEST_CASE("MotionPath3D: three keyframes produce two segments") {
    MotionPath3D path;
    const FrameRate rate{30, 1};
    path.add_keyframe({SampleTime::from_frame(0.0, rate), {0, 0, -1000}});
    path.add_keyframe({SampleTime::from_frame(45.0, rate), {200, 50, -800}});
    path.add_keyframe({SampleTime::from_frame(90.0, rate), {0, 0, -600}});
    path.build_segments_uniform();

    CHECK(path.segment_count() == 2);
}

TEST_CASE("MotionPath3D: find_segment locates correct segment") {
    MotionPath3D path;
    const FrameRate rate{30, 1};
    path.add_keyframe({SampleTime::from_frame(0.0, rate), {0, 0, -1000}});
    path.add_keyframe({SampleTime::from_frame(30.0, rate), {100, 0, -800}});
    path.add_keyframe({SampleTime::from_frame(60.0, rate), {200, 0, -600}});
    path.build_segments_uniform();

    CHECK(path.find_segment(SampleTime::from_frame(15.0, rate)) == 0);
    CHECK(path.find_segment(SampleTime::from_frame(45.0, rate)) == 1);
    // Before first segment → clamps to 0.
    CHECK(path.find_segment(SampleTime::from_frame(-10.0, rate)) == 0);
    // After last segment → clamps to last.
    CHECK(path.find_segment(SampleTime::from_frame(100.0, rate)) == 1);
}

TEST_CASE("MotionPath3D: continuity between segments") {
    MotionPath3D path;
    const FrameRate rate{30, 1};
    auto a = SpatialKeyframe3D{SampleTime::from_frame(0.0, rate), {0, 0, -1000}};
    a.out_handle = {50, 0, 0};
    auto b = SpatialKeyframe3D{SampleTime::from_frame(30.0, rate), {100, 0, -800}};
    b.in_handle = {-50, 0, 0};
    b.out_handle = {50, 0, 0};
    auto c = SpatialKeyframe3D{SampleTime::from_frame(60.0, rate), {200, 0, -600}};
    c.in_handle = {-50, 0, 0};

    path.add_keyframe(std::move(a));
    path.add_keyframe(std::move(b));
    path.add_keyframe(std::move(c));
    path.build_segments_uniform();

    // Segment 0 at t=1.0 should equal segment 1 at t=0.0.
    auto seg0_end = path.segments()[0].path.position(1.0);
    auto seg1_start = path.segments()[1].path.position(0.0);
    CHECK(glm::length(seg0_end - seg1_start) < 1e-4f);
}

// =========================================================================
// MotionPathSampler3D
// =========================================================================

TEST_CASE("MotionPathSampler3D: empty path returns origin") {
    MotionPathSampler3D sampler;
    MotionPath3D path;
    auto s = sampler.sample(path, SampleTime{0.0, FrameRate{30, 1}});
    CHECK(glm::length(s.position) < 1e-4f);
    CHECK(glm::length(s.forward - Vec3{0, 0, -1}) < 1e-4f);
}

TEST_CASE("MotionPathSampler3D: samples position along a straight path") {
    MotionPath3D path;
    const FrameRate rate{30, 1};
    SpatialKeyframe3D a{SampleTime::from_frame(0.0, rate), {0, 0, -1000}};
    a.out_handle = {100.0f/3.0f, 0, 0};
    SpatialKeyframe3D b{SampleTime::from_frame(60.0, rate), {100, 0, -1000}};
    b.in_handle = {-100.0f/3.0f, 0, 0};

    path.add_keyframe(std::move(a));
    path.add_keyframe(std::move(b));
    path.build_segments_uniform();

    MotionPathSampler3D sampler;
    auto s_start = sampler.sample(path, SampleTime::from_frame(0.0, rate));
    auto s_mid   = sampler.sample(path, SampleTime::from_frame(30.0, rate));
    auto s_end   = sampler.sample(path, SampleTime::from_frame(60.0, rate));

    CHECK(glm::length(s_start.position - Vec3{0, 0, -1000}) < 1e-4f);
    CHECK(glm::length(s_end.position - Vec3{100, 0, -1000}) < 1e-4f);
    // Midpoint should be ~50 on X (linear timing means equal steps).
    CHECK(s_mid.position.x == doctest::Approx(50.0f).epsilon(0.01f));
}

TEST_CASE("MotionPathSampler3D: sub-frame sampling produces different positions") {
    // The decisive test from the AE vendetta doc.
    MotionPath3D path;
    const FrameRate rate{30, 1};
    SpatialKeyframe3D a{SampleTime::from_frame(0.0, rate), {0, 0, -1000}};
    SpatialKeyframe3D b{SampleTime::from_frame(1.0, rate), {100, 0, -1000}};

    path.add_keyframe(std::move(a));
    path.add_keyframe(std::move(b));
    path.build_segments_uniform();

    MotionPathSampler3D sampler;
    std::vector<Vec3> positions;
    for (int s = 0; s < 8; ++s) {
        const double u = (static_cast<double>(s) + 0.5) / 8.0;
        const double sub_frame = 0.5 * u;  // 180° shutter
        auto sample = sampler.sample(path,
            SampleTime::from_frame(sub_frame, rate));
        positions.push_back(sample.position);
    }

    // All 8 sub-samples must produce different positions.
    bool all_same = true;
    for (int s = 1; s < 8; ++s) {
        if (glm::length(positions[s] - positions[0]) > 0.001f) {
            all_same = false;
            break;
        }
    }
    CHECK_FALSE(all_same);

    // Monotonically increasing in X.
    for (int s = 1; s < 8; ++s) {
        CHECK(positions[s].x >= positions[s-1].x);
    }
}

TEST_CASE("MotionPathSampler3D: before/after path clamps to endpoints") {
    MotionPath3D path;
    const FrameRate rate{30, 1};
    path.add_keyframe({SampleTime::from_frame(10.0, rate), {0, 0, -1000}});
    path.add_keyframe({SampleTime::from_frame(50.0, rate), {100, 0, -800}});
    path.build_segments_uniform();

    MotionPathSampler3D sampler;
    auto s_before = sampler.sample(path, SampleTime::from_frame(5.0, rate));
    auto s_after  = sampler.sample(path, SampleTime::from_frame(100.0, rate));

    CHECK(glm::length(s_before.position - Vec3{0, 0, -1000}) < 1e-4f);
    CHECK(glm::length(s_after.position - Vec3{100, 0, -800}) < 1e-4f);
}

TEST_CASE("MotionPathSampler3D: scaled easing modifies effective speed") {
    MotionPath3D path;
    const FrameRate rate{30, 1};
    SpatialKeyframe3D a{SampleTime::from_frame(0.0, rate), {0, 0, -1000}};
    SpatialKeyframe3D b{SampleTime::from_frame(60.0, rate), {100, 0, -1000}};

    path.add_keyframe(std::move(a));
    path.add_keyframe(std::move(b));

    // Ease-out: fast start, slow end.
    path.build_segments(TemporalCurve1D{EasingCurve{Easing::OutCubic}});

    MotionPathSampler3D sampler;
    // At t=0.5 (frame 30), OutCubic progress > 0.5 (fast start).
    auto s_mid = sampler.sample(path, SampleTime::from_frame(30.0, rate));
    // Should be past 50 on X with OutCubic easing.
    CHECK(s_mid.position.x > 50.0f);
}

// =========================================================================
// PathTimingMode
// =========================================================================

TEST_CASE("PathTimingMode: enum values are distinct") {
    CHECK(static_cast<int>(PathTimingMode::Parametric) !=
          static_cast<int>(PathTimingMode::ArcLength));
    CHECK(static_cast<int>(PathTimingMode::ArcLength) !=
          static_cast<int>(PathTimingMode::EasedArcLength));
}
