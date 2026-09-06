// ==============================================================================
// tests/scene/camera/test_camera_program_compiled.cpp
//
// CAM-01 / DOC 04 — compiled camera path: SOURCE VARIANTS (§1).
// Projections/orientation live in test_camera_program_projection_orientation.cpp,
// constraints/failure policies/fingerprint/golden in
// test_camera_program_constraints_policy.cpp.
// ==============================================================================
//
// Split parts (each ≤ 400 LOC):
//   - test_camera_program_compiled_metadata.cpp    (missing preset +
//     TICKET-A3-METADATA late-rebuild lock + empty trajectory)
//   - test_camera_program_compiled_trajectory.cpp  (§1.B trajectory
//     CameraBaseSpec transfer + roll carry-forward)
//   - test_camera_program_orient_along_path.cpp    (OrientAlongPath group)
//
// This file does NOT include legacy headers (
// imperative camera headers) — the canonical compiled path is exercised end-to-end.
// ==============================================================================
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
#include <chronon3d/scene/model/camera/lens_model.hpp>  // LensPresets for PhysicalLens trajectory test
#include <chronon3d/scene/camera/camera_v1/camera_descriptor_fingerprint.hpp>  // §1.C: detail::Fnv1aHasher (canonical FNV-1a 64-bit)
#include <chronon3d/animation/path/spatial_bezier_path.hpp>  // §4.B.2: quat_look_along, quat_to_camera_euler (TICKET-022)
#include <chronon3d/animation/effects/wiggle.hpp>             // §4.B.2: wiggle3D for canonical HandheldNoise verification

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <string>
#include <string_view>

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
// §1 — SOURCE VARIANTS
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("compiled_static_source — "
          "StaticCameraSource produces exactly base state at any frame") {
    auto desc = make_cam01_base_desc();
    desc.source = StaticCameraSource{};
    auto program = compile_or_die_cam01(desc);
    REQUIRE_FALSE(program.is_time_dependent());

    CameraSession session;
    for (Frame f : {Frame{0}, Frame{15}, Frame{30}, Frame{60},
                     Frame{120}}) {
        auto cam = eval_at_or_die_cam01(program, session, f);
        CHECK(cam.position.x == doctest::Approx(0.0f).epsilon(kCam01Eps));
        CHECK(cam.position.y == doctest::Approx(0.0f).epsilon(kCam01Eps));
        CHECK(cam.position.z == doctest::Approx(-1000.0f).epsilon(kCam01Eps));
        CHECK(cam.rotation.x == doctest::Approx(0.0f).epsilon(kCam01Eps));
        CHECK(cam.rotation.y == doctest::Approx(0.0f).epsilon(kCam01Eps));
        CHECK(cam.rotation.z == doctest::Approx(0.0f).epsilon(kCam01Eps));
    }
}

TEST_CASE("compiled_pose_tracks_source — "
          "PoseTracksSource interpolates keyframed position + rotation + zoom") {
    auto desc = make_cam01_base_desc();
    PoseTracksSource pts;
    pts.position.key(Frame{0}, Vec3{0.0f, 0.0f, -1500.0f})
               .key(Frame{90}, Vec3{0.0f, 0.0f, -500.0f}, Easing::Linear);
    pts.rotation.key(Frame{0}, Vec3{0.0f, 0.0f, 0.0f})
                .key(Frame{90}, Vec3{0.0f, 30.0f, 0.0f}, Easing::Linear);
    pts.zoom.key(Frame{0}, 1000.0f)
           .key(Frame{90}, 1500.0f, Easing::Linear);
    pts.use_target = false;
    desc.source = pts;

    auto program = compile_or_die_cam01(desc);
    REQUIRE(program.is_time_dependent());

    CameraSession session;
    // Mid-frame interpolation: 50% between keyframes → ~1000.0 z (-1000±500),
    // 15° pitch (50% of 30°), zoom 1250 (50% of 1000-1500).
    auto cam_mid = eval_at_or_die_cam01(program, session, Frame{45});
    CAPTURE(cam_mid.position.z);
    CAPTURE(cam_mid.rotation.y);
    CAPTURE(cam_mid.zoom);
    CHECK(std::abs(cam_mid.position.z - (-1000.0f)) <= 2.0f);  // ±2 px slack
    CHECK(std::abs(cam_mid.rotation.y - 15.0f) <= 0.5f);
    CHECK(std::abs(cam_mid.zoom - 1250.0f) <= 5.0f);

    // At end-frame: terminal value.
    auto cam_end = eval_at_or_die_cam01(program, session, Frame{90});
    CHECK(cam_end.position.z == doctest::Approx(-500.0f).epsilon(kCam01Eps));
    CHECK(cam_end.rotation.y == doctest::Approx(30.0f).epsilon(kCam01Eps));
    CHECK(cam_end.zoom       == doctest::Approx(1500.0f).epsilon(kCam01Eps));
}

