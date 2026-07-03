// ==============================================================================
// tests/scene/camera/test_camera_program_compiled.cpp
//
// CAM-01 / DOC 04 — Baseline test suite for the COMPILED camera path.
//
// Scope (per docs/camera-plan/04-INTEGRATION_TESTS_AND_LEGACY_REMOVAL.md §
// "Test unitari compiler"):
//
//   ✓ Static source
//   ✓ PoseTracksSource
//   ✓ OrbitMotion
//   ✓ TrajectoryMotion
//   ✓ RegisteredMotionRef (via CameraCatalog)
//   ✓ Missing preset → compile error
//   ✓ Projection: ZoomProjection
//   ✓ Projection: FovProjection
//   ✗ Projection: PhysicalLensProjection        [CAM-03 — variant missing]
//   ✓ Modifier: IdleOscillation                 [only one in variant today]
//   ✗ Modifier: HandheldNoise                   [CAM-04 — struct commented out]
//   ✓ Orientation: FixedOrientation
//   ✓ Orientation: LookAtPoint
//   ✓ Orientation: LookAtLayer (no-op when transforms missing)
//   ✗ Orientation: OrientAlongPath             [CAM-03 — implementation stub]
//   ✓ All 5 constraint types (LookAt / KeepHorizon / DampedFollow /
//                                 Distance / RotationLimit)
//   ✓ All 3 failure policies (Stop / SkipFailedConstraint /
//                              KeepLastValidCamera)
//   ✓ Invalid descriptor: zero-segment trajectory → TrajectoryEmpty
//   ✓ is_time_dependent() flag correctness
//
// NOT YET COVERED — requires explicit follow-up PRs:
//
//   ✗ Cycle detection in catalog resolution     [CAM-02]
//   ✗ Deterministic fingerprint                 [CAM-02]
//   ✗ CameraEvaluationDependency metadata       [CAM-02]
//
// This file does NOT include legacy headers (animated_camera_2_5d.hpp /
// camera_rig.hpp) — the canonical compiled path is exercised end-to-end.
// ==============================================================================
#include <doctest/doctest.h>

#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/animation/easing/easing.hpp>

#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_session.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_catalog.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_trajectory.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/model/camera/lens_model.hpp>  // LensPresets for PhysicalLens trajectory test
#include <chronon3d/animation/path/spatial_bezier_path.hpp>  // §4.B.2: quat_look_along, quat_to_camera_euler (TICKET-022)
#include <chronon3d/animation/effects/wiggle.hpp>             // §4.B.2: wiggle3D for canonical HandheldNoise verification

#include <cmath>
#include <memory>
#include <string>       // std::string, std::string::npos — used by §8 textual contract test

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
    ctx.frame = frame;
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

    // Structural guard: the camera MUST have moved noticeably from the
    // start point z=-1500.  This catches a regression where `sample()`
    // returns point[0] for every frame (e.g. segment-selection bug) AND
    // would silently pass the ±2 px slack above — the broken math would
    // still produce z≈-1500, which fails the structural check below
    // (z<-1000 means the camera is past the midpoint of the segment),
    // AND acts as a no-reverse guard (any positive excursion fails).
    CHECK(cam_end.position.z < -1000.0f);
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

TEST_CASE("compiled_registered_motion_ref_missing — "
          "RegisteredMotionRef with unknown id returns MotionNotFound") {
    SUBCASE("with valid catalog: preset not found") {
        CameraCatalog catalog(builtin_camera_presets());
        CameraDescriptor desc;
        desc.id = "test.registered_missing";
        desc.source = RegisteredMotionRef{"camera.does_not_exist"};

        auto result = compile_camera(desc, &catalog);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().kind == CameraCompileError::Kind::MotionNotFound);
    }

    SUBCASE("without catalog and non-empty id: also MotionNotFound") {
        CameraDescriptor desc;
        desc.id = "test.no_catalog";
        desc.source = RegisteredMotionRef{"anything"};

        auto result = compile_camera(desc, /*catalog=*/nullptr);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().kind == CameraCompileError::Kind::MotionNotFound);
    }
}

TEST_CASE("compiled_invalid_trajectory_empty — "
          "TrajectoryMotion with zero segments returns TrajectoryEmpty") {
    // Build a trajectory that has 1 point but ZERO segments (move_to only,
    // no bezier_to/catmull_rom_to/hold_for afterwards).  This is the only
    // way to obtain size()==0 with a non-null shared_ptr — a null
    // shared_ptr would short-circuit the compiler's TrajectoryEmpty check
    // and let the descriptor compile (CAM-02 gap to harden).
    auto empty_traj = CameraTrajectoryBuilder()
                          .move_to(Vec3{0.0f, 0.0f, -1000.0f})
                          .build();
    REQUIRE(empty_traj);
    REQUIRE(empty_traj->size() == 0);

    CameraDescriptor desc = make_cam01_base_desc();
    desc.source = TrajectoryMotion{empty_traj, /*use_arc_length=*/true};

    auto result = compile_camera(desc, /*catalog=*/nullptr);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().kind == CameraCompileError::Kind::TrajectoryEmpty);
}

// ══════════════════════════════════════════════════════════════════════════
// §1.B — TrajectoryMotion full CameraBaseSpec transfer + OrientAlongPath
// ══════════════════════════════════════════════════════════════════════════
//
// The original trajectory branch in evaluate_compiled_source() stripped the
// camera down to position + hardcoded zoom=1000/fov=50 and dropped lens,
// projection variant, DOF, motion blur, parent_name, roll, tangent, and
// is_animated.  The fix makes the trajectory branch start from the full
// CameraBaseSpec, apply the canonical ProjectionSpec dispatch, and carry
// forward tangent + roll_deg for OrientAlongPath.

TEST_CASE("compiled_trajectory_transfers_projection_spec — "
          "TrajectoryMotion + FovProjection: camera.fov_deg MUST come from "
          "the descriptor's ProjectionSpec, NOT hardcoded 50.0f") {
    auto desc = make_cam01_base_desc("test.traj_fov");
    desc.base.projection = FovProjection{AnimatedValue<float>{72.0f}};

    auto traj = CameraTrajectoryBuilder()
                    .move_to(Vec3{0.0f, 0.0f, -1500.0f})
                    .bezier_to(Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 0.0f},
                               Vec3{0.0f, 0.0f, -500.0f})
                    .duration_frames(90.0f)
                    .build();
    REQUIRE(traj);
    desc.source = TrajectoryMotion{traj, /*use_arc_length=*/true};

    auto program = compile_or_die_cam01(desc);
    CameraSession session;
    auto cam = eval_at_or_die_cam01(program, session, Frame{0});
    CHECK(cam.fov_deg == doctest::Approx(72.0f).epsilon(kCam01Eps));
    CHECK(cam.optics_mode == CameraOpticsMode::FieldOfView);
}

TEST_CASE("compiled_trajectory_transfers_physical_lens — "
          "TrajectoryMotion + PhysicalLensProjection: lens MUST come from "
          "the projection variant, NOT hardcoded defaults") {
    auto desc = make_cam01_base_desc("test.traj_lens");
    LensModel lens = LensPresets::full_frame_85mm();
    lens.f_stop = 5.6f;
    desc.base.projection = PhysicalLensProjection{lens};

    auto traj = CameraTrajectoryBuilder()
                    .move_to(Vec3{0.0f, 0.0f, -1500.0f})
                    .bezier_to(Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 0.0f},
                               Vec3{0.0f, 0.0f, -500.0f})
                    .duration_frames(90.0f)
                    .build();
    REQUIRE(traj);
    desc.source = TrajectoryMotion{traj, /*use_arc_length=*/true};

    auto program = compile_or_die_cam01(desc);
    CameraSession session;
    auto cam = eval_at_or_die_cam01(program, session, Frame{0});
    CHECK(cam.optics_mode == CameraOpticsMode::PhysicalLens);
    CHECK(cam.lens.focal_length == doctest::Approx(85.0f).epsilon(kCam01Eps));
    CHECK(cam.lens.f_stop == doctest::Approx(5.6f).epsilon(kCam01Eps));
    // PhysicalLens resets zoom/fov to 0 (no stale 1000/50).
    CHECK(cam.zoom == doctest::Approx(0.0f).epsilon(kCam01Eps));
    CHECK(cam.fov_deg == doctest::Approx(0.0f).epsilon(kCam01Eps));
}

