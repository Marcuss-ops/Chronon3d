// test_camera_program_orbit_coherence_roll_parent.cpp — Split from
// test_camera_program_projection_orientation.cpp: §4.C.4 orbit rotation
// coherence across radii and the §4.D orbit roll + parent propagation
// (AE parity) locks.
#include <doctest/doctest.h>

#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/animation/easing/easing.hpp>

#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp>
#include <chronon3d/internal/scene/camera/v1/camera_session.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_catalog.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_trajectory.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/model/camera/lens_model.hpp>

#include <cmath>
#include <memory>
#include <string>

using namespace chronon3d;
using namespace chronon3d::camera_v1;

namespace {

constexpr float kCam01Eps = 1e-5f;
constexpr FrameRate kCam01Fps{60, 1};

// Helper: compile + assert success + assert is_compiled().
CameraProgram compile_or_die_cam01(const CameraDescriptor& desc,
                              const CameraCatalog* catalog = nullptr) {
    auto result = compile_camera(desc, catalog);
    REQUIRE(result.has_value());
    auto program = std::move(result).value();
    REQUIRE(program.is_compiled());
    return program;
}

// Helper: evaluate at Frame + assert res.has_value().
Camera2_5D eval_at_or_die_cam01(const CameraProgram& program,
                           CameraSession& session, Frame frame) {
    CameraEvalContext ctx;
    ctx = ctx.with_frame(frame, FrameRate{30, 1});
    ctx.sample_time = SampleTime::from_frame_int(frame, kCam01Fps);
    auto res = program.evaluate(ctx, session);
    REQUIRE(res.has_value());
    return res.value().camera;
}

// ── Fixture: zero base, identity orientation, deterministic defaults. ──
CameraDescriptor make_cam01_base_desc(std::string id_str = "cam01.test") {
    CameraDescriptor desc;
    desc.id = std::move(id_str);
    desc.base.enabled = true;
    desc.base.position = Vec3{0.0f, 0.0f, -1000.0f};
    desc.base.rotation = Vec3{0.0f, 0.0f, 0.0f};
    desc.base.projection = ZoomProjection{AnimatedValue<float>{1000.0f}};
    desc.orientation = FixedOrientation{};
    return desc;
}

} // namespace

// ══════════════════════════════════════════════════════════════════════════
// §4.C (cont.) — rotation coherence independent of radius (DOC 02 spec)
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("compiled_orbit_rotation_coherence_independent_of_radius — "
          "DOC 02 / TICKET-024 explicit ask: orbit at a known target produces "
          "coherent intermediate state regardless of initial position (radius). "
          "At yaw=0, pitch=0, target=(0,0,0): radius=100 -> pos=(0,0,100); "
          "radius=1000 -> pos=(0,0,1000).  Both share the same point_of_interest "
          "(=target), same point_of_interest_enabled, same rotation=(0,0,0).") {
    SUBCASE("radius=100  -> pos = (0,0,100)") {
        auto desc = make_cam01_base_desc("test.t024.coh_r_100");
        OrbitMotion orbit;
        orbit.target.set(Vec3{0.0f, 0.0f, 0.0f});
        orbit.yaw.set(0.0f);
        orbit.pitch.set(0.0f);
        orbit.radius.set(100.0f);
        orbit.track.set(Vec3{0.0f, 0.0f, 0.0f});
        orbit.dolly.set(0.0f);
        orbit.roll.set(0.0f);
        desc.source = orbit;

        auto program = compile_or_die_cam01(desc);
        CameraSession session;
        auto cam = eval_at_or_die_cam01(program, session, Frame{0});
        CHECK(cam.position.z == doctest::Approx(100.0f).epsilon(kCam01Eps));
        CHECK(cam.rotation.x == doctest::Approx(0.0f).epsilon(kCam01Eps));
        CHECK(cam.rotation.y == doctest::Approx(0.0f).epsilon(kCam01Eps));
        CHECK(cam.rotation.z == doctest::Approx(0.0f).epsilon(kCam01Eps));
        CHECK(cam.point_of_interest_enabled);
        CHECK(cam.point_of_interest.x == doctest::Approx(0.0f).epsilon(kCam01Eps));
        CHECK(cam.point_of_interest.y == doctest::Approx(0.0f).epsilon(kCam01Eps));
        CHECK(cam.point_of_interest.z == doctest::Approx(0.0f).epsilon(kCam01Eps));
    }
    SUBCASE("radius=1000 -> pos = (0,0,1000); rotation / poi identical to r=100") {
        auto desc = make_cam01_base_desc("test.t024.coh_r_1000");
        OrbitMotion orbit;
        orbit.target.set(Vec3{0.0f, 0.0f, 0.0f});
        orbit.yaw.set(0.0f);
        orbit.pitch.set(0.0f);
        orbit.radius.set(1000.0f);
        orbit.track.set(Vec3{0.0f, 0.0f, 0.0f});
        orbit.dolly.set(0.0f);
        orbit.roll.set(0.0f);
        desc.source = orbit;

        auto program = compile_or_die_cam01(desc);
        CameraSession session;
        auto cam = eval_at_or_die_cam01(program, session, Frame{0});
        CHECK(cam.position.z == doctest::Approx(1000.0f).epsilon(kCam01Eps));
        CHECK(cam.rotation.x == doctest::Approx(0.0f).epsilon(kCam01Eps));
        CHECK(cam.rotation.y == doctest::Approx(0.0f).epsilon(kCam01Eps));
        CHECK(cam.rotation.z == doctest::Approx(0.0f).epsilon(kCam01Eps));
        CHECK(cam.point_of_interest_enabled);
        CHECK(cam.point_of_interest.x == doctest::Approx(0.0f).epsilon(kCam01Eps));
        CHECK(cam.point_of_interest.y == doctest::Approx(0.0f).epsilon(kCam01Eps));
        CHECK(cam.point_of_interest.z == doctest::Approx(0.0f).epsilon(kCam01Eps));
    }
}