TEST_CASE("compiled_orbit_motion_source — "
          "OrbitMotion at yaw=0,pitch=0,radius=R puts camera at target + world_forward*R "
          "(track=(0,0,0), dolly=0 ⇒ pos = orbit_position exactly; TICKET-024 canonicalises "
          "track/dolly into the camera-local basis for non-zero offsets)") {
    auto desc = make_cam01_base_desc();
    OrbitMotion orbit;
    orbit.target.set(Vec3{0.0f, 0.0f, 0.0f});
    orbit.yaw.set(0.0f);     // facing -Z (cos(yaw)=1, sin(yaw)=0)
    orbit.pitch.set(0.0f);
    orbit.radius.set(1000.0f);
    orbit.track.set(Vec3{0.0f, 0.0f, 0.0f});
    orbit.dolly.set(0.0f);
    orbit.roll.set(0.0f);
    desc.source = orbit;

    auto program = compile_or_die_cam01(desc);
    REQUIRE(program.is_time_dependent());

    CameraSession session;
    auto cam = eval_at_or_die_cam01(program, session, Frame{0});
    CAPTURE(cam.position.x);
    CAPTURE(cam.position.y);
    CAPTURE(cam.position.z);
    // forward = (0, 0, 1), so position = target(0,0,0) + forward*radius(1000) = (0,0,1000).
    CHECK(cam.position.x == doctest::Approx(0.0f).epsilon(kCam01Eps));
    CHECK(cam.position.y == doctest::Approx(0.0f).epsilon(kCam01Eps));
    CHECK(cam.position.z == doctest::Approx(1000.0f).epsilon(kCam01Eps));
}

TEST_CASE("compiled_trajectory_motion_source — "
          "TrajectoryMotion samples camera trajectory.position + sets poi_enabled") {
    auto desc = make_cam01_base_desc();

    // Build a simple trajectory: start at (0,0,-1500), move to (0,0,-500).
    auto traj = CameraTrajectoryBuilder()
                    .move_to(Vec3{0.0f, 0.0f, -1500.0f})
                    .bezier_to(Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 0.0f},
                               Vec3{0.0f, 0.0f, -500.0f})
                    .duration_frames(90.0f)
                    .build();
    REQUIRE(traj);
    REQUIRE(traj->size() == 1);

    // Builder produces 1 point + 1 segment; use shared_ptr directly.
    desc.source = TrajectoryMotion{traj, /*use_arc_length=*/true};

    auto program = compile_or_die_cam01(desc);
    REQUIRE(program.is_time_dependent());

    CameraSession session;
    // Start frame: trajectory anchored at the move_to point (-1500 z).
    auto cam_start = eval_at_or_die_cam01(program, session, Frame{0});
    CAPTURE(cam_start.position.z);
    CHECK(cam_start.position.z == doctest::Approx(-1500.0f).epsilon(kCam01Eps));
    CHECK(cam_start.point_of_interest_enabled);

    // End of 90-frame segment: with zero bezier handles the segment reduces
    // to a straight line, so position approaches the bezier_to target
    // (z=-500) at the tail of the segment.  Sample Frame{89} (just before
    // the boundary) with ±2 px slack for floating-point integration drift.
    auto cam_end = eval_at_or_die_cam01(program, session, Frame{89});
    CAPTURE(cam_end.position.z);
    CHECK(std::abs(cam_end.position.z - (-500.0f)) <= 2.0f);

    // Structural guard: the camera MUST have moved past the midpoint from
    // the start point z=-1500.  This catches a regression where `sample()`
    // returns point[0] for every frame while remaining consistent with the
    // endpoint contract above.
    CHECK(cam_end.position.z > -1000.0f);
}

TEST_CASE("compiled_registered_motion_ref_resolved — "
          "RegisteredMotionRef resolves through CameraCatalog and replaces source") {
    CameraCatalog catalog(builtin_camera_presets());
    REQUIRE_FALSE(catalog.empty());
    REQUIRE(catalog.find_descriptor("camera.orbit") != nullptr);

    CameraDescriptor desc;
    desc.id = "test.registered_resolved";
    desc.source = RegisteredMotionRef{"camera.orbit"};
    desc.base.projection = ZoomProjection{AnimatedValue<float>{1000.0f}};
    desc.orientation = FixedOrientation{};

    auto program = compile_or_die_cam01(desc, &catalog);

    // After resolution, the descriptor's source must have been REPLACED with
    // the catalog entry's concrete variant — i.e. it is no longer a
    // RegisteredMotionRef.  builtin `camera.orbit` uses OrbitMotion.
    const auto* resolved = program.descriptor();
    REQUIRE(resolved != nullptr);
    CHECK_FALSE(std::holds_alternative<RegisteredMotionRef>(resolved->source));
}
