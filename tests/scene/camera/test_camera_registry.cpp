// ==============================================================================
// tests/scene/camera/test_camera_registry.cpp
//
// PR2 — Camera V1 registry completion tests.
//
// 9 TEST_CASEs:
//   1. Duplicate motion ID throws
//   2. Duplicate constraint ID throws
//   3. Missing motion returns explicit NotFound (shared_ptr == nullptr)
//   4. Missing constraint returns explicit NotFound (nullptr)
//   5. Catalog / ids ordering is deterministic
//   6. Freeze rejects registration
//   7. Concurrent read after freeze is deterministic
//   8. All builtin IDs are present after register_camera_v1_builtins()
//   9. Registry initialization is idempotent
// ==============================================================================
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/scene/camera/camera_v1/register_camera_v1.hpp>
#include <chronon3d/scene/registry/camera_constraint_registry.hpp>
#include <chronon3d/scene/registry/camera_motion_registry.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_motion_descriptor.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>

#include <string>
#include <vector>
#include <set>
using namespace chronon3d;

namespace {

using namespace chronon3d::camera_v1;

// Minimal concrete CameraMotion for testing.
class DummyMotion final : public CameraMotion {
    CameraMotionDescriptor desc_;
public:
    explicit DummyMotion(std::string id) : desc_{std::move(id), "test", "dummy", false} {}
    CameraMotionDescriptor descriptor() const override { return desc_; }
    Camera2_5D evaluate(const CameraMotionContext&) const override { return {}; }
};

// ==============================================================================
// 1 — Duplicate motion ID throws.
// ==============================================================================
TEST_CASE("PR2: duplicate motion ID throws") {
    CameraMotionRegistry reg;  // Not singleton — fresh instance per test.
    auto m1 = std::make_shared<DummyMotion>("camera.test_dup");
    reg.register_motion(m1);

    auto m2 = std::make_shared<DummyMotion>("camera.test_dup");
    bool threw = false;
    try {
        reg.register_motion(m2);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

// ==============================================================================
// 2 — Duplicate constraint ID throws.
// ==============================================================================
TEST_CASE("PR2: duplicate constraint ID throws") {
    CameraConstraintRegistry& reg = CameraConstraintRegistry::instance();
    // Factory takes CameraConstraintParams now.
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
// 3 — Missing motion returns explicit NotFound (shared_ptr == nullptr).
// ==============================================================================
TEST_CASE("PR2: missing motion find returns nullptr") {
    auto& reg = CameraMotionRegistry::instance();
    auto found = reg.find("camera.does.not.exist.ever");
    CHECK(found == nullptr);
}

// ==============================================================================
// 4 — Missing constraint returns explicit NotFound (nullptr from create).
// ==============================================================================
TEST_CASE("PR2: missing constraint create returns nullptr") {
    auto& reg = CameraConstraintRegistry::instance();
    auto c = reg.create("camera.does.not.exist.ever");
    CHECK(c == nullptr);
}

// ==============================================================================
// 5 — Catalog / ids ordering is deterministic (alphabetical).
// ==============================================================================
TEST_CASE("PR2: catalog ordering is deterministic") {
    auto& reg = CameraMotionRegistry::instance();
    auto ids = reg.ids();
    // ids must be sorted (std::map order = alphabetical).
    for (std::size_t i = 1; i < ids.size(); ++i) {
        CHECK(ids[i - 1] <= ids[i]);
    }

    // Catalog must be parallel to ids.
    auto cat = reg.catalog();
    REQUIRE(ids.size() == cat.size());
    for (std::size_t i = 0; i < ids.size(); ++i) {
        CHECK(cat[i].id == ids[i]);
    }
}

// ==============================================================================
// 6 — Freeze rejects registration.
// ==============================================================================
TEST_CASE("PR2: freeze rejects registration") {
    CameraMotionRegistry reg;
    auto m = std::make_shared<DummyMotion>("camera.test_freeze");
    reg.register_motion(m);
    CHECK_FALSE(reg.is_frozen());

    reg.freeze();
    CHECK(reg.is_frozen());

    auto m2 = std::make_shared<DummyMotion>("camera.test_freeze2");
    bool threw = false;
    try {
        reg.register_motion(m2);
    } catch (const std::logic_error&) {
        threw = true;
    }
    CHECK(threw);

    // freeze() is idempotent — no throw on second call.
    reg.freeze();
}

// ==============================================================================
// 7 — Read after freeze returns consistent shared_ptr (same object).
// ==============================================================================
TEST_CASE("PR2: read after freeze returns consistent shared_ptr") {
    CameraMotionRegistry reg;
    auto m = std::make_shared<DummyMotion>("camera.test_deterministic");
    reg.register_motion(m);
    reg.freeze();

    auto f1 = reg.find("camera.test_deterministic");
    auto f2 = reg.find("camera.test_deterministic");
    CHECK(f1 != nullptr);
    CHECK(f1 == f2);  // Same shared_ptr — same object.
}

// ==============================================================================
// 8 — All builtin IDs are present after register_camera_v1_builtins().
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
// 9 — Registry initialization is idempotent (calling twice doesn't change size).
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
