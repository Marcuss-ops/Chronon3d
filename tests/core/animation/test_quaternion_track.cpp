#include <doctest/doctest.h>
#include <chronon3d/animation/core/quaternion_track.hpp>
#include <chronon3d/animation/effects/animated_transform.hpp>
#include <chronon3d/math/glm_types.hpp>

using namespace chronon3d;

// =========================================================================
// AnimatedQuat — quaternion track with slerp
// =========================================================================

TEST_CASE("AnimatedQuat: default value returned when no keyframes") {
    AnimatedQuat q(Quat{1.0f, 0.0f, 0.0f, 0.0f});
    Quat result = q.evaluate(Frame{0});
    CHECK(result.w == doctest::Approx(1.0f));
    CHECK(result.x == doctest::Approx(0.0f));
    CHECK(result.y == doctest::Approx(0.0f));
    CHECK(result.z == doctest::Approx(0.0f));
}

TEST_CASE("AnimatedQuat: single keyframe holds value") {
    AnimatedQuat q;
    // 90° around Y → should rotate (0,0,-1) to a direction on XZ plane
    q.key(Frame{0}, glm::angleAxis(glm::radians(90.0f), Vec3{0, 1, 0}));
    Quat result = q.evaluate(Frame{100});

    Vec3 forward = result * Vec3{0, 0, -1};
    // After 90° Y rotation, the forward vector should be perpendicular to Z
    CHECK(std::abs(forward.z) < 0.01f);
    // X should be non-zero (direction depends on GLM convention)
    CHECK(std::abs(forward.x) > 0.9f);
}

TEST_CASE("AnimatedQuat: slerp produces correct midpoint") {
    AnimatedQuat q;
    // Identity at frame 0, 90° around Y at frame 60
    q.key(Frame{0}, Quat{1.0f, 0.0f, 0.0f, 0.0f})
     .key(Frame{60}, glm::angleAxis(glm::radians(90.0f), Vec3{0, 1, 0}));

    Quat result = q.evaluate(Frame{30});
    // Midpoint should be a 45° rotation around Y
    // GLM right-hand rule maps +90°Y to negative X for forward (0,0,-1)
    // Use abs for magnitude check
    Vec3 forward = result * Vec3{0, 0, -1};
    f32 xz_angle = std::atan2(std::abs(forward.x), -forward.z);
    CHECK(xz_angle == doctest::Approx(glm::radians(45.0f)).epsilon(0.02f));
}

TEST_CASE("AnimatedQuat: slerp shortest path avoids long rotation") {
    // Two tests: 359°→0° and 170°→-170° should both take the short path
    AnimatedQuat q;

    // Test 1: 359°→0° (shortest = 1°, not 359°)
    q.key(Frame{0}, glm::angleAxis(glm::radians(359.0f), Vec3{0, 1, 0}))
     .key(Frame{60}, glm::angleAxis(glm::radians(0.0f), Vec3{0, 1, 0}));

    // Without shortest-path, midpoint would be at ~179.5° rotation
    // With shortest-path, midpoint should be near identity (only 0.5° rotation)
    Quat result = q.evaluate(Frame{30});
    Vec3 forward = result * Vec3{0, 0, -1};
    // Forward should be very close to (0,0,-1) since we only rotated 0.5°
    CHECK(std::abs(forward.x) < 0.01f);
    CHECK(forward.z == doctest::Approx(-1.0f).epsilon(0.01f));
}

TEST_CASE("AnimatedQuat: shortest path handles 170°→-170° correctly") {
    AnimatedQuat q;
    // 170° and -170° are 20° apart via the short path (through 180°)
    Quat a = glm::angleAxis(glm::radians(170.0f), Vec3{0, 1, 0});
    Quat b = glm::angleAxis(glm::radians(-170.0f), Vec3{0, 1, 0});

    q.key(Frame{0}, a)
     .key(Frame{60}, b);

    Quat result = q.evaluate(Frame{30});

    // Midpoint on the 20° arc through 180° should give 180° rotation
    // Forward vector should point straight back (+Z)
    Vec3 forward = result * Vec3{0, 0, -1};
    CHECK(forward.z > 0.9f);  // Points in +Z direction (behind)
}

