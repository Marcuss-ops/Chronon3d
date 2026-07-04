// ==============================================================================
// tests/scene/camera/test_camera_program_metadata_late_rebuild.cpp
//
// TICKET-A3-METADATA (Agent3 mission DoD gate (a)) — regression-lock that
// the outer descriptor's `failure_policy`, `time_dependent` flag, and
// `evaluation_dependency` are LATE-REBUILT from the outer descriptor
// (after step 1 resolves `RegisteredMotionRef` to a concrete source
// variant) and are NOT silently inherited from the referenced preset.
//
// Pre-TICKET-A3-METADATA, the resolved preset's metadata could leak into
// the outer CameraProgram when the outer descriptor used
// `RegisteredMotionRef` as its source. This caused two classes of bugs:
//   1. A `Stop` outer policy was silently downgraded to whatever the
//      preset declared (e.g. `KeepLastValidCamera`) — wrong failure
//      handling for users who overrode the policy at the call site.
//   2. A `DampedFollowConstraint` on the outer was masked by a
//      `Stateless` preset, leading to the camera_v1 session cache
//      skipping the canonical pre-roll walk and the EMA accumulator
//      starting from `has_previous == false` on the first target frame.
//
// The fix (already implemented in `camera_program_compiler.cpp`,
// documented at the CAM-03 fix marker in step 1, with step 3 forcing
// `program.failure_policy_ = descriptor.failure_policy`, step 3 time
// loop recomputing from `program.descriptor_.source` + `descriptor.modifiers`,
// and step 4 evaluation_dependency loop scanning the outer
// `descriptor.constraints`):
//
//   program.failure_policy_         = descriptor.failure_policy  [outer, post-graft]
//   program.time_dependent_         = !source_is_static || has_modifiers  [outer descriptors + resolved source]
//   program.evaluation_dependency_  = survey of descriptor.constraints + DampedFollow override  [outer constraints]
//
// SUBCASE breakdown:
//   A. Resolved-source time_dependent recompute (step 3): outer references
//      PoseTracksSource preset ⇒ resolved source is PoseTracksSource ⇒
//      time_dependent MUST be true.
//   B. DampedFollow force-override (step 4): preset is Stateless, outer
//      has DampedFollowConstraint ⇒ outer is RequiresHistory.
//   C. One-way inheritance lock: preset has DampedFollow, outer does NOT
//      ⇒ outer is Stateless (no bleeding).
//   D. Source variant extraction (step 1): program.descriptor_->source
//      is the concrete preset variant, NOT the outer RegisteredMotionRef.
//   E. Outer failure_policy preservation (step 3): outer's failure_policy
//      wins over the preset's default Stop (verified via public
//      descriptor() getter — no failure_policy() getter exposed ⇒ AGENTS
//      v0.1 freeze Cat-3 compliance: no API surface expansion).
// ==============================================================================
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/scene/camera/camera_v1/camera_catalog.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp>

#include <memory>
#include <span>
#include <utility>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::camera_v1;

