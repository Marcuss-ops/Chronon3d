// ==============================================================================
// tests/scene/camera/test_camera_registry.cpp
//
// PR2 — Camera V1 registry completion tests.
//
// 5 TEST_CASEs (CameraMotionRegistry removed — only constraint tests remain):
//   1. Duplicate constraint ID throws
//   2. Missing constraint returns explicit NotFound (nullptr)
//   3. All builtin IDs are present after register_camera_v1_builtins()
//   4. Registry initialization is idempotent
// ==============================================================================
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/scene/camera/camera_v1/register_camera_v1.hpp>
#include <chronon3d/scene/registry/camera_constraint_registry.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>

#include <string>
#include <vector>
#include <set>
using namespace chronon3d;

namespace {

using namespace chronon3d::camera_v1;

// ==============================================================================
// 1 — Duplicate constraint ID throws.
// ==============================================================================
TEST_CASE("PR2: duplicate constraint ID throws") {
    CameraConstraintRegistry& reg = CameraConstraintRegistry::instance();
    auto dummy_factory = +[](const CameraConstraintParams&) -> std::shared_ptr<CameraConstraint> {
        return nullptr;
    };
    if (!reg.has("camera.test_dup_constraint"))
        reg.register_factory("camera.test_dup_constraint", dummy_factory);

    bool threw = false;
    try {
        reg.register_factory("camera.test_dup_constraint", dummy_factory);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

// ==============================================================================
// 2 — Missing constraint returns explicit NotFound (nullptr from create).
// ==============================================================================
TEST_CASE("PR2: missing constraint create returns nullptr") {
    auto& reg = CameraConstraintRegistry::instance();
    auto c = reg.create("camera.does.not.exist.ever");
    CHECK(c == nullptr);
}

// ==============================================================================
// 3 — All builtin IDs are present after register_camera_v1_builtins().
// ==============================================================================
TEST_CASE("PR2: all builtin IDs are present") {
    register_camera_v1_builtins();

    auto& cr = CameraConstraintRegistry::instance();
    CHECK(cr.has("camera.look_at"));
    CHECK(cr.has("camera.keep_horizon"));
    CHECK(cr.has("camera.damped_follow"));

    // Constraints are actually creatable.
    auto look_at = cr.create("camera.look_at");
    CHECK(look_at != nullptr);
    CHECK(look_at->id() == "camera.look_at");

    auto keep_horizon = cr.create("camera.keep_horizon");
    CHECK(keep_horizon != nullptr);
    CHECK(keep_horizon->id() == "camera.keep_horizon");

    auto damped = cr.create("camera.damped_follow");
    CHECK(damped != nullptr);
    CHECK(damped->id() == "camera.damped_follow");
}

// ==============================================================================
// 4 — Registry initialization is idempotent (calling twice doesn't change size).
// ==============================================================================
TEST_CASE("PR2: registry initialization is idempotent") {
    register_camera_v1_builtins();
    auto ids1 = CameraConstraintRegistry::instance().ids();
    // Second call — no new registrations (idempotent).
    register_camera_v1_builtins();
    auto ids2 = CameraConstraintRegistry::instance().ids();

    CHECK(ids1.size() == ids2.size());
    for (std::size_t i = 0; i < ids1.size(); ++i) {
        CHECK(ids1[i] == ids2[i]);
    }
}

} // namespace
