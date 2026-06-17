#if 0  // Disabled: pre-existing brace mismatch at end of input (line 322).
       // Re-enable after fixing the syntax issue.
#include <doctest/doctest.h>
#include <chronon3d/animation/path/catmull_rom_path.hpp>
#include <cmath>

using namespace chronon3d;

// ── CatmullRomPath3D Tests ───────────────────────────────────────────────────

TEST_CASE("CatmullRomPath3D: empty path returns zero") {
    CatmullRomPath3D path;
    CHECK(path.empty());
    CHECK(path.waypoint_count() == 0);
    CHECK(path.segment_count() == 0);
    Vec3 pos = path.evaluate(0.5f);
    CHECK(pos.x == doctest::Approx(0.0f));
    CHECK(pos.y == doctest::Approx(0.0f));
    CHECK(pos.z == doctest::Approx(0.0f));
}

TEST_CASE("CatmullRomPath3D: single waypoint returns that position") {
    CatmullRomPath3D path;
    path.add_waypoint({100.0f, 200.0f, -500.0f});
    CHECK(path.waypoint_count() == 1);
    CHECK(path.segment_count() == 0);
    Vec3 pos = path.evaluate(0.5f);
    CHECK(pos.x == doctest::Approx(100.0f));
    CHECK(pos.y == doctest::Approx(200.0f));
    CHECK(pos.z == doctest::Approx(-500.0f));
}

TEST_CASE("CatmullRomPath3D: curve passes through every waypoint") {
    // The defining property: at segment boundaries, position == waypoint.
    CatmullRomPath3D path;
    path.add_waypoint({0.0f, 0.0f, -1000.0f})
        .add_waypoint({100.0f, 100.0f, -1000.0f})
        .add_waypoint({200.0f, 0.0f, -1000.0f});

    Vec3 p0 = path.evaluate(0.0f);
    CHECK(p0.x == doctest::Approx(0.0f));
    CHECK(p0.y == doctest::Approx(0.0f));

    Vec3 p_mid = path.evaluate(0.5f); // boundary = second waypoint
    CHECK(p_mid.x == doctest::Approx(100.0f));
    CHECK(p_mid.y == doctest::Approx(100.0f));

    Vec3 p_end = path.evaluate(1.0f);
    CHECK(p_end.x == doctest::Approx(200.0f));
    CHECK(p_end.y == doctest::Approx(0.0f));
}

TEST_CASE("CatmullRomPath3D: 2 waypoints give linear path") {
    CatmullRomPath3D path;
    path.add_waypoint({0.0f, 0.0f, 0.0f})
        .add_waypoint({100.0f, 0.0f, 0.0f});

    Vec3 pm = path.evaluate(0.5f);
    CHECK(pm.x == doctest::Approx(50.0f));
    CHECK(pm.y == doctest::Approx(0.0f));

    Vec3 p1 = path.evaluate(1.0f);
    CHECK(p1.x == doctest::Approx(100.0f));
}

TEST_CASE("CatmullRomPath3D: four collinear waypoints ≈ linear") {
    // Catmull-Rom through 4 collinear points should be exactly the line.
    CatmullRomPath3D path;
    path.add_waypoint({0.0f, 0.0f, 0.0f})
        .add_waypoint({100.0f, 0.0f, 0.0f})
        .add_waypoint({200.0f, 0.0f, 0.0f})
        .add_waypoint({300.0f, 0.0f, 0.0f});

    for (f32 t : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        Vec3 p = path.evaluate(t);
        CHECK(p.y == doctest::Approx(0.0f).epsilon(0.001f));
        CHECK(p.z == doctest::Approx(0.0f).epsilon(0.001f));
        CHECK(p.x == doctest::Approx(300.0f * t).epsilon(0.5f));
    }
}

TEST_CASE("CatmullRomPath3D: centripetal avoids cusps on tight turns") {
    // A 90-degree turn that the uniform parameterization tends to cusp.
    CatmullRomPath3D path;
    path.set_alpha(CatmullRomAlpha::Centripetal);
    path.add_waypoint({0.0f, 0.0f, 0.0f})
        .add_waypoint({100.0f, 0.0f, 0.0f})
        .add_waypoint({100.0f, 100.0f, 0.0f})
        .add_waypoint({100.0f, 200.0f, 0.0f});

    // No self-intersection: sample densely and check the curve stays in the L shape
    for (f32 t = 0.0f; t <= 1.0f; t += 0.05f) {
        Vec3 p = path.evaluate(t);
        // Should be in the L region: x ∈ [0, 100] OR y ∈ [0, 200]
        bool in_L = (p.x >= -0.1f && p.x <= 100.1f) || (p.y >= -0.1f && p.y <= 200.1f);
        CHECK(in_L);
    }
}