TEST_CASE("AnimatedQuat: easing affects interpolation speed") {
    AnimatedQuat q_linear, q_eased;
    // Identity → 90° around Y
    Quat a{1.0f, 0.0f, 0.0f, 0.0f};
    Quat b = glm::angleAxis(glm::radians(90.0f), Vec3{0, 1, 0});

    // NOTE: easing must be on the LEFT keyframe (controls outgoing segment)
    q_linear.key(Frame{0}, a, EasingCurve{Easing::Linear}).key(Frame{60}, b);
    q_eased.key(Frame{0}, a, EasingCurve{Easing::InOutCubic}).key(Frame{60}, b);

    // At t=0.25 (frame 15): linear=25%, eased<25% (slow start)
    Quat ql = q_linear.evaluate(Frame{15});
    Quat qe = q_eased.evaluate(Frame{15});

    // Linear should have rotated more than eased
    Vec3 fwd_linear = ql * Vec3{0, 0, -1};
    Vec3 fwd_eased  = qe * Vec3{0, 0, -1};

    // At 25% progress, linear rotates 22.5°, eased < 22.5°
    // Linear forward should have larger X component (more rotated away from -Z)
    CHECK(std::abs(fwd_linear.x) > std::abs(fwd_eased.x));
}

TEST_CASE("AnimatedQuat: sub-frame evaluation produces different results") {
    AnimatedQuat q;
    const FrameRate rate{30, 1};
    q.key(SampleTime::from_frame(0.0, rate), Quat{1.0f, 0.0f, 0.0f, 0.0f})
     .key(SampleTime::from_frame(1.0, rate),
          glm::angleAxis(glm::radians(90.0f), Vec3{0, 1, 0}));

    // 8 sub-frame samples should all be different
    std::vector<Vec3> forwards;
    for (int s = 0; s < 8; ++s) {
        const double u = (static_cast<double>(s) + 0.5) / 8.0;
        const double sub_frame = 0.5 * u;
        Quat qv = q.evaluate(SampleTime::from_frame(sub_frame, rate));
        forwards.push_back(qv * Vec3{0, 0, -1});
    }

    bool all_same = true;
    for (int s = 1; s < 8; ++s) {
        if (glm::length(forwards[s] - forwards[0]) > 0.001f) {
            all_same = false;
            break;
        }
    }
    CHECK_FALSE(all_same);
}

TEST_CASE("AnimatedQuat: three keyframes produce smooth interpolation") {
    AnimatedQuat q;
    q.key(Frame{0},  Quat{1.0f, 0.0f, 0.0f, 0.0f})
     .key(Frame{30}, glm::angleAxis(glm::radians(45.0f), Vec3{0, 1, 0}))
     .key(Frame{60}, glm::angleAxis(glm::radians(90.0f), Vec3{0, 1, 0}));

    // Use abs to handle GLM sign convention
    Vec3 fwd15 = q.evaluate(Frame{15}) * Vec3{0, 0, -1};
    f32 angle15 = std::atan2(std::abs(fwd15.x), -fwd15.z);
    CHECK(angle15 > 0.0f);
    CHECK(angle15 < glm::radians(45.0f));

    // Frame 45: halfway between 45° and 90° = 67.5°
    Vec3 fwd45 = q.evaluate(Frame{45}) * Vec3{0, 0, -1};
    f32 angle45 = std::atan2(std::abs(fwd45.x), -fwd45.z);
    CHECK(angle45 > glm::radians(45.0f));
    CHECK(angle45 < glm::radians(90.0f));
}

TEST_CASE("AnimatedQuat: is_animated and is_time_dependent") {
    AnimatedQuat q;
    CHECK_FALSE(q.is_animated());
    CHECK_FALSE(q.is_time_dependent());

    q.key(Frame{0}, Quat{1.0f, 0.0f, 0.0f, 0.0f});

    CHECK(q.is_animated());
    CHECK(q.is_time_dependent());
}

TEST_CASE("AnimatedQuat: clear removes all keyframes") {
    AnimatedQuat q;
    q.key(Frame{0}, Quat{1.0f, 0.0f, 0.0f, 0.0f})
     .key(Frame{60}, glm::angleAxis(glm::radians(90.0f), Vec3{0, 1, 0}));

    CHECK(q.size() == 2);
    q.clear();
    CHECK(q.size() == 0);
    CHECK(q.empty());
}

TEST_CASE("AnimatedQuat: set overrides value and clears keyframes") {
    AnimatedQuat q;
    q.key(Frame{0}, Quat{1.0f, 0.0f, 0.0f, 0.0f});
    q.set(glm::angleAxis(glm::radians(45.0f), Vec3{0, 1, 0}));

    CHECK(q.empty());

    // Frame 0 should give the default value (45° Y rotation)
    // Use abs to handle GLM sign convention
    Vec3 forward = q.evaluate(Frame{0}) * Vec3{0, 0, -1};
    f32 angle = std::atan2(std::abs(forward.x), -forward.z);
    CHECK(angle == doctest::Approx(glm::radians(45.0f)).epsilon(0.02f));
}

