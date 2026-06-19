// ==============================================================================
// tests/scene/camera/test_camera_registry.cpp
//
// PR2+ — Camera V1 registration tests.
// CameraMotionRegistry removed (PR7), CameraConstraintRegistry removed (PR9).
// Factory functions removed (PR12).
//
// 1 TEST_CASE:
//   1. register_camera_v1_builtins is idempotent
// ==============================================================================
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/scene/camera/camera_v1/register_camera_v1.hpp>

using namespace chronon3d;
using namespace chronon3d::camera_v1;

namespace {

// ==============================================================================
// 1 — register_camera_v1_builtins is idempotent.
// ==============================================================================
TEST_CASE("PR2+: register_camera_v1_builtins is idempotent") {
    register_camera_v1_builtins();
    // Should not throw on second call.
    register_camera_v1_builtins();
    CHECK(true);  // reached = no crash
}

} // namespace
