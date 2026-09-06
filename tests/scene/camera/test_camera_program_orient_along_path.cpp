// test_camera_program_orient_along_path.cpp — Split from
// test_camera_program_compiled.cpp: OrientAlongPath orientation coverage
// (straight line, off-axis, keep_horizon, degenerate-hold fallback,
// static-source no-crash and last-tangent persistence).
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

    // Chronon3D coordinate convention (spatial_bezier_path.hpp:272):
    // left-handed Y-up, forward = -Z.
    // quat_look_along((0,0,1)) orients the camera to look along +Z,
    // which is the OPPOSITE direction from the default -Z forward.
    // This produces a ~180° yaw rotation (rotation.y ≈ 180).
    // The camera position is unchanged (still on the trajectory line).
    CAPTURE(cam.rotation.x); CAPTURE(cam.rotation.y); CAPTURE(cam.rotation.z);
    CHECK(std::abs(cam.rotation.y - 180.0f) < 5.0f);  // ~180° yaw for -Z→+Z flip
    CHECK(std::abs(cam.rotation.x) < 1.0f);            // no pitch change
    CHECK(std::abs(cam.rotation.z) < 1.0f);            // no roll change
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
    ctx = ctx.with_frame(Frame{45}, FrameRate{30, 1});
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
    ctx = ctx.with_frame(Frame{0}, FrameRate{30, 1});
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
