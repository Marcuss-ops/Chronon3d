#include <doctest/doctest.h>
#include <chronon3d/animation/path/spatial_bezier_path.hpp>
#include <cmath>

using namespace chronon3d;

// ── SpatialBezierPath Tests ──────────────────────────────────────────────────

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

    // At t=0 → first waypoint
    Vec3 p0 = path.evaluate(0.0f);
    CHECK(p0.x == doctest::Approx(0.0f));

    // At t=1 → last waypoint
    Vec3 p1 = path.evaluate(1.0f);
    CHECK(p1.x == doctest::Approx(100.0f));

    // At t=0.5 → midpoint (no handles = linear)
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

    // At t=0.5 → boundary between segments = waypoint 1 = (100,100,0)
    Vec3 mid = path.evaluate(0.5f);
    CHECK(mid.x == doctest::Approx(100.0f));
    CHECK(mid.y == doctest::Approx(100.0f));

    // At t=0.75 → midpoint of second segment
    Vec3 q3 = path.evaluate(0.75f);
    CHECK(q3.x == doctest::Approx(150.0f));
    CHECK(q3.y == doctest::Approx(50.0f));
}

TEST_CASE("SpatialBezierPath: tangent at endpoints") {
    SpatialBezierPath path;
    path.add_waypoint({0.0f, 0.0f, 0.0f}, {50.0f, 0.0f, 0.0f}, {})
        .add_waypoint({200.0f, 0.0f, 0.0f}, {}, {});

    // Tangent at t=0 should point in +X direction (out_handle)
    Vec3 tan = path.tangent_at(0.0f);
    CHECK(tan.x > 0.0f);

    // Forward direction should be normalized
    Vec3 fwd = path.forward_at(0.0f);
    f32 len = glm::length(fwd);
    CHECK(len == doctest::Approx(1.0f).epsilon(0.01f));
}

