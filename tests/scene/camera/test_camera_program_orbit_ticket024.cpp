// test_camera_program_orbit_ticket024.cpp — Split from
// test_camera_program_projection_orientation.cpp: §4.C TICKET-024 locks —
// orbit position math runs in the camera-local basis (forward/right/up),
// not world space.
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
// §4.C — TICKET-024: orbit position math is in the camera-local basis,
//                       not world space
// ══════════════════════════════════════════════════════════════════════════
//
// PRE-FIX:
//   `pos = target + forward*radius + track` ran in WORLD coordinates and
//   `pos.z += dolly` hard-coded dolly to world +Z.  Independent of yaw /
//   pitch, this meant pitch=90° rotated the camera off-axis but `track.x`
//   still pushed the camera in world +X (not camera-local +X), and `dolly`
//   always pushed the camera AWAY from the target in world +Z.
//
// POST-FIX (TICKET-024 / DOC 02):
//   basis_forward = normalize(target - orbit_position)
//   basis_right   = cross(basis_forward, world_up)  (fallback to world +X
//                                                    at pitch = ±90°)
//   basis_up      = cross(basis_right, basis_forward)
//   pos = orbit_position + track.x*basis_right + track.y*basis_up
//                       + dolly*basis_forward
//   (track.z no longer used; was un-scoped "lateral offset" — use dolly.)
//
//   These four subcases lock the new math against silent regressions:
//     4.C.1  orbit-position from yaw/pitch/radius     (spherical math)
//     4.C.2  track.x flips with yaw                   (camera-local right)
//     4.C.3  dolly moves camera toward target         (camera-local forward,
//                                                      not world +Z)
//     4.C.4  rotation coherence: same yaw+pitch+target at different radii
//            produces coherent intermediate state (DOC 02 spec).

TEST_CASE("compiled_orbit_basis_forward_per_yaw — "
          "TICKET-024: orbit_position at yaw=0/90/180/270 lands each unit "
          "circle direction and the camera-local basis_forward = "
          "normalize(target - orbit_position) lines up with point_of_interest "
          "= target (verified via cam.point_of_interest + position sanity)") {
    SUBCASE("yaw=   ->  pos = (0, 0, 1000),  poi = (0,0,0), poi_enabled=true") {
        auto desc = make_cam01_base_desc("test.t024.yaw_0");
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
        CAPTURE(cam.position.x); CAPTURE(cam.position.y); CAPTURE(cam.position.z);
        CHECK(cam.position.x == doctest::Approx(0.0f).epsilon(kCam01Eps));
        CHECK(cam.position.y == doctest::Approx(0.0f).epsilon(kCam01Eps));
        CHECK(cam.position.z == doctest::Approx(1000.0f).epsilon(kCam01Eps));
        CHECK(cam.point_of_interest_enabled);
        CHECK(cam.point_of_interest.x == doctest::Approx(0.0f).epsilon(kCam01Eps));
        CHECK(cam.point_of_interest.y == doctest::Approx(0.0f).epsilon(kCam01Eps));
        CHECK(cam.point_of_interest.z == doctest::Approx(0.0f).epsilon(kCam01Eps));
    }
    SUBCASE("yaw=180 ->  pos = (0, 0, -1000)") {
        auto desc = make_cam01_base_desc("test.t024.yaw_180");
        OrbitMotion orbit;
        orbit.target.set(Vec3{0.0f, 0.0f, 0.0f});
        orbit.yaw.set(180.0f);
        orbit.pitch.set(0.0f);
        orbit.radius.set(1000.0f);
        orbit.track.set(Vec3{0.0f, 0.0f, 0.0f});
        orbit.dolly.set(0.0f);
        orbit.roll.set(0.0f);
        desc.source = orbit;

        auto program = compile_or_die_cam01(desc);
        CameraSession session;
        auto cam = eval_at_or_die_cam01(program, session, Frame{0});
        // std::abs() for zero-axis: sin 180° ≈ 0 produces ~8.7e-05 float drift
        CHECK(std::abs(cam.position.x) < 1e-4f);
        CHECK(std::abs(cam.position.y) < 1e-4f);
        CHECK(cam.position.z == doctest::Approx(-1000.0f).epsilon(kCam01Eps));
    }
    SUBCASE("yaw=9  ->  pos = (1000, 0, 0)") {
        auto desc = make_cam01_base_desc("test.t024.yaw_90");
        OrbitMotion orbit;
        orbit.target.set(Vec3{0.0f, 0.0f, 0.0f});
        orbit.yaw.set(90.0f);
        orbit.pitch.set(0.0f);
        orbit.radius.set(1000.0f);
        orbit.track.set(Vec3{0.0f, 0.0f, 0.0f});
        orbit.dolly.set(0.0f);
        orbit.roll.set(0.0f);
        desc.source = orbit;

        auto program = compile_or_die_cam01(desc);
        CameraSession session;
        auto cam = eval_at_or_die_cam01(program, session, Frame{0});
        CHECK(cam.position.x == doctest::Approx(1000.0f).epsilon(kCam01Eps));
        CHECK(std::abs(cam.position.y) < 1e-4f);
        CHECK(std::abs(cam.position.z) < 1e-4f);
    }
    SUBCASE("yaw=270 ->  pos = (-1000, 0, 0)") {
        auto desc = make_cam01_base_desc("test.t024.yaw_270");
        OrbitMotion orbit;
        orbit.target.set(Vec3{0.0f, 0.0f, 0.0f});
        orbit.yaw.set(270.0f);
        orbit.pitch.set(0.0f);
        orbit.radius.set(1000.0f);
        orbit.track.set(Vec3{0.0f, 0.0f, 0.0f});
        orbit.dolly.set(0.0f);
        orbit.roll.set(0.0f);
        desc.source = orbit;

        auto program = compile_or_die_cam01(desc);
        CameraSession session;
        auto cam = eval_at_or_die_cam01(program, session, Frame{0});
        CHECK(cam.position.x == doctest::Approx(-1000.0f).epsilon(kCam01Eps));
        CHECK(std::abs(cam.position.y) < 1e-4f);
        CHECK(std::abs(cam.position.z) < 1e-4f);
    }
}