TEST_CASE("compiled_trajectory_transfers_dof_motion_blur_parent — "
          "TrajectoryMotion MUST carry forward DOF, motion blur, parent_name "
          "from CameraBaseSpec (previously dropped)") {
    auto desc = make_cam01_base_desc("test.traj_extras");
    desc.base.dof.enabled = true;
    desc.base.dof.focus_distance = 750.0f;
    desc.base.dof.aperture = 0.02f;
    desc.base.motion_blur.mode = MotionBlurMode::TemporalAccumulation;
    desc.base.motion_blur.samples = 16;
    desc.base.motion_blur.shutter_angle_deg = 90.0f;
    desc.base.parent_name = "parent_layer_42";

    auto traj = CameraTrajectoryBuilder()
                    .move_to(Vec3{0.0f, 0.0f, -1500.0f})
                    .bezier_to(Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 0.0f},
                               Vec3{0.0f, 0.0f, -500.0f})
                    .duration_frames(90.0f)
                    .build();
    REQUIRE(traj);
    desc.source = TrajectoryMotion{traj, /*use_arc_length=*/true};

    auto program = compile_or_die_cam01(desc);
    CameraSession session;
    auto cam = eval_at_or_die_cam01(program, session, Frame{0});
    CHECK(cam.dof.enabled);
    CHECK(cam.dof.focus_distance == doctest::Approx(750.0f).epsilon(kCam01Eps));
    CHECK(cam.dof.aperture == doctest::Approx(0.02f).epsilon(kCam01Eps));
    CHECK(cam.motion_blur.mode == MotionBlurMode::TemporalAccumulation);
    CHECK(cam.motion_blur.samples == 16);
    CHECK(cam.motion_blur.shutter_angle_deg == doctest::Approx(90.0f).epsilon(kCam01Eps));
    CHECK(cam.parent_name == "parent_layer_42");
    // is_animated must be true for a trajectory source.
    CHECK(cam.is_animated);
}

TEST_CASE("compiled_trajectory_transfers_roll_deg — "
          "TrajectoryMotion with roll_deg on trajectory points MUST produce "
          "a non-zero roll when OrientAlongPath is active") {
    auto desc = make_cam01_base_desc("test.traj_roll");

    auto traj = CameraTrajectoryBuilder()
                    .move_to(Vec3{0.0f, 0.0f, -1500.0f}, std::nullopt, /*roll_deg=*/15.0f)
                    .bezier_to(Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 0.0f},
                               Vec3{0.0f, 0.0f, -500.0f})
                    .duration_frames(90.0f)
                    .build();
    REQUIRE(traj);
    desc.source = TrajectoryMotion{traj, /*use_arc_length=*/true};
    desc.orientation = OrientAlongPath{/*keep_horizon=*/false};

    auto program = compile_or_die_cam01(desc);
    CameraSession session;
    auto cam = eval_at_or_die_cam01(program, session, Frame{0});
    // At frame 0, roll from trajectory point[0] is 15 degrees.
    // The camera should have a non-trivial rotation.z from the trajectory roll.
    CHECK(std::abs(cam.rotation.z - 15.0f) < 1.0f);
}

TEST_CASE("compiled_orient_along_path_straight_line — "
          "OrientAlongPath orients camera along trajectory tangent "
          "(non-degenerate straight line along +Z)") {
    auto desc = make_cam01_base_desc("test.oap_straight");

    // Trajectory from (0,0,-1500) to (0,0,-500) → tangent = (0,0,1) = +Z.
    auto traj = CameraTrajectoryBuilder()
                    .move_to(Vec3{0.0f, 0.0f, -1500.0f})
                    .bezier_to(Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 0.0f},
                               Vec3{0.0f, 0.0f, -500.0f})
                    .duration_frames(90.0f)
                    .build();
    REQUIRE(traj);
    desc.source = TrajectoryMotion{traj, /*use_arc_length=*/true};
    desc.orientation = OrientAlongPath{/*keep_horizon=*/false};

    auto program = compile_or_die_cam01(desc);
    CameraSession session;
    auto cam = eval_at_or_die_cam01(program, session, Frame{45});

    // With tangent = (0,0,1), the camera should look along +Z.
    // quat_look_along((0,0,1)) should produce near-identity rotation
    // (the camera's default forward is +Z in LH convention).
    // We check that the rotation is small (not a no-op sentinel, but the
    // correct orientation for a +Z look direction).
    const float rot_l2 = std::sqrt(cam.rotation.x * cam.rotation.x
                                   + cam.rotation.y * cam.rotation.y
                                   + cam.rotation.z * cam.rotation.z);
    CAPTURE(cam.rotation.x); CAPTURE(cam.rotation.y); CAPTURE(cam.rotation.z);
    CHECK(rot_l2 < 5.0f);  // should be near-zero for a +Z look
}

TEST_CASE("compiled_orient_along_path_off_axis — "
          "OrientAlongPath with a non-axial trajectory produces a "
          "non-trivial rotation (proves it's not a no-op)") {
    auto desc = make_cam01_base_desc("test.oap_offaxis");

    // Trajectory from (0,0,-1500) to (1000,0,-500) → tangent has +X component.
    auto traj = CameraTrajectoryBuilder()
                    .move_to(Vec3{0.0f, 0.0f, -1500.0f})
                    .bezier_to(Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 0.0f},
                               Vec3{1000.0f, 0.0f, -500.0f})
                    .duration_frames(90.0f)
                    .build();
    REQUIRE(traj);
    desc.source = TrajectoryMotion{traj, /*use_arc_length=*/true};
    desc.orientation = OrientAlongPath{/*keep_horizon=*/false};

    auto program = compile_or_die_cam01(desc);
    CameraSession session;
    auto cam = eval_at_or_die_cam01(program, session, Frame{45});

    // The tangent has a significant +X component, so the camera should
    // yaw to look in that direction.  rotation.y (pan) should be non-trivial.
    CAPTURE(cam.rotation.x); CAPTURE(cam.rotation.y); CAPTURE(cam.rotation.z);
    CHECK(std::abs(cam.rotation.y) > 1.0f);  // at least 1 degree of yaw
}

TEST_CASE("compiled_orient_along_path_keep_horizon — "
          "OrientAlongPath with keep_horizon=true zeroes roll even when "
          "trajectory has roll_deg") {
    auto desc = make_cam01_base_desc("test.oap_keephorizon");

    auto traj = CameraTrajectoryBuilder()
                    .move_to(Vec3{0.0f, 0.0f, -1500.0f}, std::nullopt, /*roll_deg=*/20.0f)
                    .bezier_to(Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 0.0f},
                               Vec3{0.0f, 0.0f, -500.0f})
                    .duration_frames(90.0f)
                    .build();
    REQUIRE(traj);
    desc.source = TrajectoryMotion{traj, /*use_arc_length=*/true};
    desc.orientation = OrientAlongPath{/*keep_horizon=*/true};

    auto program = compile_or_die_cam01(desc);
    CameraSession session;
    auto cam = eval_at_or_die_cam01(program, session, Frame{0});
    // keep_horizon forces roll=0 regardless of trajectory roll_deg.
    CHECK(cam.rotation.z == doctest::Approx(0.0f).epsilon(kCam01Eps));
}

TEST_CASE("compiled_orient_along_path_degenerate_hold — "
          "OrientAlongPath with a Hold segment (zero tangent) falls back "
          "to POI direction and emits a Warning diagnostic, producing a "
          "valid non-trivial orientation toward the POI") {
    auto desc = make_cam01_base_desc("test.oap_hold");
    desc.base.point_of_interest_enabled = true;
    desc.base.point_of_interest = Vec3{0.0f, 100.0f, 0.0f};  // off-axis POI

    // Build a trajectory: move to a point, then hold for 30 frames.
    // During the hold, the tangent is (0,0,0) → degenerate.
    auto traj = CameraTrajectoryBuilder()
                    .move_to(Vec3{0.0f, 0.0f, -1000.0f})
                    .bezier_to(Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 0.0f},
                               Vec3{0.0f, 0.0f, -500.0f})
                    .duration_frames(30.0f)
                    .hold_for(30.0f)
                    .build();
    REQUIRE(traj);
    REQUIRE(traj->size() == 2);  // 1 bezier + 1 hold
    desc.source = TrajectoryMotion{traj, /*use_arc_length=*/true};
    desc.orientation = OrientAlongPath{/*keep_horizon=*/false};

    auto program = compile_or_die_cam01(desc);
    CameraSession session;

    // Evaluate at a frame within the hold segment (segment 0 ends at frame 30,
    // so frame 45 is mid-hold → tangent = (0,0,0)).
    CameraEvalContext ctx;
    ctx.frame = Frame{45};
    ctx.sample_time = SampleTime::from_frame_int(Frame{45}, kCam01Fps);
    auto res = program.evaluate(ctx, session);
    REQUIRE(res.has_value());

    // Should have a warning diagnostic about the degenerate tangent fallback.
    bool found_fallback_warning = false;
    for (const auto& d : res->diagnostics) {
        if (d.severity == CameraProgramDiagnostic::Severity::Warning &&
            d.message.find("OrientAlongPath") != std::string::npos) {
            found_fallback_warning = true;
            break;
        }
    }
    CHECK(found_fallback_warning);

    // Verify the fallback actually produced a valid orientation toward POI.
    // POI = (0,100,0), camera at (0,0,-500) → look_dir = (0,100,500) →
    // non-trivial pitch (rotation.x).  This proves the fallback didn't
    // just silently keep base rotation = (0,0,0).
    CHECK(std::abs(res->camera.rotation.x) > 0.5f);
}

