// ==============================================================================
// test_camera_program_projection_orientation.cpp — compiled camera path:
// PROJECTION VARIANTS (§2), MODIFIER (§3) and ORIENTATION (§4) tests.
// Split from test_camera_program_compiled.cpp.
// ==============================================================================
//
// CAM-01 / DOC 04 — Baseline test suite for the COMPILED camera path
// (scope checklist lives in docs/adr/ADR-013-camera-v1-semantics.md and the
// retired camera-plan docs).  Exercises the canonical compiled path end-to-end
// without legacy imperative camera headers.
//
// Split parts (each ≤ 400 LOC):
//   - test_camera_program_orientation_ticket022.cpp   (§4.B single-application
//     canonical-order locks + look-at policy)
//   - test_camera_program_orbit_ticket024.cpp         (§4.C camera-local orbit
//     basis + §4.D roll/parent propagation)
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
