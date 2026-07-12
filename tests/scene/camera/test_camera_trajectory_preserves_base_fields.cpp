// ==============================================================================
// tests/scene/camera/test_camera_trajectory_preserves_base_fields.cpp
//
// CAM-DoD coverage — Isolated supplementary test for the
// "TrajectoryMotion preserves base fields" portion of the agent1 mission
// spec.  This file is a single, dedicated, determinism-focused lock.
//
// Each carry-forward field group has its own TEST_CASE so a future
// regression pinpoints the exact broken field without grepping a 1500-line
// mega-suite.
//
// ── Walked-back sub-spec deviations (all documented for reviewer + future agents) ──
//
// 1. Motion blur spec: the agent1 brief said `motion_blur=Vec2{0.3, 0.7}`.
//    The actual struct is `MotionBlurSettings{mode/samples/shutter_angle_deg/shutter_phase_deg/...}`.
//    The carry-forward invariant is field-by-field equality, so the test
//    uses the real struct directly with values chosen to differ from the
//    type's default-constructed values so accidental carry-default
//    regressions fail loudly.
//
// 2. Lens / projection spec contradiction.  The agent1 brief said
//    `lens=PhysicalLens(35mm primes)` AND `result.camera.lens == base.lens`.
//    With PhysicalLensProjection the canonical apply_projection_spec() path
//    sets `cam.lens = projection.PhysicalLens.lens` — NOT `base.lens`.
//    So the two requirements are MUTUALLY EXCLUSIVE.  Here we deliberately
//    lock `cam.lens == base.lens` (ZoomProjection branch) because the
//    user-spec invariant was the carry-forward one.  The PhysicalLens
//    carry-forward path is locked separately in test_camera_program_compiled.cpp
//    §1.B (`compiled_trajectory_transfers_physical_lens`).
//
// 3. Aperture value: the user wrote "f/2.8".  DepthOfFieldSettings::aperture
//    has no documented unit in this codebase; consumers read it as a
//    project-specific scalar.  The carry-forward invariant here is exact
//    value preservation, not unit/semantics.  We write 2.8f verbatim.
//
// 4. Trajectory mock: `b.move_to(A).bezier_to(B)` was rejected by the
//    compiler's NEW validator (STEP 8 lineage) because the resulting
//    2-point trajectory is parsed differently than the canonical pattern.
//    Switched to `b.move_to(A).move_to(B)` (single Linear segment) which
//    is the pattern the rest of the suite uses.  Same non-zero tangent
//    guarantee (segment direction is `normalize(B-A)`).
//
// ── Field carry-forward that is actually locked here ─────────────────────
//
// Source: `apply_projection_spec + evaluate_compiled_source + post-modifier block`
// of `src/scene/camera/camera_v1/camera_program.cpp`:
//   ✓ lens            : from base.lens (ZoomProjection path) at cpp:802
//   ✓ dof             : from base.dof at cpp:468 (NOT overwritten at:787-800)
//   ✓ motion_blur     : from base.motion_blur at cpp:808 (re-applied in
//                       post-modifier block)
//   ✓ parent_name     : from base.parent_name at cpp:470
//   ✓ point_of_interest / point_of_interest_enabled :
//                       cpp:474-477 — trajectory always forces
//                       point_of_interest_enabled=true AND copies
//                       base.point_of_interest when t.target is nullopt
//                       (our trajectory does not set per-point targets).
// ==============================================================================
#include <doctest/doctest.h>

#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp>
#include <chronon3d/internal/scene/camera/v1/camera_session.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_trajectory.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/model/camera/lens_model.hpp>

#include <memory>
#include <string>

using namespace chronon3d;
using namespace chronon3d::camera_v1;