TEST_CASE("compiled_orient_along_path_with_static_source_no_crash — "
          "OrientAlongPath with a non-trajectory source (no tangent) "
          "falls back to POI direction without crashing AND emits a "
          "Warning diagnostic about the missing tangent") {
    auto desc = make_cam01_base_desc("test.oap_static");
    desc.source = StaticCameraSource{};
    desc.base.point_of_interest_enabled = true;
    desc.base.point_of_interest = Vec3{0.0f, 0.0f, 0.0f};
    desc.orientation = OrientAlongPath{/*keep_horizon=*/false};

    auto program = compile_or_die_cam01(desc);
    CameraSession session;

    CameraEvalContext ctx;
    ctx.frame = Frame{0};
    ctx.sample_time = SampleTime::from_frame_int(Frame{0}, kCam01Fps);
    auto res = program.evaluate(ctx, session);
    REQUIRE(res.has_value());
    // With no tangent and no trajectory, it should fall back to POI direction.
    // POI is (0,0,0), camera at (0,0,-1000), so it looks along +Z.
    // This should not crash and should produce a valid camera.
    CHECK(res.value().camera.enabled);

    // A static source with OrientAlongPath has no tangent — it MUST emit
    // a Warning diagnostic about the fallback (step 3 or step 4).
    bool found_warning = false;
    for (const auto& d : res->diagnostics) {
        if (d.severity == CameraProgramDiagnostic::Severity::Warning &&
            d.message.find("OrientAlongPath") != std::string::npos) {
            found_warning = true;
            break;
        }
    }
    CHECK(found_warning);
}

TEST_CASE("compiled_orient_along_path_last_tangent_persistence — "
          "OrientAlongPath: after a frame with a valid tangent, a subsequent "
          "degenerate-tangent frame uses the preserved session.last_tangent "
          "(fallback step 2), NOT the POI direction (step 3)") {
    auto desc = make_cam01_base_desc("test.oap_persist");
    desc.base.point_of_interest_enabled = true;
    desc.base.point_of_interest = Vec3{0.0f, 100.0f, 0.0f};  // off-axis POI

    // Build a trajectory with an off-axis bezier segment then a hold.
    // The bezier goes from (0,0,-1000) to (1000,0,-500), giving a tangent
    // with a +X component.  The hold has tangent = (0,0,0) → degenerate.
    auto traj = CameraTrajectoryBuilder()
                    .move_to(Vec3{0.0f, 0.0f, -1000.0f})
                    .bezier_to(Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 0.0f},
                               Vec3{1000.0f, 0.0f, -500.0f})
                    .duration_frames(30.0f)
                    .hold_for(30.0f)
                    .build();
    REQUIRE(traj);
    REQUIRE(traj->size() == 2);
    desc.source = TrajectoryMotion{traj, /*use_arc_length=*/true};
    desc.orientation = OrientAlongPath{/*keep_horizon=*/false};

    auto program = compile_or_die_cam01(desc);
    CameraSession session;

    // Frame 15: mid-bezier → valid tangent with +X component.
    // This populates session.last_tangent.
    CameraEvalContext ctx1;
    ctx1.frame = Frame{15};
    ctx1.sample_time = SampleTime::from_frame_int(Frame{15}, kCam01Fps);
    auto res1 = program.evaluate(ctx1, session);
    REQUIRE(res1.has_value());
    // The tangent has a +X component, so the camera should yaw right.
    CHECK(std::abs(res1->camera.rotation.y) > 1.0f);

    // Frame 45: mid-hold → tangent = (0,0,0) → degenerate.
    // session.last_tangent should be populated from frame 15.
    CameraEvalContext ctx2;
    ctx2.frame = Frame{45};
    ctx2.sample_time = SampleTime::from_frame_int(Frame{45}, kCam01Fps);
    auto res2 = program.evaluate(ctx2, session);
    REQUIRE(res2.has_value());

    // Should emit a warning about the degenerate tangent fallback.
    bool found_warning = false;
    for (const auto& d : res2->diagnostics) {
        if (d.severity == CameraProgramDiagnostic::Severity::Warning &&
            d.message.find("previous frame tangent") != std::string::npos) {
            found_warning = true;
            break;
        }
    }
    CHECK(found_warning);

    // The fallback uses the preserved tangent (from frame 15, +X component),
    // NOT the POI direction (which is (0,100,0) - (1000,0,-500) → mostly -X).
    // If it used POI direction, the yaw would be very different.
    // The preserved tangent yaw should match the frame-15 yaw.
    CHECK(res2->camera.rotation.y == doctest::Approx(res1->camera.rotation.y).epsilon(1.0f));
}

// ══════════════════════════════════════════════════════════════════════════
// §2 — PROJECTION VARIANTS
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("compiled_projection_zoom — "
          "ZoomProjection propagates AnimatedValue zoom into Camera2_5D::zoom") {
    auto desc = make_cam01_base_desc();
    desc.base.projection = ZoomProjection{AnimatedValue<float>{2500.0f}};
    desc.source = StaticCameraSource{};

    auto program = compile_or_die_cam01(desc);
    CameraSession session;
    auto cam = eval_at_or_die_cam01(program, session, Frame{0});
    CHECK(cam.zoom == doctest::Approx(2500.0f).epsilon(kCam01Eps));
}

TEST_CASE("compiled_projection_fov — "
          "FovProjection sets camera.projection_mode to Fov") {
    auto desc = make_cam01_base_desc();
    desc.base.projection = FovProjection{AnimatedValue<float>{60.0f}};
    desc.source = StaticCameraSource{};

    auto program = compile_or_die_cam01(desc);
    CameraSession session;
    auto cam = eval_at_or_die_cam01(program, session, Frame{0});
    CHECK(cam.fov_deg == doctest::Approx(60.0f).epsilon(kCam01Eps));
}

// ══════════════════════════════════════════════════════════════════════════
// §2.A — TICKET-021: variance-preserving dispatch for PoseTracksSource
// ══════════════════════════════════════════════════════════════════════════
//
// The original eval_pose_tracks() in src/scene/camera/camera_v1/camera_program.cpp
// ran apply_projection_spec() correctly and then unconditionally rewrote:
//
//     cam.projection_mode = Camera2_5DProjectionMode::Zoom;
//     cam.optics_mode     = CameraOpticsMode::Zoom;
//
// This wiped out FovProjection and PhysicalLensProjection — the variant
// decided by descriptor_.base was silently thrown away.  The fix in
// TICKET-021 dispatches pose-track channels onto the ACTIVE variant only,
// and never re-states `projection_mode`/`optics_mode` (apply_projection_spec
// is the single canonical writepoint).  These three tests lock the
// behaviour: static + projection-variant is unaffected, and the three
// variant combinations under PoseTracksSource all preserve their mode +
// carry the right channels.

TEST_CASE("compiled_pose_tracks_fov_no_zoom_override — "
          "PoseTracksSource + FovProjection: optics_mode MUST stay "
          "FieldOfView even when pose tracks carry NO fov_deg keys "
          "(TICKET-021 regression)") {
    auto desc = make_cam01_base_desc("test.t021.fov_static");
    desc.base.projection = FovProjection{AnimatedValue<float>{45.0f}};
    PoseTracksSource pts;
    // No keyframes on fov_deg / zoom; only position is animated.  The pose
    // track MUST NOT touch the FovProjection channel when src.fov_deg has
    // no animation; the base 45 deg value MUST survive.
    pts.position.key(Frame{0},  Vec3{0.0f, 0.0f, -1500.0f}, Easing::Linear)
               .key(Frame{60}, Vec3{0.0f, 0.0f, -500.0f},  Easing::Linear);
    desc.source = pts;

    auto program = compile_or_die_cam01(desc);
    CameraSession session;
    auto cam = eval_at_or_die_cam01(program, session, Frame{30});

    CHECK(cam.optics_mode     == CameraOpticsMode::FieldOfView);
    CHECK(cam.fov_deg == doctest::Approx(45.0f).epsilon(kCam01Eps));
}

