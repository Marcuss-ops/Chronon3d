// test_camera_program_compiled_metadata.cpp — Split from
// test_camera_program_compiled.cpp: RegisteredMotionRef failure policy
// (missing preset), the TICKET-A3-METADATA late-rebuild lock (DoD gate (a))
// and the empty-trajectory compile error.
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
#include <string>
#include <typeinfo>
#include <variant>

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

TEST_CASE("compiled_registered_motion_ref_missing — "
          "RegisteredMotionRef with unknown id returns MotionNotFound") {
    SUBCASE("with valid catalog: preset not found") {
        CameraCatalog catalog(builtin_camera_presets());
        CameraDescriptor desc;
        desc.id = "test.registered_missing";
        desc.source = RegisteredMotionRef{"camera.does_not_exist"};

        auto result = compile_camera(desc, &catalog);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == CameraCompileErrorCode::MissingPreset);
    }

    SUBCASE("without catalog and non-empty id: also MotionNotFound") {
        CameraDescriptor desc;
        desc.id = "test.no_catalog";
        desc.source = RegisteredMotionRef{"anything"};

        auto result = compile_camera(desc, /*catalog=*/nullptr);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == CameraCompileErrorCode::MissingPreset);
    }
}

// ─────────────────────────────────────────────────────────────────────────
// TICKET-A3-METADATA (CAM-03 late-rebuild lock) regression-lock test.
//
// Confirms step 1 (source graft) AND step 4 (evaluation_dependency rebuild)
// of compile_camera() both ran after an outer descriptor resolves a
// RegisteredMotionRef through the catalog.  Locks DoD gate (a) of
// TICKET-A3-METADATA: "RegisteredMotionRef non eredita metadati dal preset
// referenziato" — the preset's DampedFollow-driven RequiresHistory MUST NOT
// leak into the compiled program; the OUTER descriptor's empty constraints
// list wins because step 4 iterates `descriptor.constraints` (the OUTER's).
//
// WHY A LOCK EVEN THOUGH CAM-03 FIX IS ALREADY IN SOURCE: the fix lives
// behind a fall-through from
//   `program.descriptor_ = descriptor;`
//   …resolution step…
//   `program.descriptor_.source = recursive.descriptor_.source;`
// into steps 3-5 (failure_policy_, time_dependent_, evaluation_dependency_).
// A future regression that swaps `auto program = std::move(recursive);`
// (Shape A: program-replaced-with-recursive) would surface as
// `evaluation_dependency() == RequiresHistory` here.  Shape B (early return
// after step 1, skipping steps 3-5) is only observable through private
// fields, not through the public API used here.
//
// DELIBERATE NON-ASSERTION of failure_policy: `program.descriptor()->failure_policy`
// is a flat struct copy of OUTER's descriptor (set by `program.descriptor_ =
// descriptor` BEFORE step 1 mutates only `.source`), so reading through
// `program.descriptor()` returns outer's value unconditionally.  The actual
// program field `program.failure_policy_` is rebuilt by step 3 — but it is
// private.  Behavioural observation would require KeepLastValidCamera ≠ Stop
// in evaluate(); that's TICKET-A3-SESSION's deliverable, not here.
// Similarly, is_time_dependent() is not discriminating for this scenario:
// post-graft source is OrbitMotion in BOTH pre-fix and post-fix paths;
// outer has no modifiers → has_modifiers=false on both sides.
// ─────────────────────────────────────────────────────────────────────────
TEST_CASE("compiled_registered_motion_ref_does_not_inherit_outer_metadata — "
          "TICKET-A3-METADATA (CAM-03 late-rebuild lock) DoD gate (a): "
          "after a RegisteredMotionRef resolves through CameraCatalog, the "
          "OUTER descriptor's evaluation_dependency MUST be recomputed "
          "(step 4) and MUST NOT inherit the preset's DampedFollow-driven "
          "RequiresHistory.  Source graft (step 1) remains the preset's "
          "contribution — only metadata rebuilds from the outer.") {
    // ── PRESET — DELIBERATELY divergent metadata. ──────────────────────
    // Force every knob that the late-rebuild step is supposed to
    // overwrite so that any failure to rebuild would leak one of these
    // values into the OUTER's program.
    CameraDescriptor preset_desc;
    preset_desc.id = "preset.metadata_sentinel";
    preset_desc.base.enabled = true;
    preset_desc.base.projection = ZoomProjection{AnimatedValue<float>{1000.0f}};
    OrbitMotion preset_orbit;
    preset_orbit.target.set(Vec3{0.0f, 0.0f, 0.0f});
    preset_orbit.yaw.set(45.0f);
    preset_orbit.pitch.set(0.0f);
    preset_orbit.radius.set(1000.0f);
    preset_orbit.track.set(Vec3{0.0f, 0.0f, 0.0f});
    preset_orbit.dolly.set(0.0f);
    preset_orbit.roll.set(0.0f);
    preset_desc.source = preset_orbit;
    preset_desc.orientation = FixedOrientation{};
    // → preset has DampedFollow → step 4 of recursive compile marks the
    //   preset program requires_history = true.  Outer has NO constraints
    //   → step 4 of the outer compile marks outer program requires_history
    //   = false.  The lock here is that the OUTER's result wins.
    preset_desc.constraints.push_back(DampedFollowConstraint{0.5f});

    NamedCameraPreset preset_arr[] = {
        NamedCameraPreset{
            "preset.metadata_sentinel", "test",
            "preset with deliberately-contradictory metadata "
            "(animated source + DampedFollow; outer's constraints are empty)",
            std::move(preset_desc)},
    };
    // Array-to-span decay — matches the existing convention in
    // `compiled_cycle_detection_*` (this file).  Avoids an explicit
    // `std::span<...>(...)` constructor which would require `<span>` to be
    // visible to the test file.
    CameraCatalog catalog(preset_arr);

    // ── OUTER — DELIBERATELY empty (no constraints, no modifiers). ────
    CameraDescriptor outer;
    outer.id = "test.outer_metadata_override";
    outer.base.enabled = true;
    outer.base.projection = ZoomProjection{AnimatedValue<float>{1000.0f}};
    outer.source = RegisteredMotionRef{"preset.metadata_sentinel"};
    outer.orientation = FixedOrientation{};

    auto program = compile_or_die_cam01(outer, &catalog);

    // ── (1) Source graft DID run (step 1). ─────────────────────────────
    // Confirms the resolution path completed AND the graft assignment
    // ran; without this the resolution would have failed earlier and
    // we wouldn't be testing rebuild at all.
    const auto* resolved = program.descriptor();
    REQUIRE(resolved != nullptr);
    CAPTURE(typeid(resolved->source).name());
    CHECK(std::holds_alternative<OrbitMotion>(resolved->source));
    CHECK_FALSE(std::holds_alternative<RegisteredMotionRef>(resolved->source));

    // ── (2) evaluation_dependency MUST come from OUTER (step 4 rebuild). ─
    // Outer's constraints list is empty → Stateless.  Without the
    // late-rebuild fix (Shape A: program-replaced-with-recursive) the
    // program would inherit preset's DampedFollow → RequiresHistory.
    // This is the lone disciminating axis reachable through the public
    // API for this scenario (see top block-comment on failure_policy /
    // time_dependent exclusion).
    CAPTURE(program.evaluation_dependency());
    CHECK(program.evaluation_dependency()
          == CameraEvaluationDependency::Stateless);
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
    REQUIRE(result.error().code == CameraCompileErrorCode::InvalidTrajectory);
}
