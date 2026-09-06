// test_camera_program_compiled_trajectory.cpp — Split from
// test_camera_program_compiled.cpp: §1.B TrajectoryMotion full
// CameraBaseSpec transfer (projection spec, physical lens, DOF / motion
// blur / parent_name, animated focus distance) plus trajectory roll_deg
// carry-forward.
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
#include <chronon3d/scene/camera/camera_v1/camera_descriptor_fingerprint.hpp>
#include <chronon3d/animation/path/spatial_bezier_path.hpp>
#include <chronon3d/animation/effects/wiggle.hpp>

#include <cmath>
#include <memory>
#include <optional>
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

TEST_CASE("compiled_dof_animated_focus_distance — "
          "PoseTracksSource + animated focus_distance changes cam.dof.focus_distance "
          "frame over frame (AE parity: pull-focus animation)") {
    auto desc = make_cam01_base_desc("test.dof_animated");
    desc.base.dof.enabled = true;
    desc.base.dof.aperture = 0.02f;
    PoseTracksSource pts;
    pts.position.set(Vec3{0.0f, 0.0f, -1000.0f});
    // Animated focus: 0 → 500 → 1000 over 120 frames (linear)
    pts.focus_distance.key(Frame{0},   0.0f)
                      .key(Frame{60},  500.0f, Easing::Linear)
                      .key(Frame{120}, 1000.0f, Easing::Linear);
    desc.source = pts;

    auto program = compile_or_die_cam01(desc);
    REQUIRE(program.is_time_dependent());

    CameraSession session;
    // Frame 0: focus=0
    auto cam0 = eval_at_or_die_cam01(program, session, Frame{0});
    CHECK(cam0.dof.enabled);
    CHECK(cam0.dof.focus_distance == doctest::Approx(0.0f).epsilon(kCam01Eps));

    // Frame 30: midpoint of 0→500 → focus=250
    auto cam30 = eval_at_or_die_cam01(program, session, Frame{30});
    CHECK(cam30.dof.focus_distance == doctest::Approx(250.0f).epsilon(0.01f));

    // Frame 60: focus=500
    auto cam60 = eval_at_or_die_cam01(program, session, Frame{60});
    CHECK(cam60.dof.focus_distance == doctest::Approx(500.0f).epsilon(kCam01Eps));

    // Frame 90: midpoint of 500→1000 → focus=750
    auto cam90 = eval_at_or_die_cam01(program, session, Frame{90});
    CHECK(cam90.dof.focus_distance == doctest::Approx(750.0f).epsilon(0.01f));

    // Frame 120: focus=1000
    auto cam120 = eval_at_or_die_cam01(program, session, Frame{120});
    CHECK(cam120.dof.focus_distance == doctest::Approx(1000.0f).epsilon(kCam01Eps));

    // Structural guard: focus_distance changes monotonically
    CHECK(cam0.dof.focus_distance < cam30.dof.focus_distance);
    CHECK(cam30.dof.focus_distance < cam60.dof.focus_distance);
    CHECK(cam60.dof.focus_distance < cam90.dof.focus_distance);
    CHECK(cam90.dof.focus_distance < cam120.dof.focus_distance);
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