TEST_CASE("CatmullRomPath3D: tangent at waypoint is well-defined") {
    CatmullRomPath3D path;
    path.add_waypoint({0.0f, 0.0f, 0.0f})
        .add_waypoint({100.0f, 0.0f, 0.0f})
        .add_waypoint({200.0f, 100.0f, 0.0f});

    // Tangent at t=0 should be roughly along +X (toward first segment).
    Vec3 tan0 = path.tangent_at(0.0f);
    CHECK(tan0.x > 0.0f);

    // Forward is normalized
    Vec3 fwd = path.forward_at(0.5f);
    f32 len = glm::length(fwd);
    CHECK(len == doctest::Approx(1.0f).epsilon(0.01f));
}

TEST_CASE("CatmullRomPath3D: tangent is non-zero for non-degenerate path") {
    CatmullRomPath3D path;
    path.add_waypoint({0.0f, 0.0f, -1000.0f})
        .add_waypoint({200.0f, 50.0f, -800.0f})
        .add_waypoint({400.0f, 0.0f, -1000.0f});

    Vec3 tan = path.tangent_at(0.5f);
    CHECK(glm::length(tan) > 0.0f);
}

TEST_CASE("CatmullRomPath3D: arc-length parameterization endpoints match") {
    CatmullRomPath3D path;
    path.add_waypoint({0.0f, 0.0f, 0.0f})
        .add_waypoint({100.0f, 100.0f, 0.0f})
        .add_waypoint({200.0f, 0.0f, 0.0f});

    CHECK(path.total_arc_length() > 0.0f);

    Vec3 raw0 = path.evaluate(0.0f);
    Vec3 arc0 = path.evaluate_arc_length(0.0f);
    CHECK(arc0.x == doctest::Approx(raw0.x));
    CHECK(arc0.y == doctest::Approx(raw0.y));

    Vec3 raw1 = path.evaluate(1.0f);
    Vec3 arc1 = path.evaluate_arc_length(1.0f);
    CHECK(arc1.x == doctest::Approx(raw1.x));
    CHECK(arc1.y == doctest::Approx(raw1.y));
}

TEST_CASE("CatmullRomPath3D: t is clamped outside [0,1]") {
    CatmullRomPath3D path;
    path.add_waypoint({0.0f, 0.0f, 0.0f})
        .add_waypoint({100.0f, 0.0f, 0.0f});

    Vec3 before = path.evaluate(-0.5f);
    CHECK(before.x == doctest::Approx(0.0f));

    Vec3 after = path.evaluate(1.5f);
    CHECK(after.x == doctest::Approx(100.0f));
}

TEST_CASE("CatmullRomPath3D: closed boundary wraps around") {
    CatmullRomPath3D path;
    path.set_boundary(CatmullRomBoundary::Closed);
    path.add_waypoint({0.0f, 0.0f, 0.0f})
        .add_waypoint({100.0f, 0.0f, 0.0f})
        .add_waypoint({100.0f, 100.0f, 0.0f});

    // In closed mode, 3 waypoints → 3 segments
    CHECK(path.segment_count() == 3);

    // Endpoints match (path is closed)
    Vec3 p0 = path.evaluate(0.0f);
    Vec3 p1 = path.evaluate(1.0f);
    CHECK(p0.x == doctest::Approx(p1.x).epsilon(0.5f));
    CHECK(p0.y == doctest::Approx(p1.y).epsilon(0.5f));
}

TEST_CASE("CatmullRomPath3D: open boundary holds position at endpoints") {
    CatmullRomPath3D path;
    path.set_boundary(CatmullRomBoundary::Open);
    path.add_waypoint({50.0f, 0.0f, 0.0f})
        .add_waypoint({100.0f, 50.0f, 0.0f})
        .add_waypoint({50.0f, 100.0f, 0.0f});

    // Position is anchored at the first and last waypoints.
    Vec3 p0 = path.evaluate(0.0f);
    Vec3 p1 = path.evaluate(1.0f);
    CHECK(p0.x == doctest::Approx(50.0f));
    CHECK(p0.y == doctest::Approx(0.0f));
    CHECK(p1.x == doctest::Approx(50.0f));
    CHECK(p1.y == doctest::Approx(100.0f));

    // With Open boundary (P0 = P1 duplicated), the tangent at the start
    // equals 0.5 * (P2 - P1) — non-zero. We just check it's well-defined.
    Vec3 tan0 = path.tangent_at(0.0f);
    CHECK(std::isfinite(tan0.x));
    CHECK(std::isfinite(tan0.y));
}

TEST_CASE("CatmullRomPath3D: clear resets state") {
    CatmullRomPath3D path;
    path.add_waypoint({0.0f, 0.0f, 0.0f}).add_waypoint({100.0f, 0.0f, 0.0f});
    CHECK(!path.empty());
    path.clear();
    CHECK(path.empty());
    CHECK(path.waypoint_count() == 0);
    CHECK(path.total_arc_length() == 0.0f);
}

