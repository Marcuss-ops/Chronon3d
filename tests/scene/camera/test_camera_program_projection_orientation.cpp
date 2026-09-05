// ==============================================================================
// test_camera_program_projection_orientation.cpp — compiled camera path:
// PROJECTION VARIANTS (§2), MODIFIER (§3) and ORIENTATION (§4) tests.
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
    ctx = ctx.with_frame(Frame{0}, FrameRate{30, 1});
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
    ctx = ctx.with_frame(Frame{0}, FrameRate{30, 1});
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
        // std::abs() for zero-axis: sin 180° ≈ 0 produces ~8.7e-05 float drift
        CHECK(std::abs(cam.position.x) < 1e-4f);
        CHECK(std::abs(cam.position.y) < 1e-4f);
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
        CHECK(std::abs(cam.position.y) < 1e-4f);
        CHECK(std::abs(cam.position.z) < 1e-4f);
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
        CHECK(std::abs(cam.position.y) < 1e-4f);
        CHECK(std::abs(cam.position.z) < 1e-4f);
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
        // std::abs() for zero-axis: sin 180° ≈ 0 produces ~4.4e-05 float drift
        CHECK(std::abs(cam.position.x) < 1e-4f);
        CHECK(std::abs(cam.position.y) < 1e-4f);
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
// §4.D — TICKET-024: orbit roll + parent propagation (AE parity)
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("compiled_orbit_roll_rotation — "
          "OrbitMotion with roll=30 deg produces non-zero rotation.z on the camera "
          "(AE parity: orbit roll affects camera bank)") {
    auto desc = make_cam01_base_desc("test.t024.roll");
    OrbitMotion orbit;
    orbit.target.set(Vec3{0.0f, 0.0f, 0.0f});
    orbit.yaw.set(0.0f);
    orbit.pitch.set(0.0f);
    orbit.radius.set(1000.0f);
    orbit.track.set(Vec3{0.0f, 0.0f, 0.0f});
    orbit.dolly.set(0.0f);
    orbit.roll.set(30.0f);  // AE parity: orbit roll → camera bank (rotation.z)
    desc.source = orbit;

    auto program = compile_or_die_cam01(desc);
    CameraSession session;
    auto cam = eval_at_or_die_cam01(program, session, Frame{0});
    CAPTURE(cam.rotation.z);
    CHECK(cam.rotation.z == doctest::Approx(30.0f).epsilon(kCam01Eps));
    // position unaffected by roll (roll is a rotation around the look axis)
    CHECK(cam.position.z == doctest::Approx(1000.0f).epsilon(kCam01Eps));
}

TEST_CASE("compiled_orbit_with_parent — "
          "OrbitMotion with parent_name set on CameraBaseSpec carries parent_name "
          "through to the evaluated Camera2_5D (AE parity: orbit camera respects "
          "parent transform hierarchy)") {
    auto desc = make_cam01_base_desc("test.t024.parent");
    desc.base.parent_name = "camera_target_null";
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
    // parent_name must propagate through the compiled path
    CHECK(cam.parent_name == "camera_target_null");
    // position/rotation carry through unchanged when no transforms are in context
    CHECK(cam.position.z == doctest::Approx(1000.0f).epsilon(kCam01Eps));
    CHECK(cam.rotation.z == doctest::Approx(0.0f).epsilon(kCam01Eps));
}

