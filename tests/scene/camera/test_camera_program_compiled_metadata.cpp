// test_camera_program_compiled_metadata.cpp — trajectory validation for the
// compiled CameraProgram path.
#include <doctest/doctest.h>

#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/animation/easing/easing.hpp>

#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp>
#include <chronon3d/internal/scene/camera/v1/camera_session.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_trajectory.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/model/camera/lens_model.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor_fingerprint.hpp>
#include <chronon3d/animation/path/spatial_bezier_path.hpp>
#include <chronon3d/animation/effects/wiggle.hpp>

#include <cmath>
#include <memory>
#include <string>
#include <typeinfo>
#include <variant>

using namespace chronon3d;
using namespace chronon3d::camera_v1;

namespace {

constexpr float kCam01Eps = 1e-5f;
constexpr FrameRate kCam01Fps{60, 1};

// Helper: compile + assert success + assert is_compiled().
CameraProgram compile_or_die_cam01(const CameraDescriptor& desc) {
    auto result = compile_camera(desc);
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

    auto result = compile_camera(desc);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == CameraCompileErrorCode::InvalidTrajectory);
}