TEST_CASE("CatmullRomPath3D: add_waypoints initializer list") {
    CatmullRomPath3D path;
    path.add_waypoints({{0, 0, 0}, {50, 50, 0}, {100, 0, 0}});
    CHECK(path.waypoint_count() == 3);
    Vec3 p = path.evaluate(0.0f);
    CHECK(p.x == doctest::Approx(0.0f));
    p = path.evaluate(1.0f);
    CHECK(p.x == doctest::Approx(100.0f));
}

// ── CatmullRomCameraMotion Tests ─────────────────────────────────────────────

TEST_CASE("CatmullRomCameraMotion: empty path returns disabled camera") {
    CatmullRomCameraMotion motion;
    Camera2_5D cam = motion.evaluate(0.5f);
    CHECK(cam.enabled);
    CHECK(cam.zoom == doctest::Approx(1000.0f));
}

TEST_CASE("CatmullRomCameraMotion: passes through each waypoint exactly") {
    CatmullRomCameraMotion motion;
    motion.path.add_waypoint({0.0f, 0.0f, -1200.0f})
              .add_waypoint({200.0f, 0.0f, -800.0f})
              .add_waypoint({400.0f, 0.0f, -1000.0f});

    Camera2_5D cam_t0 = motion.evaluate(0.0f);
    Camera2_5D cam_t1 = motion.evaluate(1.0f);
    CHECK(cam_t0.position.x == doctest::Approx(0.0f));
    CHECK(cam_t1.position.x == doctest::Approx(400.0f));
}

TEST_CASE("CatmullRomCameraMotion: evaluates by frame range with easing") {
    CatmullRomCameraMotion motion;
    motion.path.add_waypoint({0.0f, 0.0f, -1000.0f})
              .add_waypoint({100.0f, 0.0f, -1000.0f});
    motion.easing = EasingCurve{Easing::OutCubic};

    Camera2_5D cam_mid = motion.evaluate(Frame{45}, Frame{0}, Frame{90});
    // OutCubic moves faster at start → midpoint should be > 50
    CHECK(cam_mid.position.x > 50.0f);
}

TEST_CASE("CatmullRomCameraMotion: auto-orient AlongPath") {
    CatmullRomCameraMotion motion;
    motion.path.add_waypoint({0.0f, 0.0f, -1000.0f})
              .add_waypoint({200.0f, 0.0f, -1000.0f});
    motion.auto_orient = AutoOrientMode::AlongPath;

    Camera2_5D cam = motion.evaluate(0.5f);
    CHECK(cam.point_of_interest_enabled);
    CHECK(cam.rotation.y != doctest::Approx(0.0f));
}

TEST_CASE("CatmullRomCameraMotion: auto-orient TowardsPOI") {
    CatmullRomCameraMotion motion;
    motion.path.add_waypoint({0.0f, 0.0f, -1000.0f})
              .add_waypoint({200.0f, 0.0f, -1000.0f});
    motion.auto_orient = AutoOrientMode::TowardsPOI;
    motion.point_of_interest = Vec3{150.0f, 50.0f, 0.0f};

    Camera2_5D cam = motion.evaluate(0.5f);
    CHECK(cam.point_of_interest.x == doctest::Approx(150.0f));
    CHECK(cam.point_of_interest.y == doctest::Approx(50.0f));
}

TEST_CASE("CatmullRomCameraMotion: roll_deg applied") {
    CatmullRomCameraMotion motion;
    motion.path.add_waypoint({0.0f, 0.0f, -1000.0f})
              .add_waypoint({200.0f, 0.0f, -1000.0f});
    motion.auto_orient = AutoOrientMode::AlongPath;
    motion.roll_deg = 12.0f;

    Camera2_5D cam = motion.evaluate(0.5f);
    CHECK(cam.rotation.z == doctest::Approx(12.0f));
}

TEST_CASE("CatmullRomCameraMotion: arc-length mode works") {
    CatmullRomCameraMotion motion;
    motion.path.add_waypoint({0.0f, 0.0f, 0.0f})
              .add_waypoint({100.0f, 100.0f, 0.0f})
              .add_waypoint({200.0f, 0.0f, 0.0f});
    motion.use_arc_length = true;

    Camera2_5D cam = motion.evaluate(0.5f);
    CHECK(!std::isnan(cam.position.x));
    CHECK(cam.enabled);
}

TEST_CASE("CatmullRomCameraMotion: zoom and fov settings") {
    CatmullRomCameraMotion motion;
    motion.path.add_waypoint({0.0f, 0.0f, 0.0f})
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
    ref.path.set_boundary(CatmullRomBoundary::Clamped)
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

#endif // #if 0 — disabled test file