TEST_CASE("compiled_orbit_track_x_camera_local_basis — "
          "TICKET-024: track.x follows the camera-local right axis, which "
          "reverses on a half-orbit.  yaw=0,track=(100,0,0) -> +100 in "
          "world +X.  yaw=180,track=(100,0,0) -> +100 in world -X (because "
          "the orbit rotated the basis).  Pre-fix: both cases produced "
          "(100, 0, ±1000).") {
    // yaw=0° -> basis_forward=(0,0,-1), basis_right=(1,0,0), pos=(100,0,1000)
    {
        auto desc = make_cam01_base_desc("test.t024.track_x_yaw_0");
        OrbitMotion orbit;
        orbit.target.set(Vec3{0.0f, 0.0f, 0.0f});
        orbit.yaw.set(0.0f);
        orbit.pitch.set(0.0f);
        orbit.radius.set(1000.0f);
        orbit.track.set(Vec3{100.0f, 0.0f, 0.0f});   // camera-local +x = world +x
        orbit.dolly.set(0.0f);
        orbit.roll.set(0.0f);
        desc.source = orbit;

        auto program = compile_or_die_cam01(desc);
        CameraSession session;
        auto cam = eval_at_or_die_cam01(program, session, Frame{0});
        CAPTURE(cam.position.x); CAPTURE(cam.position.y); CAPTURE(cam.position.z);
        CHECK(cam.position.x == doctest::Approx(100.0f).epsilon(kCam01Eps));
        CHECK(cam.position.y == doctest::Approx(0.0f).epsilon(kCam01Eps));
        CHECK(cam.position.z == doctest::Approx(1000.0f).epsilon(kCam01Eps));
    }
    // yaw=180° -> basis_forward=(0,0,1), basis_right=(-1,0,0), pos=(-100,0,-1000)
    {
        auto desc = make_cam01_base_desc("test.t024.track_x_yaw_180");
        OrbitMotion orbit;
        orbit.target.set(Vec3{0.0f, 0.0f, 0.0f});
        orbit.yaw.set(180.0f);
        orbit.pitch.set(0.0f);
        orbit.radius.set(1000.0f);
        orbit.track.set(Vec3{100.0f, 0.0f, 0.0f});   // camera-local +x = world -x
        orbit.dolly.set(0.0f);
        orbit.roll.set(0.0f);
        desc.source = orbit;

        auto program = compile_or_die_cam01(desc);
        CameraSession session;
        auto cam = eval_at_or_die_cam01(program, session, Frame{0});
        CAPTURE(cam.position.x); CAPTURE(cam.position.y); CAPTURE(cam.position.z);
        CHECK(cam.position.x == doctest::Approx(-100.0f).epsilon(kCam01Eps));
        CHECK(cam.position.y == doctest::Approx(0.0f).epsilon(kCam01Eps));
        CHECK(cam.position.z == doctest::Approx(-1000.0f).epsilon(kCam01Eps));
    }
}