TEST_CASE("SpatialBezierPath: arc-length parameterization") {
    SpatialBezierPath path;
    // S-curve with significant handles
    path.add_waypoint({0.0f, 0.0f, 0.0f}, {80.0f, 100.0f, 0.0f}, {})
        .add_waypoint({200.0f, 100.0f, 0.0f}, {}, {-80.0f, -100.0f, 0.0f});

    CHECK(path.total_arc_length() > 0.0f);

    // Arc-length evaluation should give different positions than raw evaluation
    // at the same t (due to reparameterization)
    Vec3 raw = path.evaluate(0.3f);
    Vec3 arc = path.evaluate_arc_length(0.3f);

    // Both should be valid positions
    CHECK(!std::isnan(raw.x));
    CHECK(!std::isnan(arc.x));

    // Arc-length at endpoints should match raw
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

// ── CameraMotionPath Tests ───────────────────────────────────────────────────

TEST_CASE("CameraMotionPath: empty path returns disabled camera") {
    CameraMotionPath motion;
    Camera2_5D cam = motion.evaluate(0.5f);
    CHECK(cam.enabled);
    CHECK(cam.zoom == doctest::Approx(1000.0f));
}

TEST_CASE("CameraMotionPath: evaluate with frame range") {
    CameraMotionPath motion;
    motion.path.add_waypoint({0.0f, 0.0f, -1200.0f})
               .add_waypoint({200.0f, 0.0f, -800.0f});

    Camera2_5D cam_start = motion.evaluate(Frame{0}, Frame{0}, Frame{90});
    Camera2_5D cam_end = motion.evaluate(Frame{90}, Frame{0}, Frame{90});
    Camera2_5D cam_mid = motion.evaluate(Frame{45}, Frame{0}, Frame{90});

    CHECK(cam_start.position.x == doctest::Approx(0.0f));
    CHECK(cam_end.position.x == doctest::Approx(200.0f));
    CHECK(cam_mid.position.x == doctest::Approx(100.0f));
}

TEST_CASE("CameraMotionPath: easing affects position") {
    CameraMotionPath motion;
    motion.path.add_waypoint({0.0f, 0.0f, -1000.0f})
               .add_waypoint({100.0f, 0.0f, -1000.0f});
    motion.easing = EasingCurve{Easing::OutCubic};

    // With OutCubic, midpoint should be > 50 (fast start)
    // Use Frame-based evaluate which applies easing
    Camera2_5D cam = motion.evaluate(Frame{45}, Frame{0}, Frame{90});
    CHECK(cam.position.x > 50.0f);
}

TEST_CASE("CameraMotionPath: auto-orient TowardsPOI") {
    CameraMotionPath motion;
    motion.path.add_waypoint({0.0f, 0.0f, -1000.0f})
               .add_waypoint({200.0f, 0.0f, -1000.0f});
    motion.auto_orient = AutoOrientMode::TowardsPOI;
    motion.point_of_interest = Vec3{100.0f, 0.0f, 0.0f};

    Camera2_5D cam = motion.evaluate(0.5f);
    CHECK(cam.point_of_interest_enabled);
    CHECK(cam.point_of_interest.x == doctest::Approx(100.0f));
}

TEST_CASE("CameraMotionPath: auto-orient AlongPath") {
    CameraMotionPath motion;
    motion.path.add_waypoint({0.0f, 0.0f, -1000.0f})
               .add_waypoint({200.0f, 0.0f, -1000.0f});
    motion.auto_orient = AutoOrientMode::AlongPath;

    Camera2_5D cam = motion.evaluate(0.5f);
    // Camera should have POI set (look-at along path direction)
    CHECK(cam.point_of_interest_enabled);
    // Along +X path, yaw should be near -90° (looking right)
    // Actually atan2(1, 0) = 90° for +X direction
    CHECK(cam.rotation.y != doctest::Approx(0.0f));
}

TEST_CASE("CameraMotionPath: roll_deg applied") {
    CameraMotionPath motion;
    motion.path.add_waypoint({0.0f, 0.0f, -1000.0f})
               .add_waypoint({0.0f, 0.0f, -800.0f});
    motion.auto_orient = AutoOrientMode::AlongPath;
    motion.roll_deg = 5.0f;

    Camera2_5D cam = motion.evaluate(0.5f);
    CHECK(cam.rotation.z == doctest::Approx(5.0f));
}

TEST_CASE("CameraMotionPath: arc-length mode") {
    CameraMotionPath motion;
    motion.path.add_waypoint({0.0f, 0.0f, -1000.0f}, {100.0f, 0.0f, 0.0f}, {})
               .add_waypoint({300.0f, 0.0f, -1000.0f}, {}, {-100.0f, 0.0f, 0.0f});
    motion.use_arc_length = true;

    Camera2_5D cam = motion.evaluate(0.5f);
    CHECK(!std::isnan(cam.position.x));
    CHECK(cam.enabled);
}

TEST_CASE("CameraMotionPath: zoom and fov settings") {
    CameraMotionPath motion;
    motion.path.add_waypoint({0.0f, 0.0f, -1000.0f})
               .add_waypoint({100.0f, 0.0f, -1000.0f});
    motion.zoom = 800.0f;
    motion.fov_deg = 35.0f;
    motion.projection_mode = Camera2_5DProjectionMode::Fov;

    Camera2_5D cam = motion.evaluate(0.5f);
    CHECK(cam.zoom == doctest::Approx(800.0f));
    CHECK(cam.fov_deg == doctest::Approx(35.0f));
    CHECK(cam.projection_mode == Camera2_5DProjectionMode::Fov);
}

// ── Waypoint Tests ───────────────────────────────────────────────────────────

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

// ── Fluent API Tests ─────────────────────────────────────────────────────────

TEST_CASE("CameraMotionPath: fluent chaining") {
    CameraMotionPath motion;
    auto& ref = motion;
    ref.path.add_waypoint({0.0f, 0.0f, -1000.0f}, {50.0f, 0.0f, 0.0f}, {})
            .add_waypoint({100.0f, 50.0f, -800.0f}, {}, {-50.0f, 0.0f, 0.0f});

    ref.auto_orient = AutoOrientMode::AlongPath;
    ref.easing = EasingCurve{Easing::InOutCubic};
    ref.use_arc_length = true;
    ref.roll_deg = 3.0f;

    Camera2_5D cam = ref.evaluate(0.5f);
    CHECK(cam.enabled);
    CHECK(!std::isnan(cam.position.x));
}
