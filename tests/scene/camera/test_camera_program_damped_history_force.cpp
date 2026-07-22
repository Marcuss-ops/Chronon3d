// ==============================================================================
// tests/scene/camera/test_camera_program_damped_history_force.cpp
//
// TICKET-A3-DAMPED-HISTORY (Agent3 mission DoD gate (b)) — regression-lock
// that DampedFollowConstraint in `descriptor.constraints` FORCES
// `CameraProgram::evaluation_dependency()` to
// `CameraEvaluationDependency::RequiresHistory`, regardless of any other
// indicator in the descriptor (source variant, modifier list, presence of
// other constraint types).
//
// The force-override is documented in
//   src/scene/camera/camera_v1/camera_program_compiler.cpp
// under the "─ 4. CAM-02 — compute evaluation_dependency metadata ─" block.
// The pattern is default-Stateless then monotonic promotion-on-DampedFollow.
//
// Why a regression lock is necessary: pre-TICKET-A3-DAMPED-HISTORY, the
// classification was woven into a heuristic survey-of-indicators whose
// correctness depended on the dev maintaining the DampedFollow check
// while adding new sources / modifiers. Forgetting the check would
// silently misclassify DampedFollow programs as Stateless, causing the
// camera_v1 session cache to skip the canonical pre-roll walk (see
// `preroll_session_for_frame` in `camera_session_cache.cpp`) and the
// EMA accumulator to start from `has_previous == false` on the very
// first target frame. The downstream symptom is bit-equal deterministic
// render regressions that are hard to attribute because the source
// (program-classification) is far from the symptom (DampedFollow EMA).
//
// SUBCASE breakdown:
//
//   A. only-DampedFollow fixture — DampedFollowConstraint + StaticCameraSource
//      + no modifiers + no other constraints ⇒ RequiresHistory.  This is
//      the canonical reduce-shape for the regression: a Descriptor whose
//      everything-else indicator is unambiguously Stateless MUST still
//      classify RequiresHistory because of the DampedFollowConstraint.
//
//   B. paired Stateless control — same fixture minus DampedFollowConstraint
//      (empty constraints) ⇒ Stateless.  Pairs the force-override with
//      its inverse to ensure the force is genuinely triggered by the
//      DampedFollow variant and not a side-effect of some other field.
//
//   C. DampedFollow in non-zero position in constraints[] —
//      constraints = [Distance, LookAt, KeepHorizon, DampedFollow]
//      ⇒ RequiresHistory.  Locks the "any position in constraints[]"
//      coverage; a regression that inspects only constraints[0] would
//      flip this test to Stateless.
//
//   D. DampedFollow co-existing with multiple constraints plus a
//      non-static source (PoseTracksSource) ⇒ still RequiresHistory.
//      DampedFollow dominates: the rule is monotonic, not exclusive.
//
//   E. multi-DampedFollow: two `DampedFollowConstraint` entries in the
//      same constraints[] vector still classify RequiresHistory.  Locks
//      the "more than one DampedFollow" coverage (future stack support).
// ==============================================================================
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp>

using namespace chronon3d;
using namespace chronon3d::camera_v1;

namespace {

// Static-camera base fixture: defaults to Stateless UNLESS a forcing
// constraint is added.  This is the canonical reduce-shape for the
// regression lock.
CameraDescriptor make_static_base() {
    CameraDescriptor desc;
    desc.id = "force_damped_history_test";
    desc.base.enabled = true;
    desc.base.position = Vec3{0.0f, 0.0f, -1000.0f};
    desc.base.rotation = Vec3{0.0f, 0.0f, 0.0f};
    desc.base.projection = ZoomProjection{AnimatedValue<float>{1000.0f}};
    desc.source = StaticCameraSource{};
    desc.orientation = FixedOrientation{};
    return desc;
}

// Pose-tracks source: a non-static source that, on its own, does NOT
// force RequiresHistory.  (PoseTracksSource is frame-driven; evaluation
// at frame N depends only on (descriptor, ctx.frame()).)  Combined with
// DampedFollowConstraint, the DampedFollow dominates.
CameraDescriptor make_pose_tracks_base() {
    CameraDescriptor desc = make_static_base();
    PoseTracksSource pts;
    pts.position.key(Frame{0}, Vec3{0.0f, 0.0f, -1000.0f});
    pts.use_target = false;
    desc.source = pts;
    return desc;
}

// Compile helper: hides the Result/optional ceremony.
// TICKET-120 followup (Unity build deconflict) — renamed from
// `compile_or_die` to file-scoped unique name to avoid the
// redefinition error in the unified TU built by
// chronon3d_scene_tests (see TICKET-120 build-redefinition group:
// damped_history / lookat_diagnostic / program files all had
// anonymous-namespace `compile_or_die` that collide in Unity build).
CameraProgram compile_or_die_damped_history(const CameraDescriptor& desc) {
    auto cr = compile_camera(desc, /*catalog=*/nullptr);
    REQUIRE(cr.has_value());
    auto prog = std::move(cr).value();
    REQUIRE(prog.is_compiled());
    return prog;
}

} // namespace