// ══════════════════════════════════════════════════════════════════════════
// §4.D — TICKET-024: orbit roll + parent propagation (AE parity)
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("compiled_orbit_roll_rotation — "
          "OrbitMotion with roll=30 deg produces non-zero rotation.z on the camera "
          "(AE parity: orbit roll affects camera bank)") {
    auto desc = make_cam01_base_desc("test.t024.roll");
    OrbitMotion orbit;
    orbit.target.set(Vec3{0.0f, 0.0f, 0.0f});
    orbit.yaw.set(0.0f);
    orbit.pitch.set(0.0f);
    orbit.radius.set(1000.0f);
    orbit.track.set(Vec3{0.0f, 0.0f, 0.0f});
    orbit.dolly.set(0.0f);
    orbit.roll.set(30.0f);  // AE parity: orbit roll → camera bank (rotation.z)
    desc.source = orbit;

    auto program = compile_or_die_cam01(desc);
    CameraSession session;
    auto cam = eval_at_or_die_cam01(program, session, Frame{0});
    CAPTURE(cam.rotation.z);
    CHECK(cam.rotation.z == doctest::Approx(30.0f).epsilon(kCam01Eps));
    // position unaffected by roll (roll is a rotation around the look axis)
    CHECK(cam.position.z == doctest::Approx(1000.0f).epsilon(kCam01Eps));
}

TEST_CASE("compiled_orbit_with_parent — "
          "OrbitMotion with parent_name set on CameraBaseSpec carries parent_name "
          "through to the evaluated Camera2_5D (AE parity: orbit camera respects "
          "parent transform hierarchy)") {
    auto desc = make_cam01_base_desc("test.t024.parent");
    desc.base.parent_name = "camera_target_null";
    OrbitMotion orbit;
    orbit.target.set(Vec3{0.0f, 0.0f, 0.0f});
    orbit.yaw.set(0.0f);
    orbit.pitch.set(0.0f);
    orbit.radius.set(1000.0f);
    orbit.track.set(Vec3{0.0f, 0.0f, 0.0f});
    orbit.dolly.set(0.0f);
    orbit.roll.set(0.0f);
    desc.source = orbit;

    auto program = compile_or_die_cam01(desc);
    CameraSession session;
    auto cam = eval_at_or_die_cam01(program, session, Frame{0});
    // parent_name must propagate through the compiled path
    CHECK(cam.parent_name == "camera_target_null");
    // position/rotation carry through unchanged when no transforms are in context
    CHECK(cam.position.z == doctest::Approx(1000.0f).epsilon(kCam01Eps));
    CHECK(cam.rotation.z == doctest::Approx(0.0f).epsilon(kCam01Eps));
}