TEST_CASE("AnimatedQuat: shift offsets all keyframes") {
    AnimatedQuat q;
    const FrameRate rate{30, 1};
    q.key(SampleTime::from_frame(10.0, rate), Quat{1.0f, 0.0f, 0.0f, 0.0f})
     .key(SampleTime::from_frame(50.0, rate),
          glm::angleAxis(glm::radians(90.0f), Vec3{0, 1, 0}));

    q.shift(Frame{20});

    // Frame 0 < shifted first keyframe at frame 30 → first keyframe value
    Vec3 forward = q.evaluate(Frame{0}) * Vec3{0, 0, -1};
    // Should be identity (the first keyframe's value)
    CHECK(forward.z == doctest::Approx(-1.0f).epsilon(0.01f));
}

// =========================================================================
// AnimatedTransform — quaternion rotation integration
// =========================================================================

TEST_CASE("AnimatedTransform: rotation_quat_track preferred over rotation_euler") {
    AnimatedTransform at;

    // Set Euler rotation (should be ignored when quat track is active)
    at.rotation_euler.key(Frame{0}, Vec3{0.0f, 0.0f, 0.0f})
                     .key(Frame{60}, Vec3{0.0f, 90.0f, 0.0f});

    // Set quaternion rotation (90° around Y)
    at.rotation_quat_track.key(Frame{0}, Quat{1.0f, 0.0f, 0.0f, 0.0f})
                          .key(Frame{60},
                               glm::angleAxis(glm::radians(90.0f), Vec3{0, 1, 0}));

    Transform t = at.evaluate(Frame{30});
    // Should use quaternion track (45° around Y)
    // Use abs to handle GLM sign convention
    Vec3 forward = t.rotation * Vec3{0, 0, -1};
    f32 angle = std::atan2(std::abs(forward.x), -forward.z);
    CHECK(angle == doctest::Approx(glm::radians(45.0f)).epsilon(0.02f));
}

TEST_CASE("AnimatedTransform: falls back to rotation_euler when quat track empty") {
    AnimatedTransform at;

    at.rotation_euler.key(Frame{0}, Vec3{0.0f, 0.0f, 0.0f})
                     .key(Frame{60}, Vec3{0.0f, 90.0f, 0.0f});

    // rotation_quat_track is NOT animated

    Transform t = at.evaluate(Frame{30});
    // Should use Euler (45° around Y)
    // Use abs to handle GLM sign convention
    Vec3 forward = t.rotation * Vec3{0, 0, -1};
    f32 angle = std::atan2(std::abs(forward.x), -forward.z);
    CHECK(angle == doctest::Approx(glm::radians(45.0f)).epsilon(0.02f));
}

TEST_CASE("AnimatedTransform: is_animated detects quat track") {
    AnimatedTransform at;
    CHECK_FALSE(at.is_animated());

    at.rotation_quat_track.key(Frame{0}, Quat{1.0f, 0.0f, 0.0f, 0.0f});
    CHECK(at.is_animated());
}

TEST_CASE("AnimatedTransform: is_time_dependent detects quat track") {
    AnimatedTransform at;
    CHECK_FALSE(at.is_time_dependent());

    at.rotation_quat_track.key(Frame{0}, Quat{1.0f, 0.0f, 0.0f, 0.0f});
    CHECK(at.is_time_dependent());
}

TEST_CASE("AnimatedTransform: shift affects quat track") {
    AnimatedTransform at;
    at.rotation_quat_track.key(Frame{0}, Quat{1.0f, 0.0f, 0.0f, 0.0f})
                           .key(Frame{60},
                                glm::angleAxis(glm::radians(90.0f), Vec3{0, 1, 0}));

    at.shift(Frame{30});

    // Frame 15 < first key at frame 30 → first keyframe value (identity)
    Vec3 forward = at.evaluate(Frame{15}).rotation * Vec3{0, 0, -1};
    CHECK(forward.z == doctest::Approx(-1.0f).epsilon(0.01f));
}

// =========================================================================
// Temporal handles (QuatKeyInterp + AutoBezier)
// =========================================================================