TEST_CASE("compiled_pose_tracks_fov_with_animated_fov — "
          "PoseTracksSource + FovProjection: animated fov_deg wins AND "
          "optics_mode stays FieldOfView (TICKET-021 regression)") {
    auto desc = make_cam01_base_desc("test.t021.fov_animated");
    desc.base.projection = FovProjection{AnimatedValue<float>{30.0f}};
    PoseTracksSource pts;
    // Animated fov_deg: 60 → 30 over 60 frames.  Position is held so the
    // dolly reading is pure rotation-of-FOV, no parallax noise.
    pts.fov_deg.key(Frame{0},  60.0f)
              .key(Frame{60}, 30.0f, Easing::Linear);
    pts.position.key(Frame{0},  Vec3{0.0f, 0.0f, -1000.0f}, Easing::Linear)
               .key(Frame{60}, Vec3{0.0f, 0.0f, -1000.0f}, Easing::Linear);
    desc.source = pts;

    auto program = compile_or_die_cam01(desc);
    CameraSession session;
    // At Frame{30}: 50% linear interpolation between 60 and 30 → 45 deg.
    auto cam_mid = eval_at_or_die_cam01(program, session, Frame{30});
    CHECK(cam_mid.optics_mode     == CameraOpticsMode::FieldOfView);
    CHECK(cam_mid.fov_deg == doctest::Approx(45.0f).epsilon(0.01f));
}

TEST_CASE("compiled_pose_tracks_physical_lens_no_clobber — "
          "PoseTracksSource + PhysicalLensProjection: optics_mode stays "
          "PhysicalLens AND descriptor.base.lens MUST NOT overwrite the "
          "physical lens carried by the projection variant "
          "(TICKET-021 regression)") {
    auto desc = make_cam01_base_desc("test.t021.physlens");
    // Build a sentinel LensModel that differs from base.lens defaults in
    // focal_length, sensor/crop, and f_stop.  After the fix, every value
    // must propagate from this descriptor-level lens, NOT from
    // descriptor.base.lens (50mm / 2.8f / 36x24).
    LensModel lens = LensPresets::full_frame_85mm();
    lens.f_stop    = 4.0f;     // sentinel vs default 2.8f
    desc.base.projection = PhysicalLensProjection{lens};
    PoseTracksSource pts;
    // Deliberately no keyframes on zoom / fov_deg — apply_projection_spec
    // already zeroed them on entry; if eval_pose_tracks forced Zoom or
    // base.lens afterwards, this snapshot would diverge.
    pts.position.key(Frame{0},  Vec3{0.0f, 0.0f, -1500.0f}, Easing::Linear)
               .key(Frame{60}, Vec3{0.0f, 0.0f, -500.0f},  Easing::Linear);
    desc.source = pts;

    auto program = compile_or_die_cam01(desc);
    CameraSession session;
    auto cam = eval_at_or_die_cam01(program, session, Frame{30});

    CHECK(cam.optics_mode     == CameraOpticsMode::PhysicalLens);
    // PhysicalLens carries a LensModel; canonical source-of-truth.
    CHECK(cam.lens.focal_length  == doctest::Approx(85.0f).epsilon(kCam01Eps));
    CHECK(cam.lens.sensor_width  == doctest::Approx(36.0f).epsilon(kCam01Eps));
    CHECK(cam.lens.sensor_height == doctest::Approx(24.0f).epsilon(kCam01Eps));
    CHECK(cam.lens.f_stop        == doctest::Approx(4.0f).epsilon(kCam01Eps));
    // apply_projection_spec resets cam.zoom / cam.fov_deg to 0 for the
    // physical projection snapshot; pose tracks MUST NOT pollute them.
    CHECK(cam.zoom    == doctest::Approx(0.0f).epsilon(kCam01Eps));
    CHECK(cam.fov_deg == doctest::Approx(0.0f).epsilon(kCam01Eps));
}

// ══════════════════════════════════════════════════════════════════════════
// §3 — MODIFIER (only IdleOscillation implemented today)
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("compiled_modifier_idle_oscillation — "
          "IdleOscillation adds sinusoidal offset on top of base position") {
    auto desc = make_cam01_base_desc();
    desc.source = StaticCameraSource{};  // base position: (0,0,-1000)
    IdleOscillation idle;
    idle.position_amplitude = Vec3{0.5f, 0.0f, 0.0f};
    idle.rotation_amplitude_deg = Vec3{0.0f, 0.0f, 0.0f};
    idle.zoom_amplitude = 0.0f;
    idle.frequency_hz = 1.0f;
    idle.phase = 0.0f;
    desc.modifiers.push_back(idle);

    auto program = compile_or_die_cam01(desc);
    // Even with static source, modifiers trigger time_dependent=true.
    CHECK(program.is_time_dependent());

    CameraSession session;
    // Sample at t=0 (phase=0, sin(0)=0, cos(0)=1): x offset = amp * sin(0) = 0.
    auto cam_t0 = eval_at_or_die_cam01(program, session, Frame{0});
    CHECK(cam_t0.position.x == doctest::Approx(0.0f).epsilon(kCam01Eps));
    CHECK(cam_t0.position.z == doctest::Approx(-1000.0f).epsilon(kCam01Eps));

    // Sample at t = period/4 — sin(pi/2)=1, so x offset peaks at +0.5.
    auto cam_quarter = eval_at_or_die_cam01(program, session, Frame{15});
    CAPTURE(cam_quarter.position.x);
    CHECK(std::abs(cam_quarter.position.x - 0.5f) <= 0.05f);
}

// ══════════════════════════════════════════════════════════════════════════
// §4 — ORIENTATION
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("compiled_orientation_fixed — "
          "FixedOrientation preserves source rotation unchanged") {
    auto desc = make_cam01_base_desc();
    desc.source = StaticCameraSource{};
    desc.base.rotation = Vec3{1.0f, 2.0f, 3.0f};
    desc.orientation = FixedOrientation{};

    auto program = compile_or_die_cam01(desc);
    CameraSession session;
    auto cam = eval_at_or_die_cam01(program, session, Frame{0});
    CHECK(cam.rotation.x == doctest::Approx(1.0f).epsilon(kCam01Eps));
    CHECK(cam.rotation.y == doctest::Approx(2.0f).epsilon(kCam01Eps));
    CHECK(cam.rotation.z == doctest::Approx(3.0f).epsilon(kCam01Eps));
}

TEST_CASE("compiled_orientation_look_at_point — "
          "LookAtPoint sets point_of_interest_enabled AND off-axis "
          "look-at produces a non-trivial rotation") {
    // Off-axis point relative to camera at (0,0,-1000).  A target along
    // the +Z axis would trivially leave rotation near identity, so we
    // use (100, 100, 1000) to force a non-zero yaw + pitch.
    const Vec3 kCamPos{0.0f, 0.0f, -1000.0f};
    const Vec3 kTarget{100.0f, 100.0f, 1000.0f};

    auto desc = make_cam01_base_desc();
    desc.source = StaticCameraSource{};
    desc.base.position = kCamPos;
    desc.base.rotation = Vec3{0.0f, 0.0f, 0.0f};   // identity baseline
    desc.orientation = LookAtPoint{kTarget};

    auto program = compile_or_die_cam01(desc);
    CameraSession session;
    auto cam = eval_at_or_die_cam01(program, session, Frame{0});

    CHECK(cam.point_of_interest_enabled);
    CHECK(cam.point_of_interest.x == doctest::Approx(kTarget.x).epsilon(kCam01Eps));
    CHECK(cam.point_of_interest.y == doctest::Approx(kTarget.y).epsilon(kCam01Eps));
    CHECK(cam.point_of_interest.z == doctest::Approx(kTarget.z).epsilon(kCam01Eps));

    // Structural guard: off-axis look-at MUST produce a rotation whose
    // L2 magnitude is far above float noise.  With the target
    // (100, 100, 1000) from cam (0, 0, -1000), the look-direction
    // pitch angle is ~atand(sqrt(20000)/2000) ≈ 4°, so total rotation
    // magnitude should be ≫ 1° — we use 0.5° ≈ 0.0087 deg as the floor,
    // which catches a regression where the look-at is silently skipped
    // (rotation ≈ 0) but does NOT accept a 0.001° micro-glitch from
    // float drift in a partially-broken implementation.
    //
    // UNIT: `cam.rotation` is in DEGREES for the entire camera_v1 compiled
    // path (consistent with `PoseTracksSource::rotation` keys and
    // `IdleOscillation::rotation_amplitude_deg`).  The threshold value is
    // therefore also in degrees, not radians.
    const float rot_l2 = std::sqrt(cam.rotation.x * cam.rotation.x
                                   + cam.rotation.y * cam.rotation.y
                                   + cam.rotation.z * cam.rotation.z);
    CAPTURE(rot_l2);
    constexpr float kMinLookAtRotationDeg = 0.5f;     // 0.5° in degrees
    CHECK(rot_l2 > kMinLookAtRotationDeg);
}

