// ==============================================================================
// tests/scene/camera/test_camera_registry.cpp
//
// PR2+ — Camera V1 constraint tests.
// CameraMotionRegistry removed (PR7), CameraConstraintRegistry removed (PR9).
// Uses factory functions directly.
//
// 2 TEST_CASEs:
//   1. All builtin constraints are constructible via factory functions
//   2. registry init is idempotent (CameraTransitionRegistry only)
// ==============================================================================
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/scene/camera/camera_v1/register_camera_v1.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_constraint.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>

using namespace chronon3d;

namespace {

using namespace chronon3d::camera_v1;

// ==============================================================================
// 1 — All builtin constraints are constructible via factory functions.
// ==============================================================================
TEST_CASE("PR2+: all builtin constraints constructible via factories") {
    CHECK(make_look_at_constraint() != nullptr);
    CHECK(make_keep_horizon_constraint() != nullptr);
    CHECK(make_damped_follow_constraint() != nullptr);
    CHECK(make_distance_constraint() != nullptr);
    CHECK(make_rotation_limit_constraint() != nullptr);
}

// ==============================================================================
// 2 — register_camera_v1_builtins is idempotent.
// ==============================================================================
TEST_CASE("PR2+: register_camera_v1_builtins is idempotent") {
    register_camera_v1_builtins();
    // Should not throw on second call.
    register_camera_v1_builtins();
    CHECK(true);  // reached = no crash
}

} // namespace