TEST_CASE("AnimatedQuat: Hold mode freezes at first keyframe") {
    AnimatedQuat q;
    Quat a{1.0f, 0.0f, 0.0f, 0.0f};
    Quat b = glm::angleAxis(glm::radians(90.0f), Vec3{0, 1, 0});

    // NOTE: Hold must be on the LEFT keyframe (controls the segment to the right)
    q.key(Frame{0}, a, QuatKeyInterp::Hold, 0, 0, 0, 0)
     .key(Frame{60}, b);

    // Hold should freeze at first keyframe value (identity)
    Vec3 fwd = q.evaluate(Frame{30}) * Vec3{0, 0, -1};
    CHECK(fwd.z == doctest::Approx(-1.0f).epsilon(0.01f));
}

TEST_CASE("AnimatedQuat: temporal bezier handles affect speed") {
    AnimatedQuat q_fast, q_slow;
    Quat a{1.0f, 0.0f, 0.0f, 0.0f};
    Quat b = glm::angleAxis(glm::radians(90.0f), Vec3{0, 1, 0});

    // Fast start: out_dy > 0 on left keyframe
    q_fast.key(Frame{0}, a, QuatKeyInterp::Bezier, 0, 0, 20, 0.8f)
          .key(Frame{60}, b);

    // Slow start: out_dy ≈ 0 on left keyframe
    q_slow.key(Frame{0}, a, QuatKeyInterp::Bezier, 0, 0, 20, 0.0f)
          .key(Frame{60}, b);

    Vec3 fwd_fast = q_fast.evaluate(Frame{15}) * Vec3{0, 0, -1};
    Vec3 fwd_slow = q_slow.evaluate(Frame{15}) * Vec3{0, 0, -1};

    // Fast start should have more rotation at 25% of time
    // Use abs to handle GLM sign convention
    f32 rot_fast = std::atan2(std::abs(fwd_fast.x), -fwd_fast.z);
    f32 rot_slow = std::atan2(std::abs(fwd_slow.x), -fwd_slow.z);
    CHECK(rot_fast > rot_slow);
}

TEST_CASE("AnimatedQuat: AutoBezier computes smooth tangents") {
    AnimatedQuat q;
    // Three keyframes with AutoBezier — should auto-compute tangents
    q.key(Frame{0},  Quat{1.0f, 0.0f, 0.0f, 0.0f}, QuatKeyInterp::AutoBezier, 0, 0, 0, 0)
     .key(Frame{30}, glm::angleAxis(glm::radians(45.0f), Vec3{0, 1, 0}),
          QuatKeyInterp::AutoBezier, 0, 0, 0, 0)
     .key(Frame{60}, glm::angleAxis(glm::radians(90.0f), Vec3{0, 1, 0}),
          QuatKeyInterp::AutoBezier, 0, 0, 0, 0);

    // Should not crash and produce valid rotations at each keyframe
    // Use abs to handle GLM sign convention
    Vec3 fwd0  = q.evaluate(Frame{0})  * Vec3{0, 0, -1};
    Vec3 fwd30 = q.evaluate(Frame{30}) * Vec3{0, 0, -1};
    Vec3 fwd60 = q.evaluate(Frame{60}) * Vec3{0, 0, -1};

    f32 rot0  = std::atan2(std::abs(fwd0.x) + 1e-10f, -fwd0.z);
    f32 rot30 = std::atan2(std::abs(fwd30.x), -fwd30.z);
    f32 rot60 = std::atan2(std::abs(fwd60.x), -fwd60.z);

    CHECK(rot0  == doctest::Approx(0.0f).epsilon(0.01f));
    CHECK(rot30 == doctest::Approx(glm::radians(45.0f)).epsilon(0.05f));
    CHECK(rot60 == doctest::Approx(glm::radians(90.0f)).epsilon(0.05f));
}

// =========================================================================
// Spatial Bezier for AnimatedValue<Vec3>
// =========================================================================

TEST_CASE("AnimatedValue<Vec3>: spatial handles create curved path") {
    AnimatedValue<Vec3> pos;
    // Three points: (0,0,0) → (100,0,0) → (200,0,0) with curved handles
    pos.key(Frame{0},  Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::Linear},
            Vec3{50.0f, 30.0f, 0.0f}, Vec3{0.0f, 0.0f, 0.0f})
       .key(Frame{60}, Vec3{100.0f, 0.0f, 0.0f}, EasingCurve{Easing::Linear},
            Vec3{50.0f, 30.0f, 0.0f}, Vec3{-50.0f, 30.0f, 0.0f});

    // Midpoint should have a Y offset due to spatial handles
    Vec3 mid = pos.evaluate(Frame{30});
    // Linear without handles would give (100, 0, 0) — midway between
    // (0,0,0) and (200,0,0) since three keyframes.
    // With handles, midpoint should be somewhere in the middle
    // Actually with keyframes at 0, 60, 120... wait, no: at frame 30
    // we're in the FIRST segment (between keyframe 0 and keyframe 60).
    // The midpoint of the first segment with out_handle=(50,30,0)
    // gives a position with positive Y.
    CHECK(mid.y > 0.0f);  // Curved upward, not flat
    CHECK(mid.x > 0.0f);  // Moving in positive X
}