TEST_CASE("compiled_orientation_look_at_layer_no_transforms — "
          "LookAtLayer with no transforms in ctx is a safe no-op (no crash)") {
    auto desc = make_cam01_base_desc();
    desc.source = StaticCameraSource{};
    desc.orientation = LookAtLayer{"non.existent.layer"};

    auto program = compile_or_die_cam01(desc);
    CameraSession session;

    // CameraEvalContext with NO `transforms` field — LookAtLayer gracefully
    // returns without modifying rotation, mirroring the in-camera_program.cpp
    // early-return when ctx.transforms == nullptr.
    CameraEvalContext ctx;
    ctx.frame = Frame{0};
    ctx.sample_time = SampleTime::from_frame_int(Frame{0}, kCam01Fps);
    auto res = program.evaluate(ctx, session);
    REQUIRE(res.has_value());
    CHECK(res->camera.rotation.x == doctest::Approx(0.0f).epsilon(kCam01Eps));
    CHECK(res->camera.rotation.y == doctest::Approx(0.0f).epsilon(kCam01Eps));
    CHECK(res->camera.rotation.z == doctest::Approx(0.0f).epsilon(kCam01Eps));
    CHECK_FALSE(res->camera.point_of_interest_enabled);
}

// ══════════════════════════════════════════════════════════════════════════
// ══════════════════════════════════════════════════════════════════════════
// §4.B — TICKET-022: single-application canonical-order lock
// ══════════════════════════════════════════════════════════════════════════
//
// The fix in TICKET-022 strips three redundant `apply_orientation_spec_free`
// calls (`eval_pose_tracks`, `eval_orbit_motion`, the trajectory branch in
// `evaluate_compiled_source`) so that orientation is applied EXACTLY ONCE in
// the canonical order (`base → modifier → orientation → constraints`), by
// `CameraProgram::evaluate()` post-modifiers.  These three tests lock the
// resulting invariants:
//
//   4.B.1  Determinism: SAME descriptor + SAME ctx+session → SAME quaternion,
//          compiled into two CameraProgram instances.  Hard guarantee that
//          the math is a pure function of inputs (no global state, no
//          cross-evaluation bleed-through).
//   4.B.2  Canonical-order Application: LookAtPoint orientation + HandheldNoise
//          modifier → final rotation = quat_look_along(target - modified_pos),
//          where modified_pos = source_pos + wiggle3D_position_offset(t).  This
//          proves the look-at is derived from the MODIFIED position (post-mods),
//          not from the source position (pre-mods).
//   4.B.3  Single-Look-At Policy: LookAtPoint orientation + LookAtConstraint
//          → constraint is silently skipped, only orientation rotates.  A
//          Warning diagnostic is recorded.  Final POI is the orientation's
//          target, never the constraint's target.

TEST_CASE("compiled_orientation_double_application_determinism — "
          "TICKET-022 lock: same descriptor + same ctx+session → "
          "rotation matches across two independently-compiled CameraPrograms "
          "(math is a pure function of inputs, no double-application drift)") {
    auto desc = make_cam01_base_desc("test.t022.det");
    desc.base.position = Vec3{0.0f, 0.0f, -1000.0f};
    // Off-axis target so any deliberate-bias would surface; LookAtPoint rotates
    // toward (200, 50, +1500) from a source at (0, 0, -1000).
    desc.orientation = LookAtPoint{Vec3{200.0f, 50.0f, 1500.0f}};
    desc.source = StaticCameraSource{};

    auto p1 = compile_or_die_cam01(desc);
    auto p2 = compile_or_die_cam01(desc);

    CameraSession s1;
    CameraSession s2;
    auto cam1 = eval_at_or_die_cam01(p1, s1, Frame{0});
    auto cam2 = eval_at_or_die_cam01(p2, s2, Frame{0});

    // Same descriptor → same final rotation to 1e-9 (no drift across compiles
    // or between the two CameraSessions that semantically should be equivalent
    // for the StaticCameraSource + LookAtPoint path — there is no state held
    // in a session for this descriptor since camera is time-independent).
    CAPTURE(cam1.rotation.x); CAPTURE(cam1.rotation.y); CAPTURE(cam1.rotation.z);
    CAPTURE(cam2.rotation.x); CAPTURE(cam2.rotation.y); CAPTURE(cam2.rotation.z);
    CHECK(cam1.rotation.x == doctest::Approx(cam2.rotation.x).epsilon(1e-9));
    CHECK(cam1.rotation.y == doctest::Approx(cam2.rotation.y).epsilon(1e-9));
    CHECK(cam1.rotation.z == doctest::Approx(cam2.rotation.z).epsilon(1e-9));
}

TEST_CASE("compiled_orientation_look_at_canonical_rotation_computation — "
          "DOC 02 / TICKET-022: with LookAtPoint orientation + a HandheldNoise "
          "modifier, the final rotation equals the canonical computation "
          "`quat_to_camera_euler(quat_look_along(unit(target - (base.position + "
          "wiggle3D(t, freq, amp, seed))), 0)`.  This verifies the math is "
          "consistent with computing the look-at from the MODIFIED position.  "
          "[NOTE: it does NOT by itself catch a regression that re-introduces "
          "the now-stripped pre-modifier orientation call, because the second "
          "application (last-call-wins) is also at the modified position.  "
          "Single-application is enforced by the simplicty of the fix itself "
          "and by code review of the source evaluator bodies — see the closing "
          "block-comment in camera_program.cpp.]") {
    auto desc = make_cam01_base_desc("test.t022.canon");
    desc.base.position = Vec3{0.0f, 0.0f, -1000.0f};
    desc.base.rotation = Vec3{0.0f, 0.0f, 0.0f};
    desc.orientation = LookAtPoint{Vec3{100.0f, 100.0f, 1500.0f}};
    desc.source = StaticCameraSource{};

    // HandheldNoise modifier — forces a non-trivial wiggle3D offset on
    // position.  This means a single-source-position look-at would compute
    // a DIFFERENT rotation than a modified-position look-at.  The fix
    // canonicalises the latter path.
    HandheldNoise hh;
    hh.position_amplitude = Vec3{2.0f, 1.0f, 0.5f};
    hh.position_freq_hz   = 4.0;
    hh.rotation_amplitude_deg = Vec3{0.5f, 0.3f, 0.2f};
    hh.rotation_freq_hz   = 3.0;
    hh.zoom_amplitude     = 0.0f;
    hh.seed               = 42u;
    desc.modifiers.push_back(hh);

    auto program = compile_or_die_cam01(desc);
    CameraSession session;
    auto cam = eval_at_or_die_cam01(program, session, Frame{0});

    // Independently compute the expected rotation via the canonical order:
    //   1) source position from descriptor.base
    //   2) wiggle offset at t=0 (sample_time = 0 seconds for Frame{0} @ 60fps)
    //   3) compose modified_position = source + wiggle
    //   4) look_dir = target - modified_position
    //   5) expected quaternion = quat_look_along(unit(look_dir))
    //   6) expected camera euler = quat_to_camera_euler(expected, src.rotation.z)
    const float kHz = 4.0f;
    const Vec3 kAmp{2.0f, 1.0f, 0.5f};
    const std::uint32_t kSeed = 42u;
    const Vec3 expected_offset =
        wiggle3D(kHz, kAmp, /*t_sec=*/0.0f, kSeed);
    const Vec3 expected_modified =
        desc.base.position + expected_offset;

    const Vec3 expected_look_dir_unnorm =
        Vec3{100.0f, 100.0f, 1500.0f} - expected_modified;
    const float expected_len = std::sqrt(
        expected_look_dir_unnorm.x * expected_look_dir_unnorm.x +
        expected_look_dir_unnorm.y * expected_look_dir_unnorm.y +
        expected_look_dir_unnorm.z * expected_look_dir_unnorm.z);
    const Vec3 expected_look_dir =
        expected_look_dir_unnorm / expected_len;

    const Quat expected_quat = quat_look_along(expected_look_dir);
    const Vec3 expected_rot_euler =
        quat_to_camera_euler(expected_quat, /*preserved_roll=*/0.0f);

    CAPTURE(cam.rotation.x); CAPTURE(cam.rotation.y); CAPTURE(cam.rotation.z);
    CAPTURE(expected_rot_euler.x); CAPTURE(expected_rot_euler.y); CAPTURE(expected_rot_euler.z);
    CAPTURE(cam.position.x); CAPTURE(cam.position.y); CAPTURE(cam.position.z);
    // Tolerance widened to 0.05° — wiggle3D output near a gimbal-lock axis can
    // amplify float drift in quat_to_camera_euler even when both sides share the
    // same wiggle3D call.
    CHECK(cam.rotation.x == doctest::Approx(expected_rot_euler.x).epsilon(0.05f));
    CHECK(cam.rotation.y == doctest::Approx(expected_rot_euler.y).epsilon(0.05f));
    CHECK(cam.rotation.z == doctest::Approx(expected_rot_euler.z).epsilon(0.05f));

    // Belt-and-braces: cam.position is post-mods (source + wiggle).  Verify
    // so any future regression that re-orders modifier→orientation would
    // visibly pin the wrong position here.
    CHECK(cam.position.x == doctest::Approx(expected_modified.x).epsilon(1e-4f));
    CHECK(cam.position.y == doctest::Approx(expected_modified.y).epsilon(1e-4f));
    CHECK(cam.position.z == doctest::Approx(expected_modified.z).epsilon(1e-4f));
}

