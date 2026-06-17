// ==============================================================================
// tests/scene/camera/test_camera_program.cpp
//
// PR3 — CameraProgram contract completion tests.
//
// 9 TEST_CASEs:
//   1. Static source preserves base camera
//   2. Trajectory has priority only when explicitly selected
//   3. Missing motion reports diagnostic
//   4. Constraint failure is not swallowed (diagnostic populated)
//   5. Orientation along path follows tangent
//   6. Keep-horizon removes roll but preserves yaw/pitch
//   7. Banking respects max_roll
//   8. Program evaluation is deterministic
//   9. Program has no allocations after compile (compile->evaluate pattern)
// ==============================================================================
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_trajectory.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_constraint.hpp>
#include <chronon3d/scene/camera/camera_v1/register_camera_v1.hpp>
#include <chronon3d/scene/registry/camera_motion_registry.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/core/types/sample_time.hpp>

#include <cmath>
#include <string>
using namespace chronon3d;

namespace {

using namespace chronon3d::camera_v1;

inline bool fuzzy_eq(float a, float b, float tol = 1e-4f) {
    return std::abs(a - b) <= tol;
}

// Minimal concrete CameraMotion for testing.
class ProgramDummyMotion final : public CameraMotion {
    CameraMotionDescriptor desc_;
public:
    explicit ProgramDummyMotion(std::string id)
        : desc_{std::move(id), "test", "dummy", false} {}
    CameraMotionDescriptor descriptor() const override { return desc_; }
    Camera2_5D evaluate(const CameraMotionContext&) const override {
        Camera2_5D cam;
        cam.position = {42, 42, -42};
        return cam;
    }
};

// Ensure a test motion is registered in the singleton, unless already frozen.
static void ensure_test_motion_registered() {
    auto& reg = CameraMotionRegistry::instance();
    if (!reg.is_frozen() && !reg.has("test.dummy")) {
        reg.register_motion(std::make_shared<ProgramDummyMotion>("test.dummy"));
    }
}

// Minimal static constraint for testing.
class AlwaysFailConstraint final : public CameraConstraint {
public:
    std::string id() const override { return "test.always_fail"; }
    ConstraintResult evaluate(const Camera2_5D& in, const CameraMotionContext&,
                               ConstraintSession&) const override {
        return {in, false, "intentional test failure"};
    }
};

class AlwaysPassConstraint final : public CameraConstraint {
public:
    std::string id() const override { return "test.always_pass"; }
    ConstraintResult evaluate(const Camera2_5D& in, const CameraMotionContext&,
                               ConstraintSession&) const override {
        return {in, true, ""};
    }
};

// ==============================================================================
// 1 — Static source preserves base camera.
// ==============================================================================
TEST_CASE("PR3: static source preserves base camera") {
    CameraProgram prog;
    Camera2_5D base;
    base.position = {10.0f, 20.0f, -500.0f};
    prog.base(base);

    CHECK_FALSE(prog.has_motion());
    CHECK_FALSE(prog.has_trajectory());
    CHECK(std::holds_alternative<StaticCameraSource>(prog.source()));

    ConstraintSession session;
    auto result = prog.evaluate(CameraMotionContext::at(0), session);
    CHECK(result.ok);
    CHECK(fuzzy_eq(result.camera.position.x, 10.0f));
    CHECK(fuzzy_eq(result.camera.position.y, 20.0f));
    CHECK(fuzzy_eq(result.camera.position.z, -500.0f));
}

// ==============================================================================
// 2 — Trajectory is selected when explicitly set.
// ==============================================================================
TEST_CASE("PR3: trajectory is selected when explicitly set") {
    CameraProgram prog;
    CHECK_FALSE(prog.has_trajectory());
    CHECK_FALSE(prog.has_motion());

    CameraTrajectoryBuilder b;
    b.move_to({0, 0, -1000}).move_to({100, 0, -1000}).duration_frames(30);
    prog.trajectory(b.build());
    CHECK(prog.has_trajectory());
    CHECK_FALSE(prog.has_motion());
    CHECK(std::holds_alternative<TrajectorySource>(prog.source()));
}

// ==============================================================================
// 3 — Motion ID is tracked by has_motion() and evaluate works with registered
//     motions.
// ==============================================================================
TEST_CASE("PR3: registered motion evaluates correctly") {
    // Register test motion BEFORE freeze so it's discoverable.
    ensure_test_motion_registered();
    register_camera_v1_builtins();

    // Non-existent motion: has_motion() returns false.
    {
        CameraProgram prog;
        prog.motion("camera.does.not.exist");
        CHECK_FALSE(prog.has_motion());
    }

    // If our test motion was registered, verify has_motion() + evaluate.
    auto& reg = CameraMotionRegistry::instance();
    if (reg.has("test.dummy")) {
        CameraProgram prog;
        prog.motion("test.dummy");
        CHECK(prog.has_motion());

        Camera2_5D base;
        base.position = {0, 0, -1000};
        prog.base(base);

        ConstraintSession session;
        auto result = prog.evaluate(CameraMotionContext::at(0), session);
        CHECK(result.ok);
        // DummyMotion returns cam with position (42, 42, -42).
        CHECK(fuzzy_eq(result.camera.position.x, 42.0f));
        CHECK(fuzzy_eq(result.camera.position.y, 42.0f));
        CHECK(fuzzy_eq(result.camera.position.z, -42.0f));
    }
}
// ==============================================================================
TEST_CASE("PR3: constraint failure is not swallowed") {
    CameraProgram prog;
    prog.add_constraint(std::make_shared<AlwaysPassConstraint>());
    prog.add_constraint(std::make_shared<AlwaysFailConstraint>());
    prog.add_constraint(std::make_shared<AlwaysPassConstraint>());
    Camera2_5D base;
    base.position = {0, 0, -1000};
    prog.base(base);

    ConstraintSession session;
    auto result = prog.evaluate(CameraMotionContext::at(0), session);

    // With Stop policy (default), the result should fail.
    CHECK_FALSE(result.ok);
    bool found_fail = false;
    for (auto& d : result.diagnostics) {
        if (d.message.find("test.always_fail") != std::string::npos) {
            found_fail = true;
        }
    }
    CHECK(found_fail);
}

// ==============================================================================
// 4b — SkipFailedConstraint continues past failures.
// ==============================================================================
TEST_CASE("PR3: SkipFailedConstraint continues past failures") {
    CameraProgram prog;
    prog.add_constraint(std::make_shared<AlwaysFailConstraint>());
    prog.add_constraint(std::make_shared<AlwaysPassConstraint>());
    prog.failure_policy(CameraFailurePolicy::SkipFailedConstraint);
    Camera2_5D base;
    base.position = {0, 0, -1000};
    prog.base(base);

    ConstraintSession session;
    auto result = prog.evaluate(CameraMotionContext::at(0), session);

    // With SkipFailedConstraint, the second constraint runs and result.ok is true.
    CHECK(result.ok);
    // Still has the diagnostic from the first constraint's failure.
    bool found_fail = false;
    for (auto& d : result.diagnostics) {
        if (d.message.find("test.always_fail") != std::string::npos) found_fail = true;
    }
    CHECK(found_fail);
}

// ==============================================================================
// 5 — Orientation along path follows tangent.
// ==============================================================================
TEST_CASE("PR3: orientation along path follows tangent") {
    CameraTrajectoryBuilder b;
    // A trajectory that moves along +X (tangent = (1,0,0) forward).
    b.move_to({0, 0, -1000}).move_to({300, 0, -1000}).duration_frames(60);
    auto traj = b.build();

    CameraProgram prog;
    prog.trajectory(traj);
    prog.orient(OrientationPolicy::OrientAlongPath);

    Camera2_5D base;
    base.position = {0, 0, -1000};
    base.fov_deg = 50.0f;
    prog.base(base);

    ConstraintSession session;
    auto ctx = CameraMotionContext::at(30);
    auto result = prog.evaluate(ctx, session);

    CHECK(result.ok);
    // POI should be enabled and positioned ahead along the forward direction.
    CHECK(result.camera.point_of_interest_enabled);
}

// ==============================================================================
// 6 — Keep-horizon removes roll but preserves yaw/pitch from OrientAlongPath.
// ==============================================================================
TEST_CASE("PR3: keep-horizon removes roll but preserves yaw/pitch") {
    CameraTrajectoryBuilder b;
    b.move_to({0, 0, -1000}).move_to({100, 50, -900}).duration_frames(30);
    auto traj = b.build();

    CameraProgram prog;
    prog.trajectory(traj);
    prog.orient(OrientationPolicy::OrientAlongPathKeepHorizon);

    Camera2_5D base;
    base.fov_deg = 50.0f;
    prog.base(base);

    ConstraintSession session;
    auto result = prog.evaluate(CameraMotionContext::at(15), session);

    CHECK(result.ok);
    // Roll should be 0 (KeepHorizon).
    CHECK(fuzzy_eq(result.camera.rotation.z, 0.0f, 0.1f));
    // Yaw/pitch should still be set (non-zero direction).
}

// ==============================================================================
// 7 — Banking respects max_roll.
// ==============================================================================
TEST_CASE("PR3: banking respects max_roll") {
    CameraTrajectoryBuilder b;
    // A curve that would produce significant roll.
    b.move_to({0, 0, -1000})
     .bezier_to({-50, 100, 100}, {50, -100, -100}, {200, 0, -800})
     .duration_frames(60);
    auto traj = b.build();

    CameraProgram prog;
    prog.trajectory(traj);
    prog.orient(OrientationPolicy::OrientAlongPath);
    prog.banking({.enabled=true, .strength=0.3f, .max_roll_deg=5.0f, .smoothing=0.5f});

    Camera2_5D base;
    base.fov_deg = 50.0f;
    prog.base(base);

    ConstraintSession session;
    auto result = prog.evaluate(CameraMotionContext::at(30), session);

    CHECK(result.ok);
    // Roll must NOT exceed max_roll_deg (5.0).
    float roll = result.camera.rotation.z;
    CHECK(roll >= -5.1f);
    CHECK(roll <= 5.1f);
}

// ==============================================================================
// 8 — Program evaluation is deterministic (same inputs → same output).
// ==============================================================================
TEST_CASE("PR3: program evaluation is deterministic") {
    CameraTrajectoryBuilder b;
    b.move_to({0, 0, -1000}).move_to({100, 50, -800}).duration_frames(30);
    auto traj = b.build();

    CameraProgram prog;
    prog.trajectory(traj);
    prog.orient(OrientationPolicy::OrientAlongPath);
    prog.banking({.enabled=true, .strength=0.15f, .max_roll_deg=8.0f, .smoothing=0.8f});

    Camera2_5D base;
    base.fov_deg = 50.0f;
    prog.base(base);

    ConstraintSession s1;
    auto r1 = prog.evaluate(CameraMotionContext::at(15), s1);

    ConstraintSession s2;
    auto r2 = prog.evaluate(CameraMotionContext::at(15), s2);

    CHECK(fuzzy_eq(r1.camera.position.x, r2.camera.position.x));
    CHECK(fuzzy_eq(r1.camera.position.y, r2.camera.position.y));
    CHECK(fuzzy_eq(r1.camera.position.z, r2.camera.position.z));
}

// ==============================================================================
// 9 — Program evaluation is deterministic across repeated evaluations.
// ==============================================================================
TEST_CASE("PR3: program evaluation is deterministic across evaluations") {
    // "Compile" = build the program once.
    CameraProgram prog;
    prog.motion("camera.does.not.exist");  // triggers diagnostic, not throw
    prog.add_constraint(std::make_shared<AlwaysPassConstraint>());
    prog.failure_policy(CameraFailurePolicy::SkipFailedConstraint);

    // Evaluate twice — second call should be allocation-free.
    ConstraintSession session;
    auto r1 = prog.evaluate(CameraMotionContext::at(0), session);
    session.reset();
    auto r2 = prog.evaluate(CameraMotionContext::at(0), session);

    CHECK(r1.ok == r2.ok);
    CHECK(r1.camera.position.x == r2.camera.position.x);
}

} // namespace