TEST_CASE("AnimatedValue<Vec3>: without spatial handles, path is straight") {
    AnimatedValue<Vec3> pos;
    pos.key(Frame{0},  Vec3{0.0f, 0.0f, 0.0f})
       .key(Frame{60}, Vec3{100.0f, 0.0f, 0.0f})
       .key(Frame{120}, Vec3{200.0f, 0.0f, 0.0f});

    // Without handles, interpolation is straight-line
    Vec3 mid = pos.evaluate(Frame{30});
    CHECK(mid.y == doctest::Approx(0.0f).epsilon(0.001f));  // No Y displacement
    CHECK(mid.x == doctest::Approx(50.0f));  // Midpoint between 0 and 100 on X
}

TEST_CASE("AnimatedValue<Vec3>: handles cause cubic bezier curve") {
    AnimatedValue<Vec3> pos;
    // Two keyframes with handles that bulge upward
    // Note: symmetric handles cancel out at t=0.5, so check at t=0.25
    pos.key(Frame{0},  Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::Linear},
            Vec3{50.0f, 100.0f, 0.0f}, Vec3{0.0f, 0.0f, 0.0f})
       .key(Frame{60}, Vec3{100.0f, 0.0f, 0.0f}, EasingCurve{Easing::Linear},
            Vec3{0.0f, 0.0f, 0.0f}, Vec3{-50.0f, -100.0f, 0.0f});

    // At t=0.25 (frame 15), the curve should bulge upward before coming back down
    Vec3 qtr = pos.evaluate(Frame{15});
    CHECK(qtr.y > 0.0f);  // Bulges upward at 25%
    
    // At t=0.5 (frame 30), symmetric handles cancel in Y
    Vec3 mid = pos.evaluate(Frame{30});
    CHECK(mid.y == doctest::Approx(0.0f).epsilon(0.001f));  // Symmetric handles cancel
}

TEST_CASE("AnimatedValue<Vec3>: spatial handles with easing combine correctly") {
    AnimatedValue<Vec3> pos;
    pos.key(Frame{0},  Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic},
            Vec3{50.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 0.0f})
       .key(Frame{60}, Vec3{100.0f, 0.0f, 0.0f});

    // Easing affects the speed along the curve (not tested directly here,
    // but should not crash or produce invalid results)
    Vec3 early = pos.evaluate(Frame{10});  // 1/6 of duration
    Vec3 mid   = pos.evaluate(Frame{30});
    Vec3 late  = pos.evaluate(Frame{50});

    CHECK(early.x > 0.0f);
    CHECK(mid.x > early.x);
    CHECK(late.x > mid.x);
}

// =========================================================================
// quat_from_euler_degrees utility
// =========================================================================

TEST_CASE("quat_from_euler_degrees: matches glm::quat(radians(...))") {
    Vec3 euler{30.0f, 45.0f, 60.0f};
    Quat expected = glm::quat(glm::radians(euler));
    Quat result = quat_from_euler_degrees(euler);
    // q and -q represent the same rotation
    CHECK(glm::abs(glm::dot(expected, result)) > 0.999f);
}

TEST_CASE("quat_from_euler_degrees: zero euler gives identity") {
    Quat q = quat_from_euler_degrees(Vec3{0.0f});
    CHECK(q.w == doctest::Approx(1.0f));
    CHECK(q.x == doctest::Approx(0.0f));
    CHECK(q.y == doctest::Approx(0.0f));
    CHECK(q.z == doctest::Approx(0.0f));
}

TEST_CASE("quat_from_euler_degrees: 90° yaw produces correct rotation") {
    Quat q = quat_from_euler_degrees(Vec3{0.0f, 90.0f, 0.0f});
    Vec3 forward = q * Vec3{0, 0, -1};
    // After 90° Y rotation, forward should be perpendicular to Z
    CHECK(std::abs(forward.z) < 0.01f);
    CHECK(std::abs(forward.x) > 0.9f);
}