TEST_CASE("compiled_orientation_single_look_at_constraint_skipped — "
          "TICKET-022 single-look-at policy: LookAtPoint orientation + "
          "LookAtConstraint → constraint silently skipped, orientation wins "
          "exactly once, Warning diagnostic recorded") {
    auto desc = make_cam01_base_desc("test.t022.single");
    desc.base.position = Vec3{0.0f, 0.0f, -1000.0f};
    desc.orientation   = LookAtPoint{Vec3{500.0f, 0.0f, 0.0f}};  // orientation target A
    desc.source        = StaticCameraSource{};
    // Constraint target B is intentionally DIFFERENT from orientation target A
    // so a regression that didn't skip the constraint would FAIL the rotation
    // / POI assertions below.
    desc.constraints.push_back(LookAtConstraint{Vec3{-500.0f, 0.0f, 0.0f}});

    auto program = compile_or_die_cam01(desc);

    // Manually drive the evaluate() so we can introspect diagnostics.
    CameraSession session;
    CameraEvalContext ctx;
    ctx.frame = Frame{0};
    ctx.sample_time = SampleTime::from_frame_int(Frame{0}, kCam01Fps);
    auto res = program.evaluate(ctx, session);
    REQUIRE(res.has_value());

    // POI MUST be orientation's target (A = (500, 0, 0)), NOT constraint's (B = (-500, 0, 0)).
    CAPTURE(res->camera.point_of_interest.x);
    CAPTURE(res->camera.point_of_interest.y);
    CAPTURE(res->camera.point_of_interest.z);
    CHECK(res->camera.point_of_interest.x == doctest::Approx(500.0f).epsilon(kCam01Eps));
    CHECK(res->camera.point_of_interest.y == doctest::Approx(0.0f).epsilon(kCam01Eps));
    CHECK(res->camera.point_of_interest.z == doctest::Approx(0.0f).epsilon(kCam01Eps));
    CHECK(res->camera.point_of_interest_enabled);

    // Single Warning diagnostic recorded (CAM-03 single-look-at policy message).
    REQUIRE_FALSE(res->diagnostics.empty());
    bool found_single_look_at_msg = false;
    for (const auto& d : res->diagnostics) {
        if (d.severity == CameraProgramDiagnostic::Severity::Warning &&
            d.message.find("single look-at policy") != std::string::npos) {
            found_single_look_at_msg = true;
            break;
        }
    }
    CHECK(found_single_look_at_msg);

    // Rotation sanity: rotation is non-trivial (off-axis look-at).
    const float rot_l2 = std::sqrt(res->camera.rotation.x * res->camera.rotation.x
                                   + res->camera.rotation.y * res->camera.rotation.y
                                   + res->camera.rotation.z * res->camera.rotation.z);
    CHECK(rot_l2 > 0.5f);  // degrees — matches §4 floor.
}

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
        CHECK(cam.position.x == doctest::Approx(0.0f).epsilon(kCam01Eps));
        CHECK(cam.position.y == doctest::Approx(0.0f).epsilon(kCam01Eps));
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
        CHECK(cam.position.y == doctest::Approx(0.0f).epsilon(kCam01Eps));
        CHECK(cam.position.z == doctest::Approx(0.0f).epsilon(kCam01Eps));
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
        CHECK(cam.position.y == doctest::Approx(0.0f).epsilon(kCam01Eps));
        CHECK(cam.position.z == doctest::Approx(0.0f).epsilon(kCam01Eps));
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
        CHECK(cam.position.x == doctest::Approx(0.0f).epsilon(kCam01Eps));
        CHECK(cam.position.y == doctest::Approx(0.0f).epsilon(kCam01Eps));
        CHECK(cam.position.z == doctest::Approx(-500.0f).epsilon(kCam01Eps));
    }
}

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
// §5 — ALL 5 CONSTRAINTS
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("compiled_constraint_look_at — "
          "LookAtConstraint places POI to target and sets poi_enabled") {
    auto desc = make_cam01_base_desc();
    desc.source = StaticCameraSource{};
    desc.constraints.push_back(LookAtConstraint{Vec3{0.0f, 0.0f, 0.0f}});

    auto program = compile_or_die_cam01(desc);
    CameraSession session;
    auto cam = eval_at_or_die_cam01(program, session, Frame{0});
    CHECK(cam.point_of_interest_enabled);
    CHECK(cam.point_of_interest.x == doctest::Approx(0.0f).epsilon(kCam01Eps));
    CHECK(cam.point_of_interest.y == doctest::Approx(0.0f).epsilon(kCam01Eps));
    CHECK(cam.point_of_interest.z == doctest::Approx(0.0f).epsilon(kCam01Eps));
}

TEST_CASE("compiled_constraint_keep_horizon — "
          "KeepHorizonConstraint zeroes the roll (rotation.z)") {
    auto desc = make_cam01_base_desc();
    desc.source = StaticCameraSource{};
    desc.base.rotation = Vec3{0.0f, 0.0f, 17.0f};  // pre-existing roll
    desc.constraints.push_back(KeepHorizonConstraint{});

    auto program = compile_or_die_cam01(desc);
    CameraSession session;
    auto cam = eval_at_or_die_cam01(program, session, Frame{0});
    CHECK(cam.rotation.x == doctest::Approx(0.0f).epsilon(kCam01Eps));
    CHECK(cam.rotation.y == doctest::Approx(0.0f).epsilon(kCam01Eps));
    CHECK(cam.rotation.z == doctest::Approx(0.0f).epsilon(kCam01Eps));
}

TEST_CASE("compiled_constraint_damped_follow_zero_damping_is_identity — "
          "damping=0 keeps both first-eval pass-through AND subsequent EMA "
          "returns aligned with the input position; locks the in-place "
          "damping=0 contract for both branches") {
    auto desc = make_cam01_base_desc();
    PoseTracksSource pts;
    pts.position.key(Frame{0},  Vec3{0.0f, 0.0f, -1500.0f}, Easing::Linear)
               .key(Frame{60}, Vec3{0.0f, 0.0f, -500.0f}, Easing::Linear);
    pts.use_target = false;
    desc.source = pts;
    desc.constraints.push_back(DampedFollowConstraint{0.0f});

    auto program = compile_or_die_cam01(desc);
    CameraSession session;

    // Call 1: has_previous=false → first-eval pass-through (returns in).
    auto cam_first = eval_at_or_die_cam01(program, session, Frame{30});
    CAPTURE(cam_first.position.z);
    CHECK(cam_first.position.z == doctest::Approx(-1000.0f).epsilon(kCam01Eps));

    // Call 2 at the same frame: has_previous=true → EMA path.  With
    // damping=0 the anchor contribution cancels (a*(anchor-in)=0), so
    // cam.position still equals in.position exactly.  Locks both code
    // paths so a future refactor cannot silently break one.
    auto cam_second = eval_at_or_die_cam01(program, session, Frame{30});
    CAPTURE(cam_second.position.z);
    CHECK(cam_second.position.z == doctest::Approx(-1000.0f).epsilon(kCam01Eps));
}

TEST_CASE("compiled_constraint_distance — "
          "DistanceConstraint clamps camera position to [min,max] of POI") {
    auto desc = make_cam01_base_desc();
    desc.source = StaticCameraSource{};
    desc.base.position = Vec3{0.0f, 0.0f, -500.0f};
    desc.base.point_of_interest_enabled = true;
    desc.base.point_of_interest = Vec3{0.0f, 0.0f, 0.0f};
    // Force distance to 500 (below the configured min 1000).
    desc.constraints.push_back(
        DistanceConstraint{/*min_distance=*/1000.0f, /*max_distance=*/5000.0f});

    auto program = compile_or_die_cam01(desc);
    CameraSession session;
    auto cam = eval_at_or_die_cam01(program, session, Frame{0});
    // camera should be moved to a 1000-unit distance from origin.
    Vec3 to_target = cam.position - Vec3{0.0f, 0.0f, 0.0f};
    float d = std::sqrt(to_target.x * to_target.x +
                        to_target.y * to_target.y +
                        to_target.z * to_target.z);
    CAPTURE(d);
    CHECK(std::abs(d - 1000.0f) <= 0.01f);
}

