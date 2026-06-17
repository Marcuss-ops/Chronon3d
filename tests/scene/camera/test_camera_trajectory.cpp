// ==============================================================================
// tests/scene/camera/test_camera_trajectory.cpp
//
// PR1 — Camera V1 foundation hardening tests.
//
// 10 TEST_CASEs validating CameraTrajectory + ArcLengthTable:
//   1.  Linear segment uses segment indices (not front/back)
//   2.  Bézier derivative matches finite difference
//   3.  Handle contract uses local offsets
//   4.  Sub-frame / fractional frame sampling
//   5.  ArcLengthTable maps 0→0 and total→1
//   6.  ArcLengthTable midpoint uses LUT entries (not s/total)
//   7.  Arc-length spacing CV < 0.03 (uniform-speed quality)
//   8.  Multi-segment trajectory respects every boundary
//   9.  Hold segment preserves exact position
//  10.  Zero-length segment never emits NaN
// ==============================================================================
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/scene/camera/camera_v1/camera_trajectory.hpp>
#include <chronon3d/scene/camera/camera_v1/arc_length_table.hpp>

#include <cmath>
#include <vector>
#include <algorithm>

namespace {

using namespace chronon3d::camera_v1;

constexpr float kEps = 1e-4f;

inline bool approx(float a, float b, float tol = kEps) {
    return std::abs(a - b) <= tol;
}

inline bool approx_vec(Vec3 a, Vec3 b, float tol = kEps) {
    return std::abs(a.x - b.x) <= tol
        && std::abs(a.y - b.y) <= tol
        && std::abs(a.z - b.z) <= tol;
}

inline Vec3 make_vec(float x, float y, float z) { return {x, y, z}; }

/// Helper: build a simple 2-point linear trajectory, 30 frames.
inline std::shared_ptr<CameraTrajectory> make_linear(Vec3 from, Vec3 to) {
    CameraTrajectoryBuilder b;
    b.move_to(from).move_to(to).duration_frames(30.0f);
    return b.build();
}

/// Helper: build a 4-point trajectory: Linear + Bezier + CatmullRom.
inline std::shared_ptr<CameraTrajectory> make_mixed() {
    CameraTrajectoryBuilder b;
    b.move_to(make_vec(0, 0, -1000))
     .move_to(make_vec(100, 50, -800)).duration_frames(30)
     .bezier_to({-20, 0, 0}, {30, 10, 0}, make_vec(300, 100, -600)).duration_frames(45)
     .catmull_rom_to(make_vec(500, 0, -400)).duration_frames(60);
    return b.build();
}

// ==============================================================================
// 1 — Linear segment uses segment indices (not front/back).
// ==============================================================================
TEST_CASE("CameraTrajectory linear uses segment indices") {
    // Two segments: P0→P1, P1→P2.  Must NOT lerp between P0↔P2.
    CameraTrajectoryBuilder b;
    b.move_to(make_vec(0, 0, 0))
     .move_to(make_vec(10, 0, 0)).duration_frames(30)
     .move_to(make_vec(20, 0, 0)).duration_frames(30);
    auto tr = b.build();
    REQUIRE(tr->size() == 2);

    // At frame 0:  should be at P0,   tangent toward P1.
    {
        auto ctx = CameraMotionContext::at(chronon3d::Frame{0});
        auto s = tr->sample(ctx);
        CHECK(approx_vec(s.position, make_vec(0, 0, 0)));
        // Tangent points toward P1 (not toward P2 which would be direction (20,0,0)).
        CHECK(s.tangent.x > 0.5f);  // roughly (1,0,0)
        CHECK(approx(s.tangent.y, 0.0f));
    }

    // At frame 30: beginning of second segment → at P1, tangent toward P2.
    {
        // ctx.sample_time.frame = 30.0
        auto ctx = CameraMotionContext::at(chronon3d::Frame{0});
        ctx.sample_time = chronon3d::SampleTime::from_frame(30.0, 30.0);
        auto s = tr->sample(ctx);
        CHECK(approx_vec(s.position, make_vec(10, 0, 0)));
        // Tangent should point toward P2 (20,0,0) not P0 (0,0,0).
        CHECK(s.tangent.x > 0.5f);
    }

    // At frame 60: end of last segment → at P2.
    {
        auto ctx = CameraMotionContext::at(chronon3d::Frame{0});
        ctx.sample_time = chronon3d::SampleTime::from_frame(60.0, 30.0);
        auto s = tr->sample(ctx);
        CHECK(approx_vec(s.position, make_vec(20, 0, 0)));
    }
}

// ==============================================================================
// 2 — Bézier derivative matches finite difference.
// ==============================================================================
TEST_CASE("CameraTrajectory Bezier derivative matches finite difference") {
    CameraTrajectoryBuilder b;
    b.move_to(make_vec(0, 0, -1000))
     .bezier_to({100, 50, 100}, {50, -30, -50}, make_vec(300, 80, -600))
     .duration_frames(60);
    auto tr = b.build();

    // Sample position at t=0.5 and at t=0.5+dt, compute finite-difference tangent.
    auto ctx = CameraMotionContext::at(chronon3d::Frame{0});
    ctx.sample_time = chronon3d::SampleTime::from_frame(30.0, 30.0);
    auto s0 = tr->sample(ctx);

    ctx.sample_time = chronon3d::SampleTime::from_frame(30.1, 30.0);
    auto s1 = tr->sample(ctx);

    Vec3 fd_tangent = {
        (s1.position.x - s0.position.x) / 0.1f,
        (s1.position.y - s0.position.y) / 0.1f,
        (s1.position.z - s0.position.z) / 0.1f
    };
    float fd_len = std::sqrt(fd_tangent.x*fd_tangent.x + fd_tangent.y*fd_tangent.y + fd_tangent.z*fd_tangent.z);
    Vec3 fd_norm = fd_len > 1e-6f ? Vec3{fd_tangent.x/fd_len, fd_tangent.y/fd_len, fd_tangent.z/fd_len} : Vec3{0,0,0};

    // The analytic tangent (from SpatialBezierPath) should agree with finite difference.
    CHECK(approx(fd_norm.x, s0.tangent.x, 0.02f));
    CHECK(approx(fd_norm.y, s0.tangent.y, 0.02f));
    CHECK(approx(fd_norm.z, s0.tangent.z, 0.02f));
}

// ==============================================================================
// 3 — Handle contract uses local offsets (not absolute world coords).
// ==============================================================================
TEST_CASE("CameraTrajectory handle contract uses local offsets") {
    // Build a bezier where handle_in = offset from destination, handle_out = offset from source.
    // P0 at (0,0,-1000), handle_out = (100, 0, 0) → control point at (100, 0, -1000)
    // P1 at (200, 0, -1000), handle_in  = (-100, 0, 0) → control point at (100, 0, -1000)
    // With both control points at the same location, the curve is a straight line.
    CameraTrajectoryBuilder b;
    b.move_to(make_vec(0,    0, -1000))
     .bezier_to({-100, 0, 0}, {100, 0, 0}, make_vec(200, 0, -1000))
     .duration_frames(30);
    auto tr = b.build();

    auto ctx = CameraMotionContext::at(chronon3d::Frame{0});
    ctx.sample_time = chronon3d::SampleTime::from_frame(15.0, 30.0);
    auto s = tr->sample(ctx);

    // At t=0.5, should be near the midpoint (100, 0, -1000).
    CHECK(approx(s.position.x, 100.0f, 0.1f));
    CHECK(approx(s.position.y, 0.0f));
    CHECK(approx(s.position.z, -1000.0f));
}

// ==============================================================================
// 4 — Sub-frame / fractional frame sampling.
// ==============================================================================
TEST_CASE("CameraTrajectory samples fractional frames") {
    auto tr = make_linear(make_vec(0, 0, 0), make_vec(100, 0, 0));
    // At integer frame 0: position = (0,0,0)
    {
        auto ctx = CameraMotionContext::at(chronon3d::Frame{0});
        auto s = tr->sample(ctx);
        CHECK(approx_vec(s.position, make_vec(0, 0, 0)));
    }
    // At fractional frame 15.0 (halfway): position = (50,0,0)
    {
        auto ctx = CameraMotionContext::at(chronon3d::Frame{0});
        ctx.sample_time = chronon3d::SampleTime::from_frame(15.0, 30.0);
        auto s = tr->sample(ctx);
        CHECK(approx_vec(s.position, make_vec(50, 0, 0)));
    }
    // At frame 30.0 (end): position = (100,0,0)
    {
        auto ctx = CameraMotionContext::at(chronon3d::Frame{0});
        ctx.sample_time = chronon3d::SampleTime::from_frame(30.0, 30.0);
        auto s = tr->sample(ctx);
        CHECK(approx_vec(s.position, make_vec(100, 0, 0)));
    }
    // At frame 15.5 (sub-frame): position should differ from frame 15.
    {
        auto ctx = CameraMotionContext::at(chronon3d::Frame{0});
        ctx.sample_time = chronon3d::SampleTime::from_frame(15.5, 30.0);
        auto s_sub = tr->sample(ctx);

        ctx.sample_time = chronon3d::SampleTime::from_frame(15.0, 30.0);
        auto s_int = tr->sample(ctx);
        CHECK_FALSE(approx_vec(s_sub.position, s_int.position, 0.01f));
    }
}

// ==============================================================================
// 5 — ArcLengthTable maps 0→0 and total→1.
// ==============================================================================
TEST_CASE("ArcLengthTable maps 0 to 0 and total to 1") {
    // Build LUT from a non-trivial curve.
    auto tr = make_mixed();
    // We can't access the LUT directly, so we test via the public API.
    // Instead, construct an ArcLengthTable from known entries.
    std::vector<ArcLengthEntry> entries;
    entries.push_back({0.0f, 0.0f});
    entries.push_back({2.5f, 0.25f});
    entries.push_back({5.0f, 0.5f});
    entries.push_back({7.5f, 0.75f});
    entries.push_back({10.0f, 1.0f});
    ArcLengthTable lut(std::move(entries));

    CHECK(approx(lut.total(), 10.0f));
    CHECK(approx(lut.parameter_at_fraction(0.0f), 0.0f));
    CHECK(approx(lut.parameter_at_fraction(1.0f), 1.0f));

    // t_from_arc_length at 0 and total.
    CHECK(approx(lut.t_from_arc_length(0.0f), 0.0f));
    CHECK(approx(lut.t_from_arc_length(10.0f), 1.0f));
}

// ==============================================================================
// 6 — ArcLengthTable midpoint uses LUT entries (binary search, not s/total).
// ==============================================================================
TEST_CASE("ArcLengthTable midpoint uses LUT entries") {
    // Non-linear spacing: parameter advances slowly at first, then quickly.
    // cumulative: 0, 1, 3, 10    parameter: 0, 0.33, 0.66, 1.0
    // Midpoint arc-length = 5.0. The lower_bound finds entry {3.0, 0.66}, next is {10.0, 1.0}.
    // Then: frac = (5-3)/(10-3) = 2/7 ≈ 0.2857.
    // param = 0.66 + 0.2857*(1.0-0.66) = 0.66 + 0.0971 ≈ 0.757.
    // A naive s/total would give 5/10 = 0.5. This proves the LUT is used.
    std::vector<ArcLengthEntry> entries;
    entries.push_back({0.0f, 0.0f});
    entries.push_back({1.0f, 0.33f});
    entries.push_back({3.0f, 0.66f});
    entries.push_back({10.0f, 1.0f});
    ArcLengthTable lut(std::move(entries));

    float mid = lut.parameter_at_fraction(0.5f);  // arc-length fraction 0.5 → 5.0
    CHECK(mid > 0.65f);   // Must NOT be 0.5 (which would mean s/total).
    CHECK(mid < 0.85f);   // Must be near 0.757.
    CHECK_FALSE(approx(mid, 0.5f));
}

// ==============================================================================
// 7 — Arc-length spacing CV < 0.03 (coefficient of variation of sample distances).
// ==============================================================================
TEST_CASE("Arc-length spacing CV less than 0.03") {
    // Build a trajectory with arc-length parameterization and verify the
    // arc-length LUT produces uniform spacing (low CV on sample distances).
    CameraTrajectoryBuilder b;
    b.move_to(make_vec(0, 0, -1000))
     .bezier_to({200, 100, 200}, {100, -50, -100}, make_vec(400, 50, -800))
     .duration_frames(60)
     .arc_length_parameterized(true);
    auto tr = b.build();
    REQUIRE(tr->arc_length_parameterized());

    // Sample positions at uniform arc-length fractions and measure spacing.
    constexpr int N = 32;
    std::vector<Vec3> positions;
    for (int i = 0; i <= N; ++i) {
        auto ctx = CameraMotionContext::at(chronon3d::Frame{0});
        ctx.sample_time = chronon3d::SampleTime::from_frame(
            static_cast<double>(i) * 60.0 / static_cast<double>(N), 30.0);
        positions.push_back(tr->sample(ctx).position);
    }

    // Compute sample-to-sample distances.
    float sum = 0.0f, sum_sq = 0.0f;
    for (int i = 1; i <= N; ++i) {
        Vec3 d = {
            positions[static_cast<std::size_t>(i)].x - positions[static_cast<std::size_t>(i-1)].x,
            positions[static_cast<std::size_t>(i)].y - positions[static_cast<std::size_t>(i-1)].y,
            positions[static_cast<std::size_t>(i)].z - positions[static_cast<std::size_t>(i-1)].z,
        };
        float dist = std::sqrt(d.x*d.x + d.y*d.y + d.z*d.z);
        sum += dist;
        sum_sq += dist * dist;
    }
    float mean = sum / static_cast<float>(N);
    float variance = (sum_sq / static_cast<float>(N)) - (mean * mean);
    float cv = (mean > 1e-6f) ? std::sqrt(std::max(0.0f, variance)) / mean : 0.0f;

    // CV < 0.03 means sample spacing is very uniform (within 3% variation).
    CHECK(cv < 0.03f);
}

// ==============================================================================
// 8 — Multi-segment trajectory respects every segment boundary.
// ==============================================================================
TEST_CASE("Multi-segment trajectory respects every boundary") {
    auto tr = make_mixed();
    REQUIRE(tr->size() == 3);

    // Segment 0: Linear, frames [0, 30).  At frame 0 → P0.
    {
        auto ctx = CameraMotionContext::at(chronon3d::Frame{0});
        auto s = tr->sample(ctx);
        CHECK(approx_vec(s.position, make_vec(0, 0, -1000)));
    }

    // Segment 0→1 boundary: frame 30 should be P1 (end of linear seg).
    {
        auto ctx = CameraMotionContext::at(chronon3d::Frame{0});
        ctx.sample_time = chronon3d::SampleTime::from_frame(30.0, 30.0);
        auto s = tr->sample(ctx);
        CHECK(approx_vec(s.position, make_vec(100, 50, -800)));
    }

    // Segment 1: Bezier, frames [30, 75).  Midpoint at frame 52.5.
    {
        auto ctx = CameraMotionContext::at(chronon3d::Frame{0});
        ctx.sample_time = chronon3d::SampleTime::from_frame(52.5, 30.0);
        auto s = tr->sample(ctx);
        // Should NOT be at P1 or P2; should be somewhere on the bezier curve.
        CHECK_FALSE(approx_vec(s.position, make_vec(100, 50, -800)));
        CHECK_FALSE(approx_vec(s.position, make_vec(300, 100, -600)));
    }

    // Segment 1→2 boundary: frame 75 should be P2 (end of bezier seg).
    {
        auto ctx = CameraMotionContext::at(chronon3d::Frame{0});
        ctx.sample_time = chronon3d::SampleTime::from_frame(75.0, 30.0);
        auto s = tr->sample(ctx);
        CHECK(approx_vec(s.position, make_vec(300, 100, -600)));
    }

    // Segment 2: CatmullRom, frames [75, 135).  End at frame 135.
    {
        auto ctx = CameraMotionContext::at(chronon3d::Frame{0});
        ctx.sample_time = chronon3d::SampleTime::from_frame(135.0, 30.0);
        auto s = tr->sample(ctx);
        CHECK(approx_vec(s.position, make_vec(500, 0, -400)));
    }
}

// ==============================================================================
// 9 — Hold segment preserves exact position.
// ==============================================================================
TEST_CASE("Hold segment preserves exact position") {
    CameraTrajectoryBuilder b;
    b.move_to(make_vec(42, 17, -99))
     .move_to(make_vec(100, 0, 0)).duration_frames(10)
     .hold_for(20);
    auto tr = b.build();

    // hold_for creates a Hold segment at the last point for 20 frames.
    // At frame 15 (mid-hold): position should equal P1 (100,0,0).
    {
        auto ctx = CameraMotionContext::at(chronon3d::Frame{0});
        ctx.sample_time = chronon3d::SampleTime::from_frame(15.0, 30.0);
        auto s = tr->sample(ctx);
        CHECK(approx_vec(s.position, make_vec(100, 0, 0)));
        CHECK(approx(s.tangent.x, 0.0f));
        CHECK(approx(s.tangent.y, 0.0f));
        CHECK(approx(s.tangent.z, 0.0f));
    }
    // At frame 30 (end): still at same position.
    {
        auto ctx = CameraMotionContext::at(chronon3d::Frame{0});
        ctx.sample_time = chronon3d::SampleTime::from_frame(30.0, 30.0);
        auto s = tr->sample(ctx);
        CHECK(approx_vec(s.position, make_vec(100, 0, 0)));
    }
}

// ==============================================================================
// 10 — Zero-length segment never emits NaN.
// ==============================================================================
TEST_CASE("Zero-length segment never emits NaN") {
    CameraTrajectoryBuilder b;
    b.move_to(make_vec(5, 5, 5))
     .move_to(make_vec(5, 5, 5)).duration_frames(30);  // degenerate: P0 == P1
    auto tr = b.build();

    for (int f = 0; f <= 30; f += 10) {
        auto ctx = CameraMotionContext::at(chronon3d::Frame{0});
        ctx.sample_time = chronon3d::SampleTime::from_frame(static_cast<double>(f), 30.0);
        auto s = tr->sample(ctx);
        CHECK_FALSE(std::isnan(s.position.x));
        CHECK_FALSE(std::isnan(s.position.y));
        CHECK_FALSE(std::isnan(s.position.z));
        CHECK_FALSE(std::isnan(s.tangent.x));
        CHECK_FALSE(std::isnan(s.tangent.y));
        CHECK_FALSE(std::isnan(s.tangent.z));
    }
}

} // namespace