TEST_CASE("compiled_orbit_dolly_camera_local_basis — "
          "TICKET-024: dolly pushes the camera along the camera→target "
          "axis (NOT world +Z).  yaw=0,radius=1000,dolly=500 -> pos=(0,0,500) "
          "(toward target).  yaw=180,radius=1000,dolly=500 -> pos=(0,0,-500).  "
          "Pre-fix: dolly was hard-coded to world +Z so both cases produced "
          "+500 to z, irrespective of orbit direction (z=1500 at yaw=0, "
          "z=-500 at yaw=180).") {
    SUBCASE("yaw=0,   dolly=500, radius=1000 -> camera at (0,0,500)") {
        auto desc = make_cam01_base_desc("test.t024.dolly_yaw_0");
        OrbitMotion orbit;
        orbit.target.set(Vec3{0.0f, 0.0f, 0.0f});
        orbit.yaw.set(0.0f);
        orbit.pitch.set(0.0f);
        orbit.radius.set(1000.0f);
        orbit.track.set(Vec3{0.0f, 0.0f, 0.0f});
        orbit.dolly.set(500.0f);
        orbit.roll.set(0.0f);
        desc.source = orbit;

        auto program = compile_or_die_cam01(desc);
        CameraSession session;
        auto cam = eval_at_or_die_cam01(program, session, Frame{0});
        CAPTURE(cam.position.x); CAPTURE(cam.position.y); CAPTURE(cam.position.z);
        CHECK(cam.position.x == doctest::Approx(0.0f).epsilon(kCam01Eps));
        CHECK(cam.position.y == doctest::Approx(0.0f).epsilon(kCam01Eps));
        CHECK(cam.position.z == doctest::Approx(500.0f).epsilon(kCam01Eps));
    }
    SUBCASE("yaw=180, dolly=500, radius=1000 -> camera at (0,0,-500)") {
        auto desc = make_cam01_base_desc("test.t024.dolly_yaw_180");
        OrbitMotion orbit;
        orbit.target.set(Vec3{0.0f, 0.0f, 0.0f});
        orbit.yaw.set(180.0f);
        orbit.pitch.set(0.0f);
        orbit.radius.set(1000.0f);
        orbit.track.set(Vec3{0.0f, 0.0f, 0.0f});
        orbit.dolly.set(500.0f);
        orbit.roll.set(0.0f);
        desc.source = orbit;

        auto program = compile_or_die_cam01(desc);
        CameraSession session;
        auto cam = eval_at_or_die_cam01(program, session, Frame{0});
        CAPTURE(cam.position.x); CAPTURE(cam.position.y); CAPTURE(cam.position.z);
        // std::abs() for zero-axis: sin 180° ≈ 0 produces ~4.4e-05 float drift
        CHECK(std::abs(cam.position.x) < 1e-4f);
        CHECK(std::abs(cam.position.y) < 1e-4f);
        CHECK(cam.position.z == doctest::Approx(-500.0f).epsilon(kCam01Eps));
    }
}
