// ==============================================================================
// tests/scene/camera/test_camera_program_compiled.cpp
//
// CAM-01 / DOC 04 — compiled camera path: SOURCE VARIANTS (§1).
// Projections/orientation live in test_camera_program_projection_orientation.cpp,
// constraints/failure policies/fingerprint/golden in
// test_camera_program_constraints_policy.cpp.
// ==============================================================================
// ==============================================================================
// tests/scene/camera/test_camera_program_compiled.cpp
//
// CAM-01 / DOC 04 — Baseline test suite for the COMPILED camera path.
//
// Scope (per the retired docs/camera-plan/04-INTEGRATION_TESTS_AND_LEGACY_REMOVAL.md §  // drift-class: historical (camera-plan design docs retired)
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
#include <string>       // std::string, std::string::npos — used by §8 textual contract test
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

// =================================================================
// §1.C GOLDEN — placeholder sentinel + .bin reader + state hasher.
// =================================================================
//
// Patterned after `tests/deterministic/test_baseline_green.cpp`
// (`kUncapturedSentinel = 0xDEADBEEFDEADBEEFULL` + `kRefBaseline*`).
// `0xDEADBEEFDEADBEEF` is distinct from the FNV-1a offset basis
// (`0xCBF29CE484222325`) so the first-run placeholder cannot collide
// with a real hash.  The same sentinel value is written verbatim into
// `tests/scene/camera/_golden/trajectory_lens_dof.golden.bin` at
// initial commit so that:
//   1. The .bin is a single 8-byte file (point-in-time hash snapshot).
//   2. The TEST_CASE can detect "first run, placeholder still pinned":
//      hashes[0] != kUncapturedSentinel ⇒ MESSAGE + capture hint;
//      hashes[0] == kUncapturedSentinel ⇒ REQUIRE exact match.
//   3. After the upstream regressions are fixed on origin/main, running
//      `tools/regen_camera_golden.sh` captures the post-eval hash and
//      writes it to the .bin (overwriting the placeholder).

constexpr std::uint64_t kUncapturedSentinel = 0xDEADBEEFDEADBEEFULL;

// Resolve the canonical .bin path.  This TU lives at
//   <repo>/tests/scene/camera/test_camera_program_compiled.cpp
// and the .bin lives at
//   <repo>/tests/scene/camera/_golden/trajectory_lens_dof.golden.bin
// `__FILE__` is the compiler-provided absolute source path on Linux-ci
// (verified during the Step 5 build pass), so this works regardless of
// ctest's working directory.
inline std::filesystem::path resolve_golden_path() {
    std::filesystem::path src = std::filesystem::absolute(std::filesystem::path{__FILE__});
    return src.parent_path() / "_golden" / "trajectory_lens_dof.golden.bin";
}

// Read the 8-byte little-endian uint64_t from the .bin.  Returns the
// placeholder sentinel if the file is absent or not exactly 8 bytes
// (which preserves "first run, not yet generated" semantics across CI
// runs before the upstream blockers clear).
inline std::uint64_t read_pinned_golden_u64(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return kUncapturedSentinel;
    std::uint64_t v = 0;
    f.read(reinterpret_cast<char*>(&v), sizeof(v));
    if (f.gcount() != static_cast<std::streamsize>(sizeof(v))) {
        return kUncapturedSentinel;
    }
    return v;
}

// FNV-1a 64-bit over the four field groups the Step 5 spec mandates:
//   camera.position, camera.lens (LensModel fields), camera.dof
//   (DOFSettings fields), camera.rotation.  Plus the descriptor id
//   ("name") so that any future rename of the test source surfaces as
//   a hash drift.  Reuses `detail::Fnv1aHasher` from
//   `camera_descriptor_fingerprint.hpp` so the byte-encoding matches
//   every other FNV-1a in the repo (no parallel implementation).
inline std::uint64_t hash_camera_state_for_golden(const Camera2_5D& cam,
                                                  std::string_view name) {
    // Fully qualified: `using namespace chronon3d;` + `using namespace
    // chronon3d::camera_v1;` in this TU both expose a `detail` namespace
    // — `chronon3d::camera_v1::detail` (owning Fnv1aHasher) and the
    // unrelated `chronon3d::detail` (transitively pulled in by
    // `chronon3d/animation/core/animated_value.hpp` via
    // `chronon3d/animation/core/detail/animated_value_expressions.hpp`).
    // Bare `detail::Fnv1aHasher` is ambiguous on this TU; qualify.
    chronon3d::camera_v1::detail::Fnv1aHasher h;
    h.mix_str(name);
    h.mix_bool(cam.enabled);
    h.mix_bool(cam.is_animated);
    h.mix_vec3(cam.position);
    h.mix_vec3(cam.rotation);
    h.mix_u8(static_cast<std::uint8_t>(cam.optics_mode));
    {
        const auto& lens = cam.lens;
        h.mix_f32(lens.focal_length);
        h.mix_f32(lens.f_stop);
        h.mix_f32(lens.close_focus);
        h.mix_f32(lens.sensor_width);
        h.mix_f32(lens.sensor_height);
        h.mix_enum(lens.gate_fit);
        h.mix_f32(lens.pixel_aspect);
        h.mix_f32(lens.anamorphic_squeeze);
    }
    {
        const auto& dof = cam.dof;
        // Match the canonical `compute_camera_descriptor_fingerprint`
        // DOF-section byte layout so any per-eval divergence in the
        // trajectory → DOF carry-forward path would surface as a hash
        // drift against the pinned golden.
        h.mix_bool(dof.enabled);
        h.mix_f32(dof.focus_z);
        h.mix_f32(dof.aperture);
        h.mix_f32(dof.max_blur);
        h.mix_f32(dof.focus_distance);
        h.mix_bool(dof.use_physical_model);
        h.mix_f32(dof.near_bokeh_radius);
        h.mix_f32(dof.far_bokeh_radius);
    }
    return h.h;
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