TEST_CASE("compiled_constraint_rotation_limit — "
          "RotationLimitConstraint clamps each axis to configured bounds") {
    auto desc = make_cam01_base_desc();
    desc.source = StaticCameraSource{};
    desc.base.rotation = Vec3{200.0f, -200.0f, 200.0f};  // way over limits
    desc.constraints.push_back(
        RotationLimitConstraint{/*max_pitch=*/30.0f,
                                  /*max_yaw=*/45.0f,
                                  /*max_roll=*/10.0f});

    auto program = compile_or_die_cam01(desc);
    CameraSession session;
    auto cam = eval_at_or_die_cam01(program, session, Frame{0});
    CHECK(cam.rotation.x == doctest::Approx(30.0f).epsilon(kCam01Eps));
    CHECK(cam.rotation.y == doctest::Approx(-45.0f).epsilon(kCam01Eps));
    CHECK(cam.rotation.z == doctest::Approx(10.0f).epsilon(kCam01Eps));
}

// ══════════════════════════════════════════════════════════════════════════
// §6 — FAILURE POLICIES (all 3)
// ══════════════════════════════════════════════════════════════════════════

namespace {

/// Build a descriptor whose DistanceConstraint always fails
/// (POI == position → d < 1e-3 → returns ok=false, reason="distance-zero").
CameraDescriptor make_distance_fail_cam01_desc(CameraFailurePolicy policy) {
    auto desc = make_cam01_base_desc("cam01.distance_fail");
    desc.source = StaticCameraSource{};
    // Position == POI → distance = 0 → constraint fails.
    desc.base.position = Vec3{1.0f, 2.0f, 3.0f};
    desc.base.point_of_interest_enabled = true;
    desc.base.point_of_interest = Vec3{1.0f, 2.0f, 3.0f};
    desc.constraints.push_back(
        DistanceConstraint{/*min_distance=*/10.0f, /*max_distance=*/1000.0f});
    desc.failure_policy = policy;
    return desc;
}

} // namespace

TEST_CASE("compiled_failure_policy_stop — "
          "Stop: first failing constraint halts evaluation with ok=false") {
    auto desc = make_distance_fail_cam01_desc(CameraFailurePolicy::Stop);
    auto program = compile_or_die_cam01(desc);
    CameraSession session;

    CameraEvalContext ctx;
    ctx.frame = Frame{0};
    ctx.sample_time = SampleTime::from_frame_int(Frame{0}, kCam01Fps);
    auto res = program.evaluate(ctx, session);
    CHECK_FALSE(res.has_value());
    CHECK(res.error().message == "distance-zero");
}

TEST_CASE("compiled_failure_policy_skip_failed — "
          "SkipFailedConstraint: failing constraint skipped, evaluation "
          "returns ok=true (since it's the only/last one)") {
    auto desc = make_distance_fail_cam01_desc(CameraFailurePolicy::SkipFailedConstraint);
    auto program = compile_or_die_cam01(desc);
    CameraSession session;

    CameraEvalContext ctx;
    ctx.frame = Frame{0};
    ctx.sample_time = SampleTime::from_frame_int(Frame{0}, kCam01Fps);
    auto res = program.evaluate(ctx, session);
    CHECK(res.has_value());  // failure was skipped, no subsequent constraints
    REQUIRE_FALSE(res->diagnostics.empty());
    CHECK(res->diagnostics.front().message == "distance-zero");
}

TEST_CASE("compiled_failure_policy_keep_last_valid — "
          "NOTE: today this shares Stop's codepath in camera_program.cpp "
          "(both set ok=false + diagnostic).  Kept as a contract-locking "
          "test so future divergence (a true 'last-valid' tracking) is "
          "flagged here instead of slipping through silently.") {
    auto desc = make_distance_fail_cam01_desc(CameraFailurePolicy::KeepLastValidCamera);
    auto program = compile_or_die_cam01(desc);
    CameraSession session;

    CameraEvalContext ctx;
    ctx.frame = Frame{0};
    ctx.sample_time = SampleTime::from_frame_int(Frame{0}, kCam01Fps);
    auto res = program.evaluate(ctx, session);
    CHECK_FALSE(res.has_value());
    CHECK_FALSE(res.error().message.empty());
}

// ══════════════════════════════════════════════════════════════════════════
// §7 — TIME-DEPENDENCE FLAG
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("compiled_is_time_dependent — "
          "conservative flag: static + no modifiers → false, "
          "otherwise true") {
    SUBCASE("static source, no modifiers: false") {
        auto desc = make_cam01_base_desc();
        desc.source = StaticCameraSource{};
        auto program = compile_or_die_cam01(desc);
        CHECK_FALSE(program.is_time_dependent());
    }

    SUBCASE("static source + IdleOscillation: true (modifiers contribute)") {
        auto desc = make_cam01_base_desc();
        desc.source = StaticCameraSource{};
        IdleOscillation idle;
        idle.position_amplitude = Vec3{1.0f, 0.0f, 0.0f};
        desc.modifiers.push_back(idle);
        auto program = compile_or_die_cam01(desc);
        CHECK(program.is_time_dependent());
    }

    SUBCASE("PoseTracksSource: true (non-static source)") {
        auto desc = make_cam01_base_desc();
        PoseTracksSource pts;
        pts.position.key(Frame{0},  Vec3{0.0f, 0.0f, -1000.0f})
                   .key(Frame{90}, Vec3{0.0f, 0.0f, -500.0f});
        desc.source = pts;
        auto program = compile_or_die_cam01(desc);
        CHECK(program.is_time_dependent());
    }

    SUBCASE("OrbitMotion: true (animated yaw)") {
        auto desc = make_cam01_base_desc();
        OrbitMotion orbit;
        orbit.target.set(Vec3{0.0f, 0.0f, 0.0f});
        orbit.yaw.key(Frame{0}, 0.0f).key(Frame{60}, 30.0f);
        desc.source = orbit;
        auto program = compile_or_die_cam01(desc);
        CHECK(program.is_time_dependent());
    }
}

// ══════════════════════════════════════════════════════════════════════════
// §8 — UNCOMPILED PROGRAM CONTRACT
// ══════════════════════════════════════════════════════════════════════════
//
// evaluate() on a non-compiled CameraProgram must report ok=false with a
// diagnostic instead of producing undefined output.  This locks the
// defensive branch documented in camera_program.hpp::evaluate().
TEST_CASE("compiled_uncompiled_evaluate_returns_error — "
          "evaluate() before compile_camera() returns ok=false + Error diagnostic "
          "with a 'compile'-related message AND a zero-value Camera2_5D") {
    CameraProgram program;   // NOT compiled: is_compiled() == false
    REQUIRE_FALSE(program.is_compiled());

    CameraSession session;
    CameraEvalContext ctx;
    ctx.frame = Frame{0};
    ctx.sample_time = SampleTime::from_frame_int(Frame{0}, kCam01Fps);

    auto res = program.evaluate(ctx, session);
    CHECK_FALSE(res.has_value());
    CHECK(res.error().kind == CameraEvaluationError::Kind::Uncompiled);
    // Textual contract: the error message must mention "compile" / "not compiled".
    CHECK(res.error().message.find("compile")
          != std::string::npos);
}

// ══════════════════════════════════════════════════════════════════════════
// §9 — CAM-02: CYCLE DETECTION, DETERMINISTIC FINGERPRINT,
//              CameraEvaluationDependency METADATA
// ══════════════════════════════════════════════════════════════════════════
//
// These tests exercise the compile_camera() / compute_camera_descriptor_fingerprint
// / CameraProgram::evaluation_dependency() surface added in CAM-02.
//

// ── §9 helpers ──────────────────────────────────────────────────────────
//
// Build two CameraDescriptors whose RegisteredMotionRef chain forms a cycle:
//   preset.A → "preset.B" → "preset.A"
// Tests the cycle-detection path through the 3-arg compile_camera() with a
// shared CameraCompileContext.
namespace {

NamedCameraPreset make_circular_AB_preset_a() {
    CameraDescriptor d;
    d.id = "preset.A";
    d.base.enabled = true;
    d.base.projection = ZoomProjection{AnimatedValue<float>{1000.0f}};
    d.source = RegisteredMotionRef{"preset.B"};
    d.orientation = FixedOrientation{};
    return NamedCameraPreset{"preset.A", "test", "circular A back-edge",
                              std::move(d)};
}

NamedCameraPreset make_circular_AB_preset_b() {
    CameraDescriptor d;
    d.id = "preset.B";
    d.base.enabled = true;
    d.base.projection = ZoomProjection{AnimatedValue<float>{1000.0f}};
    d.source = RegisteredMotionRef{"preset.A"};
    d.orientation = FixedOrientation{};
    return NamedCameraPreset{"preset.B", "test", "circular B back-edge",
                              std::move(d)};
}

NamedCameraPreset make_self_loop_preset() {
    CameraDescriptor d;
    d.id = "preset.SELF";
    d.base.enabled = true;
    d.base.projection = ZoomProjection{AnimatedValue<float>{1000.0f}};
    d.source = RegisteredMotionRef{"preset.SELF"};
    d.orientation = FixedOrientation{};
    return NamedCameraPreset{"preset.SELF", "test", "self-loop preset",
                              std::move(d)};
}

} // namespace

