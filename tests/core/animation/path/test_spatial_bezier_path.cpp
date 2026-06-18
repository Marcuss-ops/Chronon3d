// ---------------------------------------------------------------------------
// tests/core/animation/path/test_spatial_bezier_path.cpp
//
// Pure-math tests for SpatialBezierPath and the Waypoint value type.
// Relocated from tests/scene/camera/test_camera_motion_path.cpp
// during the June 2026 test-domain cleanup: the file previously
// dragged scene + software-backend dependencies into what is pure
// animation/path math.
//
// CameraMotionPath tests (camera-yoked — use Camera2_5D,
// EasingCurve, AutoOrientMode) stay in
// tests/scene/camera/test_camera_motion_path.cpp where they belong.
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>
#include <chronon3d/animation/path/spatial_bezier_path.hpp>
#include <cmath>
using namespace chronon3d;


// ── SpatialBezierPath Tests ──────────────────────────────────────────────

TEST_CASE("SpatialBezierPath: empty path returns zero") {
    SpatialBezierPath path;
    CHECK(path.empty());
    CHECK(path.waypoint_count() == 0);
    CHECK(path.segment_count() == 0);
    Vec3 pos = path.evaluate(0.5f);
    CHECK(pos.x == doctest::Approx(0.0f));
    CHECK(pos.y == doctest::Approx(0.0f));
    CHECK(pos.z == doctest::Approx(0.0f));
}

TEST_CASE("SpatialBezierPath: single waypoint returns that position") {
    SpatialBezierPath path;
    path.add_waypoint({100.0f, 200.0f, -500.0f});
    CHECK(path.waypoint_count() == 1);
    CHECK(path.segment_count() == 0);
    Vec3 pos = path.evaluate(0.5f);
    CHECK(pos.x == doctest::Approx(100.0f));
    CHECK(pos.y == doctest::Approx(200.0f));
    CHECK(pos.z == doctest::Approx(-500.0f));
}

TEST_CASE("SpatialBezierPath: two waypoints linear interpolation") {
    SpatialBezierPath path;
    path.add_waypoint({0.0f, 0.0f, -1000.0f})
        .add_waypoint({100.0f, 0.0f, -1000.0f});

    CHECK(path.waypoint_count() == 2);
    CHECK(path.segment_count() == 1);

    Vec3 p0 = path.evaluate(0.0f);
    CHECK(p0.x == doctest::Approx(0.0f));

    Vec3 p1 = path.evaluate(1.0f);
    CHECK(p1.x == doctest::Approx(100.0f));

    Vec3 pm = path.evaluate(0.5f);
    CHECK(pm.x == doctest::Approx(50.0f));
    CHECK(pm.y == doctest::Approx(0.0f));
}

TEST_CASE("SpatialBezierPath: three waypoints with no handles") {
    SpatialBezierPath path;
    path.add_waypoint({0.0f, 0.0f, 0.0f})
        .add_waypoint({100.0f, 100.0f, 0.0f})
        .add_waypoint({200.0f, 0.0f, 0.0f});

    CHECK(path.segment_count() == 2);

    Vec3 mid = path.evaluate(0.5f);
    CHECK(mid.x == doctest::Approx(100.0f));
    CHECK(mid.y == doctest::Approx(100.0f));

    Vec3 q3 = path.evaluate(0.75f);
    CHECK(q3.x == doctest::Approx(150.0f));
    CHECK(q3.y == doctest::Approx(50.0f));
}

TEST_CASE("SpatialBezierPath: tangent at endpoints") {
    SpatialBezierPath path;
    path.add_waypoint({0.0f, 0.0f, 0.0f}, {50.0f, 0.0f, 0.0f}, {})
        .add_waypoint({200.0f, 0.0f, 0.0f}, {}, {});

    Vec3 tan = path.tangent_at(0.0f);
    CHECK(tan.x > 0.0f);

    Vec3 fwd = path.forward_at(0.0f);
    f32 len = glm::length(fwd);
    CHECK(len == doctest::Approx(1.0f).epsilon(0.01f));
}

TEST_CASE("SpatialBezierPath: arc-length parameterization") {
    SpatialBezierPath path;
    path.add_waypoint({0.0f, 0.0f, 0.0f}, {80.0f, 100.0f, 0.0f}, {})
        .add_waypoint({200.0f, 100.0f, 0.0f}, {}, {-80.0f, -100.0f, 0.0f});

    CHECK(path.total_arc_length() > 0.0f);

    Vec3 raw = path.evaluate(0.3f);
    Vec3 arc = path.evaluate_arc_length(0.3f);

    CHECK(!std::isnan(raw.x));
    CHECK(!std::isnan(arc.x));

    Vec3 raw0 = path.evaluate(0.0f);
    Vec3 arc0 = path.evaluate_arc_length(0.0f);
    CHECK(arc0.x == doctest::Approx(raw0.x));
    CHECK(arc0.y == doctest::Approx(raw0.y));

    Vec3 raw1 = path.evaluate(1.0f);
    Vec3 arc1 = path.evaluate_arc_length(1.0f);
    CHECK(arc1.x == doctest::Approx(raw1.x));
    CHECK(arc1.y == doctest::Approx(raw1.y));
}

TEST_CASE("SpatialBezierPath: clamping t outside [0,1]") {
    SpatialBezierPath path;
    path.add_waypoint({0.0f, 0.0f, 0.0f})
        .add_waypoint({100.0f, 0.0f, 0.0f});

    Vec3 before = path.evaluate(-0.5f);
    CHECK(before.x == doctest::Approx(0.0f));

    Vec3 after = path.evaluate(1.5f);
    CHECK(after.x == doctest::Approx(100.0f));
}

TEST_CASE("SpatialBezierPath: clear") {
    SpatialBezierPath path;
    path.add_waypoint({0.0f, 0.0f, 0.0f}).add_waypoint({100.0f, 0.0f, 0.0f});
    CHECK(!path.empty());
    path.clear();
    CHECK(path.empty());
    CHECK(path.waypoint_count() == 0);
}


// ── Waypoint Tests ───────────────────────────────────────────────────────

TEST_CASE("Waypoint: default construction") {
    Waypoint wp;
    CHECK(wp.position.x == doctest::Approx(0.0f));
    CHECK(wp.in_handle.x == doctest::Approx(0.0f));
    CHECK(wp.out_handle.x == doctest::Approx(0.0f));
}

TEST_CASE("Waypoint: parameterized construction") {
    Waypoint wp(Vec3{1.0f, 2.0f, 3.0f}, Vec3{10.0f, 0.0f, 0.0f}, Vec3{-10.0f, 0.0f, 0.0f});
    CHECK(wp.position.x == doctest::Approx(1.0f));
    CHECK(wp.out_handle.x == doctest::Approx(10.0f));
    CHECK(wp.in_handle.x == doctest::Approx(-10.0f));
}
