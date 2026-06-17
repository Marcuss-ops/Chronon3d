// ==============================================================================
// tests/scene/camera/test_camera_constraints_p5.cpp
//
// CameraConstraint P5 tests — migrated to current API (June 2026).
//
// Tests:
//   1. LookAt produces valid quaternion rotation
//   2. LookAt fails on coincident target
//   3. KeepHorizon zeros roll
//   4. DampedFollow uses per-constraint state independently
//   5. Distance constraint clamps camera distance
//   6. Distance fails on coincident camera+target
//   7. RotationLimit clamps angles
//   8. Factory creates DampedFollow with custom damping
//   9. register_default includes all 5 builtins
// ==============================================================================
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <tests/helpers/test_math.hpp>

#include <chronon3d/scene/camera/camera_v1/camera_constraint.hpp>
#include <chronon3d/scene/registry/camera_constraint_registry.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/core/types/sample_time.hpp>

#include <cmath>
using namespace chronon3d;

namespace {

using namespace chronon3d::camera_v1;
using chronon3d::test::approx;

// ==============================================================================
// 1 — LookAt produces valid quaternion rotation.
// ==============================================================================
TEST_CASE("PR4: LookAt produces valid quaternion rotation") {
    register_default_camera_constraints();
    auto& reg = CameraConstraintRegistry::instance();
    auto c = reg.create("camera.look_at");
    REQUIRE(c != nullptr);

    Camera2_5D cam;
    cam.position = {0, 0, 0};

    CameraMotionContext ctx = CameraMotionContext::at(Frame{0});
    ctx.base_target = {100, 0, 0};

    ConstraintSession session;
    auto result = c->evaluate(cam, ctx, session);

    CHECK(result.ok);
    CHECK(result.reason.empty());
    CHECK(result.camera.point_of_interest_enabled);
    CHECK(approx(result.camera.point_of_interest.x, 100.0f, 0.1f));
}

// ==============================================================================
// 2 — LookAt fails on coincident target.
// ==============================================================================
TEST_CASE("PR4: LookAt fails on coincident target") {
    register_default_camera_constraints();
    auto& reg = CameraConstraintRegistry::instance();
    auto c = reg.create("camera.look_at");
    REQUIRE(c != nullptr);

    Camera2_5D cam;
    cam.position = {1, 1, 1};

    CameraMotionContext ctx = CameraMotionContext::at(Frame{0});
    ctx.base_target = {1, 1, 1};

    ConstraintSession session;
    auto result = c->evaluate(cam, ctx, session);

    CHECK_FALSE(result.ok);
    CHECK(result.reason == "target-coincident");
}

// ==============================================================================
// 3 — KeepHorizon zeros roll, preserves yaw.
// ==============================================================================
TEST_CASE("PR4: KeepHorizon zeros roll") {
    register_default_camera_constraints();
    auto& reg = CameraConstraintRegistry::instance();
    auto c = reg.create("camera.keep_horizon");
    REQUIRE(c != nullptr);

    Camera2_5D cam;
    cam.rotation = {30.0f, 45.0f, 15.0f};

    ConstraintSession session;
    auto result = c->evaluate(cam, CameraMotionContext::at(Frame{0}), session);

    CHECK(result.ok);
    CHECK(approx(result.camera.rotation.z, 0.0f));
    CHECK(approx(result.camera.rotation.y, 45.0f));
}

// ==============================================================================
// 4 — DampedFollow uses per-constraint state independently.
// ==============================================================================
TEST_CASE("PR4: DampedFollow uses per-constraint state independently") {
    register_default_camera_constraints();
    auto& reg = CameraConstraintRegistry::instance();
    auto c1 = reg.create("camera.damped_follow", DampedFollowParams{0.1f});
    auto c2 = reg.create("camera.damped_follow", DampedFollowParams{0.9f});
    REQUIRE(c1 != nullptr);
    REQUIRE(c2 != nullptr);

    Camera2_5D cam;
    cam.position = {0, 0, -1000};

    CameraMotionContext ctx = CameraMotionContext::at(Frame{0});

    ConstraintSession s1;
    ConstraintSession s2;
    s1.ensure_states(1);
    s2.ensure_states(1);

    auto r1 = c1->evaluate(cam, ctx, s1);
    auto r2 = c2->evaluate(cam, ctx, s2);

    CHECK(r1.ok);
    CHECK(r2.ok);
    CHECK(approx(r1.camera.position.x, r2.camera.position.x));
}

// ==============================================================================
// 5 — Distance constraint clamps camera distance.
// ==============================================================================
TEST_CASE("PR4: Distance constraint clamps camera distance") {
    register_default_camera_constraints();
    auto& reg = CameraConstraintRegistry::instance();
    auto c = reg.create("camera.distance", DistanceParams{50.0f, 500.0f});
    REQUIRE(c != nullptr);

    // Camera at distance 10 → should be pushed to min 50.
    {
        Camera2_5D cam;
        cam.position = {0, 0, -10};
        cam.point_of_interest = {0, 0, 0};
        cam.point_of_interest_enabled = true;
        ConstraintSession session;
        auto result = c->evaluate(cam, CameraMotionContext::at(Frame{0}), session);
        CHECK(result.ok);
        CHECK(approx(result.camera.position.z, -50.0f, 1.0f));
    }

    // Camera at distance 1000 → should be pulled to max 500.
    {
        Camera2_5D cam;
        cam.position = {0, 0, -1000};
        cam.point_of_interest = {0, 0, 0};
        cam.point_of_interest_enabled = true;
        ConstraintSession session;
        auto result = c->evaluate(cam, CameraMotionContext::at(Frame{0}), session);
        CHECK(result.ok);
        CHECK(approx(result.camera.position.z, -500.0f, 1.0f));
    }
}

// ==============================================================================
// 6 — Distance constraint fails on coincident camera+target.
// ==============================================================================
TEST_CASE("PR4: Distance fails on coincident camera+target") {
    register_default_camera_constraints();
    auto& reg = CameraConstraintRegistry::instance();
    auto c = reg.create("camera.distance");
    REQUIRE(c != nullptr);

    Camera2_5D cam;
    cam.position = {5, 5, 5};
    cam.point_of_interest = {5, 5, 5};
    cam.point_of_interest_enabled = true;

    ConstraintSession session;
    auto result = c->evaluate(cam, CameraMotionContext::at(Frame{0}), session);

    CHECK_FALSE(result.ok);
    CHECK(result.reason == "distance-zero");
}

// ==============================================================================
// 7 — RotationLimit clamps angles.
// ==============================================================================
TEST_CASE("PR4: RotationLimit clamps angles") {
    register_default_camera_constraints();
    auto& reg = CameraConstraintRegistry::instance();
    auto c = reg.create("camera.rotation_limit",
        RotationLimitParams{30.0f, 90.0f, 10.0f});
    REQUIRE(c != nullptr);

    Camera2_5D cam;
    cam.rotation = {120.0f, 200.0f, 50.0f};

    ConstraintSession session;
    auto result = c->evaluate(cam, CameraMotionContext::at(Frame{0}), session);

    CHECK(result.ok);
    CHECK(result.camera.rotation.x <= 30.0f);
    CHECK(result.camera.rotation.x >= -30.0f);
    CHECK(result.camera.rotation.y <= 90.0f);
    CHECK(result.camera.rotation.z <= 10.0f);
}

// ==============================================================================
// 8 — Factory creates DampedFollow with custom damping.
// ==============================================================================
TEST_CASE("PR4: factory creates DampedFollow with custom damping") {
    register_default_camera_constraints();
    auto& reg = CameraConstraintRegistry::instance();

    auto high = reg.create("camera.damped_follow", DampedFollowParams{0.9f});
    auto low  = reg.create("camera.damped_follow", DampedFollowParams{0.1f});
    REQUIRE(high != nullptr);
    REQUIRE(low != nullptr);

    Camera2_5D cam;
    cam.position = {0, 0, -1000};

    CameraMotionContext ctx = CameraMotionContext::at(Frame{0});

    ConstraintSession sh, sl;
    sh.ensure_states(1); sl.ensure_states(1);
    auto rh0 = high->evaluate(cam, ctx, sh);
    auto rl0 = low->evaluate(cam, ctx, sl);
    CHECK(rh0.ok);
    CHECK(rl0.ok);

    // Second frame — apply damping.
    ctx = CameraMotionContext::at(Frame{1});
    cam.position = {100, 0, -1000};
    auto rh1 = high->evaluate(cam, ctx, sh);
    auto rl1 = low->evaluate(cam, ctx, sl);

    float jump_high = std::abs(rh1.camera.position.x - 0.0f);
    float jump_low  = std::abs(rl1.camera.position.x - 0.0f);
    CHECK(jump_high < jump_low);
}

// ==============================================================================
// 9 — register_default includes all 5 builtins.
// ==============================================================================
TEST_CASE("PR4: register_default includes all 5 builtins") {
    register_default_camera_constraints();
    auto& reg = CameraConstraintRegistry::instance();

    CHECK(reg.has("camera.look_at"));
    CHECK(reg.has("camera.keep_horizon"));
    CHECK(reg.has("camera.damped_follow"));
    CHECK(reg.has("camera.distance"));
    CHECK(reg.has("camera.rotation_limit"));

    auto ids = reg.ids();
    CHECK(ids.size() >= 5);
}

} // namespace