namespace {

// ── Helper: minimal base spec (zoom 1000, position [0,0,-1000]). ──────
CameraBaseSpec make_base_spec() {
    CameraBaseSpec bs;
    bs.enabled = true;
    bs.position = Vec3{0.0f, 0.0f, -1000.0f};
    bs.rotation = Vec3{0.0f, 0.0f, 0.0f};
    bs.projection = ZoomProjection{AnimatedValue<float>{1000.0f}};
    return bs;
}

// ── Helper: build a preset CameraDescriptor with the given source. ──
CameraDescriptor make_preset_desc(
    std::string preset_id,
    CameraSourceSpec inner_source) {
    CameraDescriptor desc;
    desc.id = std::move(preset_id);
    desc.base = make_base_spec();
    desc.source = std::move(inner_source);
    desc.orientation = FixedOrientation{};
    desc.failure_policy = CameraFailurePolicy::Stop;
    return desc;
}

// ── Helper: build a preset with PoseTracksSource + DampedFollow. ────
CameraDescriptor make_preset_pose_damped(std::string preset_id) {
    CameraDescriptor desc;
    desc.id = std::move(preset_id);
    desc.base = make_base_spec();

    PoseTracksSource pts;
    pts.position.key(Frame{0}, Vec3{0.0f, 0.0f, -1000.0f})
               .key(Frame{90}, Vec3{0.0f, 0.0f, -800.0f});
    pts.use_target = false;
    desc.source = pts;

    desc.orientation = FixedOrientation{};
    desc.constraints.push_back(DampedFollowConstraint{0.5f});
    desc.failure_policy = CameraFailurePolicy::Stop;
    return desc;
}

// ── Helper: build a preset with StaticCameraSource + no DampedFollow ─
// ── → preset is Stateless + NOT time-dependent.                    ──
CameraDescriptor make_preset_static_stateless(std::string preset_id) {
    CameraDescriptor desc;
    desc.id = std::move(preset_id);
    desc.base = make_base_spec();
    desc.source = StaticCameraSource{};
    desc.orientation = FixedOrientation{};
    desc.failure_policy = CameraFailurePolicy::Stop;
    return desc;
}

// ── Helper: build an outer descriptor that references a preset id. ──
CameraDescriptor make_outer_ref(
    std::string outer_id,
    std::string preset_id,
    CameraFailurePolicy outer_policy,
    bool outer_has_modifiers,
    bool outer_has_damped_follow) {
    CameraDescriptor desc;
    desc.id = std::move(outer_id);
    desc.base = make_base_spec();
    desc.source = RegisteredMotionRef{preset_id};
    desc.orientation = FixedOrientation{};
    desc.failure_policy = outer_policy;

    if (outer_has_modifiers) {
        desc.modifiers.push_back(HandheldNoise{});
    }

    if (outer_has_damped_follow) {
        desc.constraints.push_back(DampedFollowConstraint{0.5f});
    }
    return desc;
}

// ── Helper: build a CameraCatalog + owner for the id string_views. ──
//
// `CameraCatalog` is immutable post-construction; the public API exposes
// `find` / `find_descriptor` for lookups but no `register_descriptor`
// mutation method. The catalog is built from a `std::span<const
// NamedCameraPreset>`, and each `NamedCameraPreset::id` is a
// `std::string_view` (non-owning) that aliases the backing string.
//
// To avoid dangling `string_view`s in the catalog after the caller's
// `entries` vector is destroyed, the helper returns a small struct
// that owns both the catalog and the backing strings. The caller must
// keep the returned struct alive for as long as the catalog is in use.
//
// This is critical: `CameraCatalog(std::span<const NamedCameraPreset>)`
// COPIES the `NamedCameraPreset` structs (shallow string_view copy) into
// the catalog's internal `presets_`. If the source strings are
// destroyed, `presets_[i].id` dangles and `catalog.find(...)` (called
// later by `compile_camera` for `RegisteredMotionRef` resolution) reads
// UB memory.
struct CatalogWithOwners {
    CameraCatalog catalog;
    std::vector<std::string> id_owners;
};

CatalogWithOwners make_catalog(
    const std::vector<std::pair<std::string, CameraDescriptor>>& entries) {
    CatalogWithOwners result;
    result.id_owners.reserve(entries.size());
    std::vector<NamedCameraPreset> named;
    named.reserve(entries.size());
    for (const auto& [id, desc] : entries) {
        // Own a copy of the id string; the catalog's string_views will
        // alias the entries in `result.id_owners` (which outlive the
        // catalog because the struct owns both).
        result.id_owners.push_back(id);
        NamedCameraPreset p;
        p.id = result.id_owners.back();
        p.category = "test";
        p.description = "test preset";
        p.descriptor = desc;
        named.push_back(std::move(p));
    }
    result.catalog = CameraCatalog(std::span<const NamedCameraPreset>(named));
    return result;
}

// ── Helper: compile-or-die (file-scoped unique per TICKET-120 ──────
// ── Unity-build deconflict convention).                            ──
CameraProgram compile_or_die_metadata_late_rebuild(
    const CameraDescriptor& desc,
    const CameraCatalog* catalog) {
    auto cr = compile_camera(desc, catalog);
    REQUIRE(cr.has_value());
    auto prog = std::move(cr).value();
    REQUIRE(prog.is_compiled());
    return prog;
}

} // namespace