// ── §9.1 Cycle detection ─────────────────────────────────────────────────
TEST_CASE("compiled_cycle_detection_mutual_a_b — "
          "two presets back-to-back trigger CircularCatalogReference") {
    NamedCameraPreset presets[] = {
        make_circular_AB_preset_a(),
        make_circular_AB_preset_b(),
    };
    CameraCatalog catalog(presets);

    CameraDescriptor top;
    top.id = "test.cycle_outer";
    top.base.enabled = true;
    top.base.projection = ZoomProjection{AnimatedValue<float>{1000.0f}};
    top.source = RegisteredMotionRef{"preset.A"};
    top.orientation = FixedOrientation{};

    auto result = compile_camera(top, &catalog);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().kind
          == CameraCompileError::Kind::CircularCatalogReference);
    // Diagnostic message must name at least one of the cyclic ids so the
    // operator can identify which chain is broken.
    CHECK(result.error().message.find("preset.A") != std::string::npos);
}

TEST_CASE("compiled_cycle_detection_self_loop — "
          "a preset that resolves to itself triggers CircularCatalogReference") {
    NamedCameraPreset presets[] = {make_self_loop_preset()};
    CameraCatalog catalog(presets);

    CameraDescriptor top;
    top.id = "test.cycle_self";
    top.base.enabled = true;
    top.base.projection = ZoomProjection{AnimatedValue<float>{1000.0f}};
    top.source = RegisteredMotionRef{"preset.SELF"};
    top.orientation = FixedOrientation{};

    auto result = compile_camera(top, &catalog);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().kind
          == CameraCompileError::Kind::CircularCatalogReference);
}

// ── §9.2 Deterministic fingerprint ───────────────────────────────────────
TEST_CASE("compiled_fingerprint_identical_content_equal — "
          "two descriptors with identical content hash to same fingerprint") {
    CameraDescriptor desc1 = make_cam01_base_desc("test.fp_id_a");
    // CameraSourceSpec is std::variant; assign the named struct FIRST into
    // a local so we can drive its keyframes, then promote the local into the
    // descriptor.  Writing `desc1.source.position.key(...)` is a class of
    // pre-existing test-file bug (variant has no `position` member).
    PoseTracksSource pts_a;
    pts_a.position.key(Frame{0},  Vec3{0.0f, 0.0f, -1500.0f})
               .key(Frame{90}, Vec3{0.0f, 0.0f, -500.0f});
    desc1.source = pts_a;
    desc1.constraints.push_back(DampedFollowConstraint{0.3f});
    desc1.orientation = LookAtPoint{Vec3{1.0f, 0.0f, 0.0f}};

    CameraDescriptor desc2 = desc1;   // structural copy (same fields).
    auto fp1 = compute_camera_descriptor_fingerprint(desc1);
    auto fp2 = compute_camera_descriptor_fingerprint(desc2);
    CAPTURE(fp1);
    CAPTURE(fp2);
    CHECK(fp1 == fp2);

    // Tweak orientation; fingerprint MUST differ.
    desc2.orientation = LookAtPoint{Vec3{-1.0f, 0.0f, 0.0f}};
    auto fp3 = compute_camera_descriptor_fingerprint(desc2);
    CHECK(fp1 != fp3);
}

TEST_CASE("compiled_fingerprint_excludes_trajectory_pointer_identity — "
          "two CameraTrajectory instances with identical content hash equal") {
    // Two shared_ptrs to different heap allocations of CameraTrajectory
    // with the same point/segment content.  Pointer identity differs but
    // the fingerprint MUST be equal because the user's spec explicitly
    // excludes addresses/pointers from the hash.
    auto traj_a = CameraTrajectoryBuilder()
                      .move_to(Vec3{0.0f, 0.0f, -1500.0f})
                      .bezier_to(Vec3{0.0f, 0.0f, 0.0f},
                                 Vec3{0.0f, 0.0f, 0.0f},
                                 Vec3{0.0f, 0.0f, -500.0f})
                      .duration_frames(90.0f)
                      .build();
    auto traj_b = CameraTrajectoryBuilder()
                      .move_to(Vec3{0.0f, 0.0f, -1500.0f})
                      .bezier_to(Vec3{0.0f, 0.0f, 0.0f},
                                 Vec3{0.0f, 0.0f, 0.0f},
                                 Vec3{0.0f, 0.0f, -500.0f})
                      .duration_frames(90.0f)
                      .build();
    REQUIRE(traj_a);
    REQUIRE(traj_b);
    REQUIRE(traj_a.get() != traj_b.get());  // pointer identity differs.

    CameraDescriptor d1 = make_cam01_base_desc("test.fp_traj");
    d1.source = TrajectoryMotion{traj_a, /*use_arc_length=*/false};
    CameraDescriptor d2 = make_cam01_base_desc("test.fp_traj");
    d2.source = TrajectoryMotion{traj_b, /*use_arc_length=*/false};

    auto fp1 = compute_camera_descriptor_fingerprint(d1);
    auto fp2 = compute_camera_descriptor_fingerprint(d2);
    CAPTURE(fp1);
    CAPTURE(fp2);
    CHECK(fp1 == fp2);

    // But differing trajectory content must produce a different hash.
    auto traj_c = CameraTrajectoryBuilder()
                      .move_to(Vec3{0.0f, 0.0f, -2000.0f})   // different start
                      .bezier_to(Vec3{0.0f, 0.0f, 0.0f},
                                 Vec3{0.0f, 0.0f, 0.0f},
                                 Vec3{0.0f, 0.0f, -1000.0f})  // different end
                      .duration_frames(90.0f)
                      .build();
    CameraDescriptor d3 = make_cam01_base_desc("test.fp_traj");
    d3.source = TrajectoryMotion{traj_c, /*use_arc_length=*/false};
    auto fp3 = compute_camera_descriptor_fingerprint(d3);
    CHECK(fp1 != fp3);
}

// ── §9.3 CameraEvaluationDependency metadata ──────────────────────────────
TEST_CASE("compiled_dependency_static_no_constraints_is_stateless — "
          "static source + no constraints ⇒ Stateless") {
    auto desc = make_cam01_base_desc("test.dep_static");
    desc.source = StaticCameraSource{};
    auto program = compile_or_die_cam01(desc);
    CHECK(program.evaluation_dependency()
          == CameraEvaluationDependency::Stateless);
}

TEST_CASE("compiled_dependency_damped_follow_is_requires_history — "
          "any DampedFollowConstraint (damping ≥ 0) ⇒ RequiresHistory") {
    SUBCASE("damping = 0.5 (active EMA)") {
        auto desc = make_cam01_base_desc("test.dep_damped_active");
        desc.source = PoseTracksSource{};
        desc.constraints.push_back(DampedFollowConstraint{0.5f});
        auto program = compile_or_die_cam01(desc);
        CHECK(program.evaluation_dependency()
              == CameraEvaluationDependency::RequiresHistory);
    }
    SUBCASE("damping = 0 (pass-through branch is still in evaluate)") {
        auto desc = make_cam01_base_desc("test.dep_damped_zero");
        desc.source = PoseTracksSource{};
        desc.constraints.push_back(DampedFollowConstraint{0.0f});
        auto program = compile_or_die_cam01(desc);
        CHECK(program.evaluation_dependency()
              == CameraEvaluationDependency::RequiresHistory);
    }
}

TEST_CASE("compiled_dependency_other_constraints_are_stateless — "
          "LookAt/KeepHorizon/Distance/RotationLimit ⇒ Stateless") {
    auto desc = make_cam01_base_desc("test.dep_other_constraints");
    desc.source = PoseTracksSource{};
    desc.constraints.push_back(LookAtConstraint{Vec3{0.0f, 0.0f, 0.0f}});
    desc.constraints.push_back(KeepHorizonConstraint{});
    desc.constraints.push_back(
        DistanceConstraint{/*min_distance=*/10.0f, /*max_distance=*/1000.0f});
    desc.constraints.push_back(
        RotationLimitConstraint{/*max_pitch=*/30.0f,
                                 /*max_yaw=*/45.0f,
                                 /*max_roll=*/10.0f});
    auto program = compile_or_die_cam01(desc);
    CHECK(program.evaluation_dependency()
          == CameraEvaluationDependency::Stateless);
}

TEST_CASE("compiled_dependency_idempotent_after_recompile — "
          "compiling the same descriptor twice produces the same dependency") {
    auto desc = make_cam01_base_desc("test.dep_idem");
    desc.source = StaticCameraSource{};
    desc.constraints.push_back(DampedFollowConstraint{0.5f});
    auto p1 = compile_or_die_cam01(desc);
    auto p2 = compile_or_die_cam01(desc);
    CHECK(p1.evaluation_dependency() == p2.evaluation_dependency());
    CHECK(p1.evaluation_dependency()
          == CameraEvaluationDependency::RequiresHistory);
}
