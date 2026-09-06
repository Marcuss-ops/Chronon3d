// test_temporal_spatial_curve_timing_paths.cpp — Split from
// test_temporal_spatial_curve.cpp: TemporalCurve1D, SpatialKeyframe3D,
// MotionSegment3D, MotionPath3D, MotionPathSampler3D and PathTimingMode
// coverage.
#include <doctest/doctest.h>
#include <chronon3d/animation/core/temporal_spatial_curve.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <cmath>
#include <vector>
using namespace chronon3d;

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