namespace {

constexpr float kEpsTrajPreserves = 1e-5f;
constexpr FrameRate kFpsTrajPreserves{60, 1};

// Build a non-trivial trajectory: 2 points, single Linear segment.
// At any sampled frame the position lies between P0 and P1 (so
// pos != base.position) AND the segment direction is non-zero, so
// tangent != Vec3(0,0,0).  Pattern matches the demos in test_camera_trajectory.cpp
// (e.g. `make_linear(from, to)`).
std::shared_ptr<CameraTrajectory> make_test_trajectory() {
    CameraTrajectoryBuilder b;
    b.move_to(Vec3{0.0f, 0.0f, -1500.0f})
     .move_to(Vec3{100.0f, 50.0f, -800.0f})
     .duration_frames(90.0f);
    return b.build();
}

// Build a CameraDescriptor with EVERY carry-forward field populated to a
// non-default sentinel value.  Compiler is invoked per-test so each
// carry-forward assertion runs against a freshly compiled program.
CameraDescriptor make_sentinel_desc() {
    CameraDescriptor desc;
    desc.id = "test.traj_preserves_base_fields";

    desc.base.enabled = true;
    desc.base.position = Vec3{0.0f, 0.0f, -1000.0f};
    desc.base.rotation = Vec3{0.0f, 0.0f, 0.0f};
    desc.base.parent_name = "shot_main";   // CANARY string

    // 35mm full-frame prime (LensPresets::full_frame_35mm):
    // focal=35 f_stop=1.4 close_focus=300 sensor=36x24 gate=Fill
    desc.base.lens = LensPresets::full_frame_35mm();

    // ZoomProjection → cam.lens == base.lens (the carry-forward path
    // we are locking).  See header note #2 for the spec contradiction.
    desc.base.projection = ZoomProjection{AnimatedValue<float>{1000.0f}};

    // DOF: focus=5000 (5m), aperture=2.8 (user's "f/2.8" verbatim),
    // max_blur=3.0 (3 px).
    desc.base.dof.enabled = true;
    desc.base.dof.focus_distance = 5000.0f;
    desc.base.dof.aperture = 2.8f;
    desc.base.dof.max_blur = 3.0f;
    // focus_z intentionally NOT set — that is a legacy synonym of
    // focus_distance and would only double-cover the same carry-forward
    // check.  focus_distance alone is sufficient and matches the spec.

    // MotionBlurSettings: every field set to a non-default value so a
    // future partial-carry regression fails loud.
    desc.base.motion_blur.mode = MotionBlurMode::TemporalAccumulation;
    desc.base.motion_blur.samples = 7;
    desc.base.motion_blur.shutter_angle_deg = 270.0f;
    desc.base.motion_blur.shutter_phase_deg = 60.0f;

    desc.source = TrajectoryMotion{make_test_trajectory(),
                                   /*use_arc_length=*/true};
    desc.orientation = FixedOrientation{};

    return desc;
}

CameraProgram compile_or_die_traj_preserves(const CameraDescriptor& desc) {
    auto result = compile_camera(desc, /*catalog=*/nullptr);
    if (!result.has_value()) {
        // Silent-on-success diagnostic.  INFO prints only on the next
        // FAIL, keeping green runs uncluttered.  Grep-friendly keys:
        //   "kind=" "compile_camera error message="
        INFO("compile_camera error kind=" << static_cast<int>(result.error().kind)
             << " compile_camera error message=\"" << result.error().message << "\"");
    }
    REQUIRE(result.has_value());
    auto program = std::move(result).value();
    REQUIRE(program.is_compiled());
    return program;
}

Camera2_5D eval_at_or_die_traj_preserves(const CameraProgram& program,
                          CameraSession& session, Frame frame) {
    CameraEvalContext ctx;
    ctx.frame = frame;
    ctx.sample_time = SampleTime::from_frame_int(frame, kFpsTrajPreserves);
    auto res = program.evaluate(ctx, session);
    REQUIRE(res.has_value());
    return res->camera;
}

} // namespace

// ══════════════════════════════════════════════════════════════════════════
// Carry-forward invariants — one TEST_CASE per field group.
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("TrajectoryMotion preserves base.lens (35mm prime carry-forward, "
          "ZoomProjection path)") {
    auto desc = make_sentinel_desc();
    auto program = compile_or_die_traj_preserves(desc);
    CameraSession session;
    auto cam = eval_at_or_die_traj_preserves(program, session, Frame{0});

    CHECK(cam.optics_mode == CameraOpticsMode::Zoom);

    CAPTURE(cam.lens.focal_length);
    CAPTURE(cam.lens.f_stop);
    CAPTURE(cam.lens.close_focus);
    CAPTURE(cam.lens.sensor_width);
    CAPTURE(cam.lens.sensor_height);
    CAPTURE(static_cast<int>(cam.lens.gate_fit));
    CHECK(cam.lens.focal_length  == doctest::Approx(35.0f).epsilon(kEpsTrajPreserves));
    CHECK(cam.lens.f_stop        == doctest::Approx(1.4f).epsilon(kEpsTrajPreserves));
    CHECK(cam.lens.close_focus   == doctest::Approx(300.0f).epsilon(kEpsTrajPreserves));
    CHECK(cam.lens.sensor_width  == doctest::Approx(36.0f).epsilon(kEpsTrajPreserves));
    CHECK(cam.lens.sensor_height == doctest::Approx(24.0f).epsilon(kEpsTrajPreserves));
    CHECK(cam.lens.gate_fit == GateFit::Fill);
}

TEST_CASE("TrajectoryMotion preserves base.dof "
          "(focus=5m, aperture=f/2.8, max_blur=3px)") {
    auto desc = make_sentinel_desc();
    auto program = compile_or_die_traj_preserves(desc);
    CameraSession session;
    auto cam = eval_at_or_die_traj_preserves(program, session, Frame{0});

    CAPTURE(cam.dof.enabled);
    CAPTURE(cam.dof.focus_distance);
    CAPTURE(cam.dof.aperture);
    CAPTURE(cam.dof.max_blur);
    CHECK(cam.dof.enabled);
    CHECK(cam.dof.focus_distance == doctest::Approx(5000.0f).epsilon(kEpsTrajPreserves));
    CHECK(cam.dof.aperture       == doctest::Approx(2.8f).epsilon(kEpsTrajPreserves));
    CHECK(cam.dof.max_blur       == doctest::Approx(3.0f).epsilon(kEpsTrajPreserves));
}

