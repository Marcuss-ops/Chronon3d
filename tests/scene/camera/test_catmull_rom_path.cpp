// ---------------------------------------------------------------------------
// tests/scene/camera/test_catmull_rom_path.cpp
//
// CatmullRomCameraMotion tests (camera-yoked — instantiate Camera2_5D,
// EasingCurve, AutoOrientMode).  The pure-math CatmullRomPath3D tests
// have moved to tests/core/animation/path/test_catmull_rom_path.cpp
// during the June 2026 test-domain cleanup.
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>
#include <chronon3d/animation/path/catmull_rom_path.hpp>
#include <cmath>
using namespace chronon3d;


// ── CatmullRomCameraMotion Tests ─────────────────────────────────────────

TEST_CASE("CatmullRomCameraMotion: empty path returns disabled camera") {
    CatmullRomCameraMotion motion;
    Camera2_5D cam = motion.evaluate(0.5f);
    CHECK(cam.enabled);
    CHECK(cam.zoom == doctest::Approx(1000.0f));
}

TEST_CASE("CatmullRomCameraMotion: passes through each waypoint exactly") {
    CatmullRomCameraMotion motion;
    motion.path().add_waypoint({0.0f, 0.0f, -1200.0f})
              .add_waypoint({200.0f, 0.0f, -800.0f})
              .add_waypoint({400.0f, 0.0f, -1000.0f});

    Camera2_5D cam_t0 = motion.evaluate(0.0f);
    Camera2_5D cam_t1 = motion.evaluate(1.0f);
    CHECK(cam_t0.position.x == doctest::Approx(0.0f));
    CHECK(cam_t1.position.x == doctest::Approx(400.0f));
}

TEST_CASE("CatmullRomCameraMotion: evaluates by frame range with easing") {
    CatmullRomCameraMotion motion;
    motion.path().add_waypoint({0.0f, 0.0f, -1000.0f})
              .add_waypoint({100.0f, 0.0f, -1000.0f});
    motion.easing = EasingCurve{Easing::OutCubic};

    Camera2_5D cam_mid = motion.evaluate(Frame{45}, Frame{0}, Frame{90});
    // OutCubic moves faster at start → midpoint should be > 50
    CHECK(cam_mid.position.x > 50.0f);
}

TEST_CASE("CatmullRomCameraMotion: auto-orient AlongPath") {
    CatmullRomCameraMotion motion;
    motion.path().add_waypoint({0.0f, 0.0f, -1000.0f})
              .add_waypoint({200.0f, 0.0f, -1000.0f});
    motion.auto_orient = AutoOrientMode::AlongPath;

    Camera2_5D cam = motion.evaluate(0.5f);
    CHECK(cam.point_of_interest_enabled);
    CHECK(cam.rotation.y != doctest::Approx(0.0f));
}

TEST_CASE("CatmullRomCameraMotion: auto-orient TowardsPOI") {
    CatmullRomCameraMotion motion;
    motion.path().add_waypoint({0.0f, 0.0f, -1000.0f})
              .add_waypoint({200.0f, 0.0f, -1000.0f});
    motion.auto_orient = AutoOrientMode::TowardsPOI;
    motion.point_of_interest = Vec3{150.0f, 50.0f, 0.0f};

    Camera2_5D cam = motion.evaluate(0.5f);
    CHECK(cam.point_of_interest.x == doctest::Approx(150.0f));
    CHECK(cam.point_of_interest.y == doctest::Approx(50.0f));
}

TEST_CASE("CatmullRomCameraMotion: roll_deg applied") {
    CatmullRomCameraMotion motion;
    motion.path().add_waypoint({0.0f, 0.0f, -1000.0f})
              .add_waypoint({200.0f, 0.0f, -1000.0f});
    motion.auto_orient = AutoOrientMode::AlongPath;
    motion.roll_deg = 12.0f;

    Camera2_5D cam = motion.evaluate(0.5f);
    CHECK(cam.rotation.z == doctest::Approx(12.0f));
}

TEST_CASE("CatmullRomCameraMotion: arc-length mode works") {
    CatmullRomCameraMotion motion;
    motion.path().add_waypoint({0.0f, 0.0f, 0.0f})
              .add_waypoint({100.0f, 100.0f, 0.0f})
              .add_waypoint({200.0f, 0.0f, 0.0f});
    motion.use_arc_length = true;

    Camera2_5D cam = motion.evaluate(0.5f);
    CHECK(!std::isnan(cam.position.x));
    CHECK(cam.enabled);
}

TEST_CASE("CatmullRomCameraMotion: zoom and fov settings") {
    CatmullRomCameraMotion motion;
    motion.path().add_waypoint({0.0f, 0.0f, 0.0f})
              .add_waypoint({100.0f, 0.0f, 0.0f});
    motion.zoom = 800.0f;
    motion.fov_deg = 35.0f;
    motion.projection_mode = Camera2_5DProjectionMode::Fov;

    Camera2_5D cam = motion.evaluate(0.5f);
    CHECK(cam.zoom == doctest::Approx(800.0f));
    CHECK(cam.fov_deg == doctest::Approx(35.0f));
    CHECK(cam.projection_mode == Camera2_5DProjectionMode::Fov);
}

TEST_CASE("CatmullRomCameraMotion: fluent chaining") {
    CatmullRomCameraMotion motion;
    auto& ref = motion;
    ref.path().set_boundary(CatmullRomBoundary::Clamped)
            .set_alpha(CatmullRomAlpha::Centripetal)
            .add_waypoint({0.0f, 0.0f, -1000.0f})
            .add_waypoint({100.0f, 50.0f, -800.0f})
            .add_waypoint({200.0f, 0.0f, -1000.0f});
    ref.auto_orient = AutoOrientMode::AlongPath;
    ref.easing = EasingCurve{Easing::InOutCubic};
    ref.use_arc_length = true;
    ref.roll_deg = 2.0f;

    Camera2_5D cam = ref.evaluate(0.5f);
    CHECK(cam.enabled);
    CHECK(!std::isnan(cam.position.x));
}
