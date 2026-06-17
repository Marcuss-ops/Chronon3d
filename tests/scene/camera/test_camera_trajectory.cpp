// ==============================================================================
// tests/scene/camera/test_camera_trajectory.cpp
//
// CameraTrajectory V1 tests — migrated to current API (June 2026).
//
// Key API changes from old test:
//   - SampleTime::from_frame(double, double) → SampleTime::from_frame(double, FrameRate)
//   - CameraMotionContext::at(Frame{0}) — uses Frame (int-constructible)
//   - CameraTrajectoryBuilder uses move_to, bezier_to, catmull_rom_to, hold_for
//   - ArcLengthTable API unchanged
//
// 10 TEST_CASEs:
//   1. Linear segment uses segment indices (not front/back)
//   2. Bézier derivative matches finite difference
//   3. Handle contract uses local offsets
//   4. Sub-frame / fractional frame sampling
//   5. ArcLengthTable maps 0→0 and total→1
//   6. ArcLengthTable midpoint uses LUT entries
//   7. Arc-length spacing CV < 0.03
//   8. Multi-segment trajectory respects every boundary
//   9. Hold segment preserves exact position
//  10. Zero-length segment never emits NaN
// ==============================================================================
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <tests/helpers/test_math.hpp>

#include <chronon3d/scene/camera/camera_v1/camera_trajectory.hpp>
#include <chronon3d/scene/camera/camera_v1/arc_length_table.hpp>

#include <cmath>
#include <vector>
#include <algorithm>

