// ==============================================================================
// test_camera_program_constraints_policy.cpp — compiled camera path:
// CONSTRAINTS (§5), FAILURE POLICIES (§6), TIME-DEPENDENCE (§7),
// UNCOMPILED CONTRACT (§8), CAM-02 metadata (§9) and GOLDEN lock (§10).
// Split from test_camera_program_compiled.cpp.
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
    ctx = ctx.with_frame(Frame{0}, FrameRate{30, 1});
    ctx.sample_time = SampleTime::from_frame_int(Frame{0}, kCam01Fps);
    auto res = program.evaluate(ctx, session);
    CHECK_FALSE(res.has_value());
    CHECK(res.error().message.find("distance-zero") != std::string::npos);
}

TEST_CASE("compiled_failure_policy_skip_failed — "
          "SkipFailedConstraint: failing constraint skipped, evaluation "
          "returns ok=true (since it's the only/last one)") {
    auto desc = make_distance_fail_cam01_desc(CameraFailurePolicy::SkipFailedConstraint);
    auto program = compile_or_die_cam01(desc);
    CameraSession session;

    CameraEvalContext ctx;
    ctx = ctx.with_frame(Frame{0}, FrameRate{30, 1});
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
    ctx = ctx.with_frame(Frame{0}, FrameRate{30, 1});
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
    ctx = ctx.with_frame(Frame{0}, FrameRate{30, 1});
    ctx.sample_time = SampleTime::from_frame_int(Frame{0}, kCam01Fps);

    auto res = program.evaluate(ctx, session);
    CHECK_FALSE(res.has_value());
    CHECK(res.error().code == CameraErrorCode::Uncompiled);
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
    CHECK(result.error().code
          == CameraCompileErrorCode::CircularPresetReference);
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
    CHECK(result.error().code
          == CameraCompileErrorCode::CircularPresetReference);
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

// ══════════════════════════════════════════════════════════════════════════════
// §10 — GOLDEN regression lock: TrajectoryMotion + PhysicalLens + DOF
// ══════════════════════════════════════════════════════════════════════════════
//
// Latches the post-evaluate camera state of a fully-formed descriptor into
// the (camera.position, camera.lens, camera.dof, camera.rotation) field
// groups by FNV-1a hash + a binary snapshot at
//   tests/scene/camera/_golden/trajectory_lens_dof.golden.bin
//
// Three sub-checks per the Step 5 spec:
//   (b) DETERMINISM: 5 consecutive identical evaluations of program.evaluate()
//       under the SAME ctx + CameraSession MUST yield identical hashes (no
//       hidden cross-frame state, no per-call RNG drift).  This is the
//       base guarantee for any further regression lock.
//   (c) GOLDEN: the first hash MUST equal the pinned uint64_t in the .bin
//       file.  If the .bin is the placeholder sentinel, the test prints
//       a MESSAGE() with the first valid hash + an instruction to run
//       tools/regen_camera_golden.sh once both upstream blockers clear.
//
// Pre-existing baseline regressions on HEAD=3a5eb193 (NOT introduced by
// this file) block the compile step at the same point as Step 3/4 work:
//   • src/scene/camera/camera_v1/camera_program_compiler.cpp:330-335 uses
//     trajectory->size() (segment count) where it should use
//     trajectory->points().size() (point count).  Every 2-point/1-segment
//     trajectory's compile_camera() rejects with kind=19 /
//     "trajectory segment[0] has invalid indices: from_idx=0 to_idx=1
//     (trajectory has 1 points)".
//   • tests/scene/camera/golden_projection_test.cpp refers to 4
//     `FocalPx` identifiers that are no longer types in this codebase;
//     the scene-tests executable fails to link.
//
// The test file therefore fails (compile error) on the current HEAD.
// The test structure is intact and will flip green once those two
// upstream regressions are fixed.
TEST_CASE("compiled_trajectory_lens_dof_golden — "
          "GOLDEN regression: TrajectoryMotion + PhysicalLensProjection + DOF — "
          "5× identical eval ⇒ FNV-1a(camera) matches pinned uint64_t "
          "in tests/scene/camera/_golden/trajectory_lens_dof.golden.bin") {
    // (a) Fixed CameraDescriptor.  desc.id is part of the hash (acts as the
    //     "name" canary) — every regenerate MUST keep this exact string,
    //     otherwise the hash drifts and the golden must be re-pinned.
    CameraDescriptor desc;
    desc.id = "golden.trajectory_lens_dof";
    desc.base.enabled = true;

    // PhysicalLens projection: full-frame 85mm with a sentinel f_stop (5.6f,
    // distinct from the LensPresets::full_frame_85mm default of 1.4f) so
    // any non-canonical lens-source switch would surface as a hash drift.
    LensModel lens = LensPresets::full_frame_85mm();
    lens.f_stop = 5.6f;
    desc.base.projection = PhysicalLensProjection{lens};

    // DOF enabled with a physical-model setup so the carry-forward contract
    // is non-trivial.  ALL three values must propagate to cam.dof exactly;
    // any silent revert to defaults would shift the hash.
    desc.base.dof.enabled = true;
    desc.base.dof.use_physical_model = true;
    desc.base.dof.focus_distance = 1500.0f;
    desc.base.dof.aperture       = 0.020f;
    desc.base.dof.max_blur       = 12.0f;

    // 2-point, 1-segment trajectory (bezier with zero handles reduces to a
    // straight line) so position, tangent, and frame-range are unambiguous.
    auto traj = CameraTrajectoryBuilder()
                    .move_to(Vec3{0.0f, 0.0f, -1500.0f})
                    .bezier_to(Vec3{0.0f, 0.0f, 0.0f},
                               Vec3{0.0f, 0.0f, 0.0f},
                               Vec3{0.0f, 0.0f, -500.0f})
                    .duration_frames(90.0f)
                    .build();
    REQUIRE(traj);
    desc.source      = TrajectoryMotion{traj, /*use_arc_length=*/true};
    desc.orientation = OrientAlongPath{/*keep_horizon=*/false};

    // WILL FAIL at compile_camera() on HEAD=3a5eb193 due to the upstream
    // trajectory->size() validator regression.  When unblocked, this is
    // the canonical compile step the golden is pinned against.
    auto program = compile_or_die_cam01(desc);

    CameraSession session;
    CameraEvalContext ctx;
    ctx = ctx.with_frame(Frame{45}, FrameRate{30, 1});   // mid-beziersegment, deterministic
    ctx.sample_time = SampleTime::from_frame_int(Frame{45}, kCam01Fps);

    // (b) 5 consecutive evaluations MUST yield identical hashes.
    std::array<std::uint64_t, 5> hashes{};
    for (std::size_t i = 0; i < 5; ++i) {
        auto res = program.evaluate(ctx, session);
        REQUIRE(res.has_value());
        hashes[i] = hash_camera_state_for_golden(res->camera, desc.id);
    }
    CAPTURE(hashes[0]);
    for (std::size_t i = 1; i < 5; ++i) {
        CHECK(hashes[i] == hashes[0]);
    }

    // (c) Compare against the pinned .bin hash.  If the .bin is still the
    // placeholder sentinel (first run / pre-regen), we MESSAGE() the first
    // valid hash so the operator can pipe it to tools/regen_camera_golden.sh
    // once upstream regressions clear.  Otherwise REQUIRE exact match.
    const std::uint64_t kPinned =
        read_pinned_golden_u64(resolve_golden_path());
    if (kPinned == kUncapturedSentinel) {
        MESSAGE("Golden at " << resolve_golden_path()
                 << " is the placeholder sentinel; first valid hash observed: "
                 << hashes[0] << ".  Once the upstream trajectory-validator "
                 << "regression (camera_program_compiler.cpp:330-335) AND the "
                 << "golden_projection_test.cpp FocalPx build error are "
                 << "unblocked on origin/main, run "
                 << "tools/regen_camera_golden.sh to pin this value, then "
                 << "commit the regenerated .bin.");
    } else {
        REQUIRE(hashes[0] == kPinned);
    }
}