TEST_CASE(
    "camera_v1_damped_follow_constraint_forces_requires_history — "
    "TICKET-A3-DAMPED-HISTORY DoD gate (b): the presence of "
    "DampedFollowConstraint in descriptor.constraints forces "
    "CameraProgram::evaluation_dependency() to RequiresHistory even "
    "when the autodetect on the rest of the descriptor would have "
    "produced Stateless. This lock pins the explicit force-override "
    "rule in camera_program_compiler.cpp against future regressions "
    "where the DampedFollow check is silently lost in a heuristic "
    "refactor.") {

    SUBCASE("A. only-DampedFollow fixture (Static + no modifiers + 1 constraint) ⇒ RequiresHistory") {
        CameraDescriptor desc = make_static_base();
        desc.constraints.push_back(DampedFollowConstraint{0.5f});
        CameraProgram prog = compile_or_die_damped_history(desc);
        CHECK(prog.evaluation_dependency() ==
              CameraEvaluationDependency::RequiresHistory);
    }

    SUBCASE("B. paired Stateless control — same fixture minus DampedFollow (empty constraints) ⇒ Stateless") {
        CameraDescriptor desc = make_static_base();
        // desc.constraints is intentionally empty (no DampedFollow).
        REQUIRE(desc.constraints.empty());
        CameraProgram prog = compile_or_die_damped_history(desc);
        CHECK(prog.evaluation_dependency() ==
              CameraEvaluationDependency::Stateless);
    }

    SUBCASE("C. DampedFollow in non-zero position in constraints[] ⇒ still RequiresHistory") {
        CameraDescriptor desc = make_static_base();
        // Lead with three non-DampedFollow constraints; DampedFollow follows.
        desc.constraints.push_back(DistanceConstraint{10.0f, 1000.0f});
        desc.constraints.push_back(LookAtConstraint{Vec3{0.0f, 0.0f, 0.0f}});
        desc.constraints.push_back(KeepHorizonConstraint{});
        desc.constraints.push_back(DampedFollowConstraint{0.15f});
        CameraProgram prog = compile_or_die_damped_history(desc);
        CHECK(prog.evaluation_dependency() ==
              CameraEvaluationDependency::RequiresHistory);
    }

    SUBCASE("D. DampedFollow on top of PoseTracksSource + modifiers + other constraints ⇒ RequiresHistory") {
        CameraDescriptor desc = make_pose_tracks_base();
        desc.modifiers.push_back(HandheldNoise{});  // modifier: still Stateless-by-itself.
        desc.constraints.push_back(DistanceConstraint{10.0f, 1000.0f});
        desc.constraints.push_back(LookAtConstraint{Vec3{0.0f, 0.0f, 0.0f}});
        desc.constraints.push_back(DampedFollowConstraint{0.5f});
        CameraProgram prog = compile_or_die_damped_history(desc);
        CHECK(prog.evaluation_dependency() ==
              CameraEvaluationDependency::RequiresHistory);
    }

    SUBCASE("E. multiple DampedFollow entries in constraints[] ⇒ still RequiresHistory (one-shot force dominates)") {
        CameraDescriptor desc = make_static_base();
        desc.constraints.push_back(DampedFollowConstraint{0.25f});
        desc.constraints.push_back(DampedFollowConstraint{0.75f});
        CameraProgram prog = compile_or_die_damped_history(desc);
        CHECK(prog.evaluation_dependency() ==
              CameraEvaluationDependency::RequiresHistory);
    }
}