namespace {

using namespace chronon3d::camera_v1;
using chronon3d::test::approx;

constexpr float kEps = 1e-4f;
const FrameRate kFps30{30, 1};

inline bool approx_vec(Vec3 a, Vec3 b, float tol = kEps) {
    return std::abs(a.x - b.x) <= tol
        && std::abs(a.y - b.y) <= tol
        && std::abs(a.z - b.z) <= tol;
}

inline Vec3 make_vec(float x, float y, float z) { return {x, y, z}; }

inline std::shared_ptr<CameraTrajectory> make_linear(Vec3 from, Vec3 to) {
    CameraTrajectoryBuilder b;
    b.move_to(from).move_to(to).duration_frames(30.0f);
    return b.build();
}

inline std::shared_ptr<CameraTrajectory> make_mixed() {
    CameraTrajectoryBuilder b;
    b.move_to(make_vec(0, 0, -1000))
     .move_to(make_vec(100, 50, -800)).duration_frames(30)
     .bezier_to({-20, 0, 0}, {30, 10, 0}, make_vec(300, 100, -600)).duration_frames(45)
     .catmull_rom_to(make_vec(500, 0, -400)).duration_frames(60);
    return b.build();
}

// ==============================================================================
// 1 — Linear segment uses segment indices.
// ==============================================================================
TEST_CASE("CameraTrajectory linear uses segment indices") {
    CameraTrajectoryBuilder b;
    b.move_to(make_vec(0, 0, 0))
     .move_to(make_vec(10, 0, 0)).duration_frames(30)
     .move_to(make_vec(20, 0, 0)).duration_frames(30);
    auto tr = b.build();
    REQUIRE(tr->size() == 2);

    // Frame 0: at P0, tangent toward P1.
    {
        auto ctx = CameraMotionContext::at(Frame{0});
        auto s = tr->sample(ctx);
        CHECK(approx_vec(s.position, make_vec(0, 0, 0)));
        CHECK(s.tangent.x > 0.5f);
        CHECK(approx(s.tangent.y, 0.0f));
    }

    // Frame 30: at P1, tangent toward P2.
    {
        auto ctx = CameraMotionContext::at(Frame{0});
        ctx.sample_time = SampleTime::from_frame(30.0, kFps30);
        auto s = tr->sample(ctx);
        CHECK(approx_vec(s.position, make_vec(10, 0, 0)));
        CHECK(s.tangent.x > 0.5f);
    }

    // Frame 60: at P2.
    {
        auto ctx = CameraMotionContext::at(Frame{0});
        ctx.sample_time = SampleTime::from_frame(60.0, kFps30);
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

    auto ctx = CameraMotionContext::at(Frame{0});
    ctx.sample_time = SampleTime::from_frame(30.0, kFps30);
    auto s0 = tr->sample(ctx);

    ctx.sample_time = SampleTime::from_frame(30.1, kFps30);
    auto s1 = tr->sample(ctx);

    Vec3 fd_tangent = {
        (s1.position.x - s0.position.x) / 0.1f,
        (s1.position.y - s0.position.y) / 0.1f,
        (s1.position.z - s0.position.z) / 0.1f
    };
    float fd_len = std::sqrt(fd_tangent.x*fd_tangent.x + fd_tangent.y*fd_tangent.y + fd_tangent.z*fd_tangent.z);
    Vec3 fd_norm = fd_len > 1e-6f ? Vec3{fd_tangent.x/fd_len, fd_tangent.y/fd_len, fd_tangent.z/fd_len} : Vec3{0,0,0};

    CHECK(approx(fd_norm.x, s0.tangent.x, 0.02f));
    CHECK(approx(fd_norm.y, s0.tangent.y, 0.02f));
    CHECK(approx(fd_norm.z, s0.tangent.z, 0.02f));
}

// ==============================================================================
// 3 — Handle contract uses local offsets.
// ==============================================================================
TEST_CASE("CameraTrajectory handle contract uses local offsets") {
    CameraTrajectoryBuilder b;
    b.move_to(make_vec(0,    0, -1000))
     .bezier_to({-100, 0, 0}, {100, 0, 0}, make_vec(200, 0, -1000))
     .duration_frames(30);
    auto tr = b.build();

    auto ctx = CameraMotionContext::at(Frame{0});
    ctx.sample_time = SampleTime::from_frame(15.0, kFps30);
    auto s = tr->sample(ctx);

    CHECK(approx(s.position.x, 100.0f, 0.1f));
    CHECK(approx(s.position.y, 0.0f));
    CHECK(approx(s.position.z, -1000.0f));
}

// ==============================================================================
// 4 — Sub-frame / fractional frame sampling.
// ==============================================================================
TEST_CASE("CameraTrajectory samples fractional frames") {
    auto tr = make_linear(make_vec(0, 0, 0), make_vec(100, 0, 0));

    // Integer frame 0: position = (0,0,0)
    {
        auto ctx = CameraMotionContext::at(Frame{0});
        auto s = tr->sample(ctx);
        CHECK(approx_vec(s.position, make_vec(0, 0, 0)));
    }
    // Frame 15.0 (halfway): position = (50,0,0)
    {
        auto ctx = CameraMotionContext::at(Frame{0});
        ctx.sample_time = SampleTime::from_frame(15.0, kFps30);
        auto s = tr->sample(ctx);
        CHECK(approx_vec(s.position, make_vec(50, 0, 0)));
    }
    // Frame 30.0 (end): position = (100,0,0)
    {
        auto ctx = CameraMotionContext::at(Frame{0});
        ctx.sample_time = SampleTime::from_frame(30.0, kFps30);
        auto s = tr->sample(ctx);
        CHECK(approx_vec(s.position, make_vec(100, 0, 0)));
    }
    // Frame 15.5 (sub-frame): position must differ from frame 15.
    {
        auto ctx = CameraMotionContext::at(Frame{0});
        ctx.sample_time = SampleTime::from_frame(15.5, kFps30);
        auto s_sub = tr->sample(ctx);

        ctx.sample_time = SampleTime::from_frame(15.0, kFps30);
        auto s_int = tr->sample(ctx);
        CHECK_FALSE(approx_vec(s_sub.position, s_int.position, 0.01f));
    }
}

// ==============================================================================
// 5 — ArcLengthTable maps 0→0 and total→1.
// ==============================================================================
TEST_CASE("ArcLengthTable maps 0 to 0 and total to 1") {
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

    CHECK(approx(lut.t_from_arc_length(0.0f), 0.0f));
    CHECK(approx(lut.t_from_arc_length(10.0f), 1.0f));
}

// ==============================================================================
// 6 — ArcLengthTable midpoint uses LUT entries.
// ==============================================================================
TEST_CASE("ArcLengthTable midpoint uses LUT entries") {
    std::vector<ArcLengthEntry> entries;
    entries.push_back({0.0f, 0.0f});
    entries.push_back({1.0f, 0.33f});
    entries.push_back({3.0f, 0.66f});
    entries.push_back({10.0f, 1.0f});
    ArcLengthTable lut(std::move(entries));

    float mid = lut.parameter_at_fraction(0.5f);
    CHECK(mid > 0.65f);
    CHECK(mid < 0.85f);
    CHECK_FALSE(approx(mid, 0.5f));
}

// ==============================================================================
// 7 — Arc-length spacing CV < 0.03.
// ==============================================================================
TEST_CASE("Arc-length spacing CV less than 0.03") {
    CameraTrajectoryBuilder b;
    b.move_to(make_vec(0, 0, -1000))
     .bezier_to({200, 100, 200}, {100, -50, -100}, make_vec(400, 50, -800))
     .duration_frames(60)
     .arc_length_parameterized(true);
    auto tr = b.build();
    REQUIRE(tr->arc_length_parameterized());

    constexpr int N = 32;
    std::vector<Vec3> positions;
    for (int i = 0; i <= N; ++i) {
        auto ctx = CameraMotionContext::at(Frame{0});
        ctx.sample_time = SampleTime::from_frame(
            static_cast<double>(i) * 60.0 / static_cast<double>(N), kFps30);
        positions.push_back(tr->sample(ctx).position);
    }

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

    CHECK(cv < 0.03f);
}

// ==============================================================================
// 8 — Multi-segment trajectory respects every segment boundary.
// ==============================================================================
TEST_CASE("Multi-segment trajectory respects every boundary") {
    auto tr = make_mixed();
    REQUIRE(tr->size() == 3);

    // Segment 0: Linear, frames [0, 30). At frame 0 → P0.
    {
        auto ctx = CameraMotionContext::at(Frame{0});
        auto s = tr->sample(ctx);
        CHECK(approx_vec(s.position, make_vec(0, 0, -1000)));
    }

    // Segment 0→1 boundary: frame 30 should be P1.
    {
        auto ctx = CameraMotionContext::at(Frame{0});
        ctx.sample_time = SampleTime::from_frame(30.0, kFps30);
        auto s = tr->sample(ctx);
        CHECK(approx_vec(s.position, make_vec(100, 50, -800)));
    }

    // Segment 1: Bezier, midpoint at frame 52.5.
    {
        auto ctx = CameraMotionContext::at(Frame{0});
        ctx.sample_time = SampleTime::from_frame(52.5, kFps30);
        auto s = tr->sample(ctx);
        CHECK_FALSE(approx_vec(s.position, make_vec(100, 50, -800)));
        CHECK_FALSE(approx_vec(s.position, make_vec(300, 100, -600)));
    }

    // Segment 1→2 boundary: frame 75 should be P2.
    {
        auto ctx = CameraMotionContext::at(Frame{0});
        ctx.sample_time = SampleTime::from_frame(75.0, kFps30);
        auto s = tr->sample(ctx);
        CHECK(approx_vec(s.position, make_vec(300, 100, -600)));
    }

    // Segment 2: CatmullRom end at frame 135.
    {
        auto ctx = CameraMotionContext::at(Frame{0});
        ctx.sample_time = SampleTime::from_frame(135.0, kFps30);
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

    // Mid-hold: position should equal P1.
    {
        auto ctx = CameraMotionContext::at(Frame{0});
        ctx.sample_time = SampleTime::from_frame(15.0, kFps30);
        auto s = tr->sample(ctx);
        CHECK(approx_vec(s.position, make_vec(100, 0, 0)));
        CHECK(approx(s.tangent.x, 0.0f));
        CHECK(approx(s.tangent.y, 0.0f));
        CHECK(approx(s.tangent.z, 0.0f));
    }
    // End: still at same position.
    {
        auto ctx = CameraMotionContext::at(Frame{0});
        ctx.sample_time = SampleTime::from_frame(30.0, kFps30);
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
     .move_to(make_vec(5, 5, 5)).duration_frames(30);
    auto tr = b.build();

    for (int f = 0; f <= 30; f += 10) {
        auto ctx = CameraMotionContext::at(Frame{0});
        ctx.sample_time = SampleTime::from_frame(static_cast<double>(f), kFps30);
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