TEST_CASE("TrajectoryMotion preserves base.motion_blur "
          "(TemporalAccumulation carry-forward)") {
    auto desc = make_sentinel_desc();
    auto program = compile_or_die_traj_preserves(desc);
    CameraSession session;
    auto cam = eval_at_or_die_traj_preserves(program, session, Frame{0});

    CAPTURE(static_cast<int>(cam.motion_blur.mode));
    CAPTURE(cam.motion_blur.samples);
    CAPTURE(cam.motion_blur.shutter_angle_deg);
    CAPTURE(cam.motion_blur.shutter_phase_deg);
    CHECK(cam.motion_blur.mode              == MotionBlurMode::TemporalAccumulation);
    CHECK(cam.motion_blur.samples           == 7);
    CHECK(cam.motion_blur.shutter_angle_deg == doctest::Approx(270.0f).epsilon(kEpsTrajPreserves));
    CHECK(cam.motion_blur.shutter_phase_deg == doctest::Approx(60.0f).epsilon(kEpsTrajPreserves));
}

TEST_CASE("TrajectoryMotion preserves base.parent_name ('shot_main')") {
    auto desc = make_sentinel_desc();
    auto program = compile_or_die_traj_preserves(desc);
    CameraSession session;
    auto cam = eval_at_or_die_traj_preserves(program, session, Frame{0});

    CAPTURE(cam.parent_name.c_str());
    CHECK(cam.parent_name == "shot_main");
}

// POI / point_of_interest_enabled carry-forward: this is a *different*
// invariant from the base-field tests above — it locks a TRAJECTORY BEHAVIOR
// invariant (the trajectory branch forces point_of_interest_enabled=true
// regardless of base.point_of_interest_enabled, and copies
// base.point_of_interest when t.target is nullopt).  The user spec did not
// ask for this but a future regression on the trajectory branch would
// silently pass our other tests.
TEST_CASE("TrajectoryMotion point_of_interest carry-forward (forced-enabled "
          "+ base-default when trajectory has no per-point target)") {
    auto desc = make_sentinel_desc();
    auto program = compile_or_die_traj_preserves(desc);
    CameraSession session;
    auto cam = eval_at_or_die_traj_preserves(program, session, Frame{0});

    // The trajectory branch in cpp:474-477 ALWAYS sets
    // cam.point_of_interest_enabled = true (regardless of
    // desc.base.point_of_interest_enabled, which we leave at its default
    // CameraBaseSpec default = false).  This is a TRAJECTORY semantic,
    // not a base carry-forward, but we lock it to catch drift.
    CHECK(cam.point_of_interest_enabled);

    // No per-point target set on the trajectory, so cpp:475 runs:
    //     cam.point_of_interest = base.point_of_interest;
    // base.point_of_interest default is Vec3{0,0,0}.
    CAPTURE(cam.point_of_interest.x);
    CAPTURE(cam.point_of_interest.y);
    CAPTURE(cam.point_of_interest.z);
    CHECK(cam.point_of_interest.x == doctest::Approx(0.0f).epsilon(kEpsTrajPreserves));
    CHECK(cam.point_of_interest.y == doctest::Approx(0.0f).epsilon(kEpsTrajPreserves));
    CHECK(cam.point_of_interest.z == doctest::Approx(0.0f).epsilon(kEpsTrajPreserves));
}

TEST_CASE("TrajectoryMotion applies trajectory.position (sanity guard — "
          "proves the trajectory source is actually consulted, not statically "
          "replaced by base.position)") {
    auto desc = make_sentinel_desc();
    auto program = compile_or_die_traj_preserves(desc);
    CameraSession session;
    // Mid-segment (Frame{45} of a 90-frame piece) so position is well
    // away from base.position and from both P0 and P1.
    auto cam = eval_at_or_die_traj_preserves(program, session, Frame{45});

    // base.position = (0,0,-1000).  Expected midpoint z = (-1500 + -800)/2
    // = -1150.  ±5 px slack accommodates the linear-segment numerical drift.
    CAPTURE(cam.position.x);
    CAPTURE(cam.position.y);
    CAPTURE(cam.position.z);
    // Mid-segment position differs from base.position (sanity guard —
    // proves the trajectory source is actually consulted, not statically
    // replaced by base.position).
    CHECK(std::abs(cam.position.x - 0.0f) > 10.0f);   // != base 0, tolerance
    CHECK(std::abs(cam.position.z - (-1000.0f)) > 10.0f);
    CHECK(std::abs(cam.position.x - 50.0f) <= 5.0f);
    CHECK(std::abs(cam.position.y - 25.0f) <= 5.0f);
    CHECK(std::abs(cam.position.z - (-1150.0f)) <= 5.0f);
    CHECK(cam.is_animated);
}