TEST_CASE(
    "camera_v1_late_rebuild_outer_metadata_does_not_inherit_from_preset — "
    "TICKET-A3-METADATA DoD gate (a): compile_camera() must LATE-REBUILD "
    "the outer descriptor's failure_policy, time_dependent flag, and "
    "evaluation_dependency AFTER the RegisteredMotionRef resolution in "
    "step 1 — not silently inherit them from the referenced preset. "
    "This lock pins the CAM-03 fix marker against future regressions "
    "where the inheritance is re-introduced by a heuristic refactor.") {

    SUBCASE("A. resolved source is the resolved PoseTracksSource; time_dependent reflects RESOLVED source, NOT outer (late-rebuild step 3)") {
        auto preset = make_preset_desc(
            "preset.pose",
            []() {
                PoseTracksSource pts;
                pts.position.key(Frame{0}, Vec3{0.0f, 0.0f, -1000.0f})
                           .key(Frame{90}, Vec3{0.0f, 0.0f, -800.0f});
                pts.use_target = false;
                return pts;
            }());
        auto owners = make_catalog({{"preset.pose", preset}});
        auto outer = make_outer_ref(
            "outer.uses_pose_preset",
            "preset.pose",
            CameraFailurePolicy::Stop,
            /*outer_has_modifiers=*/false,
            /*outer_has_damped_follow=*/false);
        CameraProgram prog = compile_or_die_metadata_late_rebuild(outer, &owners.catalog);
        CHECK(prog.is_time_dependent() == true);
        REQUIRE(prog.descriptor() != nullptr);
        CHECK(std::holds_alternative<PoseTracksSource>(prog.descriptor()->source));
        CHECK(prog.evaluation_dependency() ==
              CameraEvaluationDependency::Stateless);
    }

    SUBCASE("B. preset is Stateless, outer has DampedFollow ⇒ outer is RequiresHistory (late-rebuild step 4)") {
        auto preset = make_preset_static_stateless("preset.static");
        auto owners = make_catalog({{"preset.static", preset}});
        auto outer = make_outer_ref(
            "outer.damped_over_static_preset",
            "preset.static",
            CameraFailurePolicy::Stop,
            /*outer_has_modifiers=*/false,
            /*outer_has_damped_follow=*/true);
        CameraProgram prog = compile_or_die_metadata_late_rebuild(outer, &owners.catalog);
        CHECK(prog.evaluation_dependency() ==
              CameraEvaluationDependency::RequiresHistory);
        REQUIRE(prog.descriptor() != nullptr);
        CHECK(std::holds_alternative<StaticCameraSource>(prog.descriptor()->source));
        CHECK(prog.is_time_dependent() == false);
    }

    SUBCASE("C. preset has DampedFollow, outer does NOT ⇒ outer is Stateless (one-way lock)") {
        auto preset = make_preset_pose_damped("preset.pose_damped");
        auto owners = make_catalog({{"preset.pose_damped", preset}});
        auto outer = make_outer_ref(
            "outer.no_damped",
            "preset.pose_damped",
            CameraFailurePolicy::Stop,
            /*outer_has_modifiers=*/false,
            /*outer_has_damped_follow=*/false);
        CameraProgram prog = compile_or_die_metadata_late_rebuild(outer, &owners.catalog);
        CHECK(prog.evaluation_dependency() ==
              CameraEvaluationDependency::Stateless);
        CHECK(prog.is_time_dependent() == true);
    }

    SUBCASE("D. resolved source is the concrete preset variant, NOT the outer RegisteredMotionRef (step 1)") {
        auto preset = make_preset_desc(
            "preset.for_source_check",
            []() {
                PoseTracksSource pts;
                pts.position.key(Frame{0}, Vec3{0.0f, 0.0f, -1000.0f});
                pts.use_target = false;
                return pts;
            }());
        auto owners = make_catalog({{"preset.for_source_check", preset}});
        auto outer = make_outer_ref(
            "outer.source_check",
            "preset.for_source_check",
            CameraFailurePolicy::Stop,
            /*outer_has_modifiers=*/false,
            /*outer_has_damped_follow=*/false);
        CameraProgram prog = compile_or_die_metadata_late_rebuild(outer, &owners.catalog);
        REQUIRE(prog.descriptor() != nullptr);
        CHECK(std::holds_alternative<PoseTracksSource>(prog.descriptor()->source));
        CHECK_FALSE(std::holds_alternative<RegisteredMotionRef>(prog.descriptor()->source));
        CHECK(prog.descriptor()->id == "outer.source_check");
    }

    SUBCASE("E. outer's failure_policy is preserved (step 3 direct assignment from outer descriptor)") {
        auto preset = make_preset_static_stateless("preset.fp_lock");
        auto owners = make_catalog({{"preset.fp_lock", preset}});
        auto outer1 = make_outer_ref(
            "outer.fp_keep_last",
            "preset.fp_lock",
            CameraFailurePolicy::KeepLastValidCamera,
            /*outer_has_modifiers=*/false,
            /*outer_has_damped_follow=*/false);
        CameraProgram prog1 = compile_or_die_metadata_late_rebuild(outer1, &owners.catalog);
        REQUIRE(prog1.descriptor() != nullptr);
        CHECK(prog1.descriptor()->id == "outer.fp_keep_last");
        CHECK(prog1.descriptor()->failure_policy == CameraFailurePolicy::KeepLastValidCamera);
        auto outer2 = make_outer_ref(
            "outer.fp_skip_failed",
            "preset.fp_lock",
            CameraFailurePolicy::SkipFailedConstraint,
            /*outer_has_modifiers=*/false,
            /*outer_has_damped_follow=*/false);
        CameraProgram prog2 = compile_or_die_metadata_late_rebuild(outer2, &owners.catalog);
        REQUIRE(prog2.descriptor() != nullptr);
        CHECK(prog2.descriptor()->id == "outer.fp_skip_failed");
        CHECK(prog2.descriptor()->failure_policy == CameraFailurePolicy::SkipFailedConstraint);
    }
}
