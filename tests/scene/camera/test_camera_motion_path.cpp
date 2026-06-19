// ---------------------------------------------------------------------------
// tests/scene/camera/test_camera_motion_path.cpp
//
// CameraMotionPath tests (camera-yoked — instantiate Camera2_5D,
// EasingCurve, AutoOrientMode).  The pure-math SpatialBezierPath and
// Waypoint tests have moved to
// tests/core/animation/path/test_spatial_bezier_path.cpp
// during the June 2026 test-domain cleanup.
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>
#include <chronon3d/animation/path/spatial_bezier_path.hpp>
#include <cmath>
using namespace chronon3d;


// ── CameraMotionPath Tests ───────────────────────────────────────────────

TEST_CASE("CameraMotionPath: empty path returns disabled camera") {
    CameraMotionPath motion;
    Camera2_5D cam = motion.evaluate(0.5f);
    CHECK(cam.enabled);
    CHECK(cam.zoom == doctest::Approx(1000.0f));
}

TEST_CASE("CameraMotionPath: evaluate with frame range") {
    CameraMotionPath motion;
    motion.path().add_waypoint({0.0f, 0.0f, -1200.0f})
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
    motion.path().add_waypoint({0.0f, 0.0f, -1000.0f})
               .add_waypoint({100.0f, 0.0f, -1000.0f});
    motion.easing = EasingCurve{Easing::OutCubic};

    Camera2_5D cam = motion.evaluate(Frame{45}, Frame{0}, Frame{90});
    CHECK(cam.position.x > 50.0f);
}

TEST_CASE("CameraMotionPath: auto-orient TowardsPOI") {
    CameraMotionPath motion;
    motion.path().add_waypoint({0.0f, 0.0f, -1000.0f})
               .add_waypoint({200.0f, 0.0f, -1000.0f});
    motion.auto_orient = AutoOrientMode::TowardsPOI;
    motion.point_of_interest = Vec3{100.0f, 0.0f, 0.0f};

    Camera2_5D cam = motion.evaluate(0.5f);
    CHECK(cam.point_of_interest_enabled);
    CHECK(cam.point_of_interest.x == doctest::Approx(100.0f));
}

TEST_CASE("CameraMotionPath: auto-orient AlongPath") {
    CameraMotionPath motion;
    motion.path().add_waypoint({0.0f, 0.0f, -1000.0f})
               .add_waypoint({200.0f, 0.0f, -1000.0f});
    motion.auto_orient = AutoOrientMode::AlongPath;

    Camera2_5D cam = motion.evaluate(0.5f);
    CHECK(cam.point_of_interest_enabled);
    CHECK(cam.rotation.y != doctest::Approx(0.0f));
}

TEST_CASE("CameraMotionPath: roll_deg applied") {
    CameraMotionPath motion;
    motion.path().add_waypoint({0.0f, 0.0f, -1000.0f})
               .add_waypoint({0.0f, 0.0f, -800.0f});
    motion.auto_orient = AutoOrientMode::AlongPath;
    motion.roll_deg = 5.0f;

    Camera2_5D cam = motion.evaluate(0.5f);
    CHECK(cam.rotation.z == doctest::Approx(5.0f));
}

TEST_CASE("CameraMotionPath: arc-length mode") {
    CameraMotionPath motion;
    motion.path().add_waypoint({0.0f, 0.0f, -1000.0f}, {100.0f, 0.0f, 0.0f}, {})
               .add_waypoint({300.0f, 0.0f, -1000.0f}, {}, {-100.0f, 0.0f, 0.0f});
    motion.use_arc_length = true;

    Camera2_5D cam = motion.evaluate(0.5f);
    CHECK(!std::isnan(cam.position.x));
    CHECK(cam.enabled);
}

TEST_CASE("CameraMotionPath: zoom and fov settings") {
    CameraMotionPath motion;
    motion.path().add_waypoint({0.0f, 0.0f, -1000.0f})
               .add_waypoint({100.0f, 0.0f, -1000.0f});
    motion.zoom = 800.0f;
    motion.fov_deg = 35.0f;
    motion.projection_mode = Camera2_5DProjectionMode::Fov;

    Camera2_5D cam = motion.evaluate(0.5f);
    CHECK(cam.zoom == doctest::Approx(800.0f));
    CHECK(cam.fov_deg == doctest::Approx(35.0f));
    CHECK(cam.projection_mode == Camera2_5DProjectionMode::Fov);
}

// ── Fluent API Tests ─────────────────────────────────────────────────────

TEST_CASE("CameraMotionPath: fluent chaining") {
    CameraMotionPath motion;
    auto& ref = motion;
    ref.path().add_waypoint({0.0f, 0.0f, -1000.0f}, {50.0f, 0.0f, 0.0f}, {})
            .add_waypoint({100.0f, 50.0f, -800.0f}, {}, {-50.0f, 0.0f, 0.0f});

    ref.auto_orient = AutoOrientMode::AlongPath;
    ref.easing = EasingCurve{Easing::InOutCubic};
    ref.use_arc_length = true;
    ref.roll_deg = 3.0f;

    Camera2_5D cam = ref.evaluate(0.5f);
    CHECK(cam.enabled);
    CHECK(!std::isnan(cam.position.x));
}
