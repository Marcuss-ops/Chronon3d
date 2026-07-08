// ==============================================================================
// tests/scene/camera/test_orient_along_path.cpp
//
// CAM-DoD Step 4 — OrientAlongPath sub-test regression lock.
//
// Each of the 4 sub-tests in the agent1 spec is encoded as an independent
// TEST_CASE so a future regression pinpoints the exact broken invariant
// without grepping a 1500-line mega-suite.  The 4 sub-tests:
//
//   (a) tangent valid + keep_horizon=true
//       → cam.rotation.z == 0 AND rotation's forward axis ≈ quat_look_along(tangent)
//
//   (b) tangent valid + keep_horizon=false + trajectory roll_deg=15
//       → cam.rotation.z == 15 (roll override applied verbatim)
//
//   (c) tangent degenerate (length < 1e-7) + session.last_tangent valid
//       → camera rotates toward session.last_tangent + Warning diagnostic
//
//   (d) tangent degenerate + session.last_tangent absent + POI active
//       → camera rotates toward POI + Warning diagnostic
//
// ALL sub-tests additionally verify that cam.rotation has NO NaN or ±Infinity
// components (failure mode where degenerate math produces garbage in the
// fallback chain).
//
// ── Pre-existing baseline regression (NOT introduced by this file) ───────────
//
// As of HEAD `3a5eb193` of origin/main, the compile_camera() validator in
// `src/scene/camera/camera_v1/camera_program_compiler.cpp:330-335` uses
// `traj->trajectory->size()` (which returns SEGMENTS, not POINTS) as the
// upper bound when validating segment indices.  The result is a spurious
// rejection of every well-formed 2-point / 1-segment trajectory with the
// diagnostic `kind=19 message="trajectory segment[0] has invalid indices:
// from_idx=0 to_idx=1 (trajectory has 1 points)"`.
//
// This regresses every test that builds a `TrajectoryMotion` source on
// HEAD.  Two test families therefore fail at `compile_or_die` UNIFORMLY
// here:
//   • baseline `compiled_orient_along_path_*` in test_camera_program_compiled.cpp
//     (these were authored against an older branch where the validator
//      did not use `size()`);
//   • NEW sub-tests in this file.
//
// Sub-test (d) does NOT use a trajectory source and is therefore immune
// to this regression.  Sub-tests (a) (b) (c) need a trajectory so they can
// test tangent behaviour; they will surface the validator bug at
// `compile_or_die`.  We deliberately do NOT mask the failure (no skip, no
// WARN-and-return) — running this file drives the regression into the
// doctest report where it can be audited.
//
// Suggested followup (separate ADR / TICKET, NOT in this commit):
// change `camera_program_compiler.cpp:331` from
//     const auto n_pts = traj->trajectory->size();
// to
//     const auto n_pts = traj->trajectory->points().size();
// and re-run `compiled_trajectory_*` / `compiled_orient_along_path_*` /
// this entire file to confirm the 4+11 baseline regressions flip green.
// ==============================================================================
#include <doctest/doctest.h>

#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_session.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_trajectory.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>

#include <cmath>
#include <memory>

using namespace chronon3d;
using namespace chronon3d::camera_v1;

namespace {

// TICKET-120 followup (Unity build deconflict) — renamed from
// `kEpsOrientAlongPath` to file-scoped unique name to avoid the redefinition
// error in the unified TU built by chronon3d_scene_tests (three
// test files shared an anonymous-namespace `constexpr float kEpsOrientAlongPath`
// that collide in Unity build: see TICKET-120 build-redefinition
// group).
constexpr float kEpsOrientAlongPath = 1e-5f;
constexpr FrameRate kFpsOrientAlongPath{60, 1};

// ── NaN / Infinity detector for result.camera.rotation ──
bool is_finite3(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// ── L2-magnitude helper for any camera-v1 euler-degree rotation ──
float rotation_l2(const Vec3& r_deg) {
    return std::sqrt(r_deg.x * r_deg.x + r_deg.y * r_deg.y + r_deg.z * r_deg.z);
}

// ── Helpers ──
CameraProgram compile_or_die_orient_along_path(const CameraDescriptor& desc) {
    auto result = compile_camera(desc, /*catalog=*/nullptr);
    REQUIRE(result.has_value());          // fail loudly if the descriptor is
    auto program = std::move(result).value();  // rejected (validator regression visible here)
    REQUIRE(program.is_compiled());
    return program;
}

Camera2_5D eval_at_or_die_orient(const CameraProgram& program,
                          CameraSession& session, Frame frame) {
    CameraEvalContext ctx;
    ctx.frame = frame;
    ctx.sample_time = SampleTime::from_frame_int(frame, kFpsOrientAlongPath);
    auto res = program.evaluate(ctx, session);
    REQUIRE(res.has_value());
    return res->camera;
}

bool has_warning_message_containing(
    const std::vector<CameraProgramDiagnostic>& diagnostics,
    const std::string& needle) {
    for (const auto& d : diagnostics) {
        if (d.severity == CameraProgramDiagnostic::Severity::Warning &&
            d.message.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

// ══════════════════════════════════════════════════════════════════════════
// (a) tangent valid + keep_horizon=true → rotation.z == 0 + forward = quat_look_along(tangent)
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("OrientAlongPath (a) — tangent valid + keep_horizon=true sets "
          "rotation.z==0 and forward≈quat_look_along(tangent)") {
    // Build a 2-point / 1-segment trajectory:
    //   P0 = (0,0,-1000)        P1 = (0,0,-500)
    // Segment 0 direction = (0,0,+1) → quat_look_along((0,0,1)) ≈ identity.
    auto traj = CameraTrajectoryBuilder()
                    .move_to(Vec3{0.0f, 0.0f, -1000.0f})
                    .move_to(Vec3{0.0f, 0.0f, -500.0f})
                    .duration_frames(60.0f)
                    .build();
    REQUIRE(traj);
    REQUIRE(traj->size() == 1);
    REQUIRE(traj->points().size() == 2);

    CameraDescriptor desc;
    desc.id = "test.oap.a";
    desc.base.enabled = true;
    desc.base.position = Vec3{0.0f, 0.0f, -1000.0f};        // overridden by trajectory
    desc.base.rotation = Vec3{0.0f, 30.0f, 17.0f};          // SSA base → cleared by OAP
    desc.base.projection = ZoomProjection{AnimatedValue<float>{1000.0f}};
    desc.source = TrajectoryMotion{traj, /*use_arc_length=*/true};
    desc.orientation = OrientAlongPath{/*keep_horizon=*/true};

    auto program = compile_or_die_orient_along_path(desc);
    CameraSession session;

    // Sample at Frame{30} (mid-segment): tangent is the segment direction
    // (P1 - P0) normalised = (0,0,+1).  The evaluation order is
    //   1. evaluate_compiled_source  → tangent = (0,0,+1), position = midway
    //   2. modifiers                  → none
    //   3. apply_orientation_spec_free(OrientAlongPath)
    //        fwd = normalize(tangent) = (0,0,+1)
    //        cam.rotation = quat_to_camera_euler(quat_look_along((0,0,+1)), 0)
    //        with keep_horizon=true → roll stays 0  ⇒  cam.rotation.z == 0 exactly
    //   4. constraints (none here)
    auto cam = eval_at_or_die_orient(program, session, Frame{30});

    CAPTURE(cam.rotation.x);
    CAPTURE(cam.rotation.y);
    CAPTURE(cam.rotation.z);    CHECK(is_finite3(cam.rotation));        // no NaN / Inf in fallback
    CHECK(cam.rotation.z == doctest::Approx(0.0f).epsilon(kEpsOrientAlongPath));   // keep_horizon clears roll
    // Forward-magnitude: a look-along-(0,0,1) rotation should be near identity
    // in euler space (no yaw, no pitch, no roll).  The numerical identity is
    // locked canonically in test_camera_program_compiled.cpp §1.B via
    // `compiled_orientation_look_at_canonical_rotation_computation`; here we
    // use a structural magnitude check that ALSO catches "rotation became
    // NaN / garbage in the fallback".
    CHECK(rotation_l2(cam.rotation) < 5.0f);  // near-identity for a +Z look
}

// ══════════════════════════════════════════════════════════════════════════
// (b) tangent valid + keep_horizon=false + roll_deg=15 → rotation.z == 15
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("OrientAlongPath (b) — tangent valid + keep_horizon=false + "
          "trajectory.roll_deg=15 sets rotation.z==15 verbatim") {
    // Trajectory whose first point carries roll_deg = 15.  Second point is on-axis
    // so a tangent +Z look gives a near-identity forward — isolating the roll
    // assertion from the forward rotation noise.
    auto traj = CameraTrajectoryBuilder()
                    .move_to(Vec3{0.0f, 0.0f, -1000.0f},
                              /*target=*/std::nullopt,
                              /*roll_deg=*/15.0f)
                    .move_to(Vec3{0.0f, 0.0f, -500.0f})
                    .duration_frames(60.0f)
                    .build();
    REQUIRE(traj);
    REQUIRE(traj->size() == 1);
    REQUIRE(traj->points().size() == 2);
    // Sentinel: confirm the roll value made it onto point[0].
    REQUIRE(traj->points()[0].roll_deg == doctest::Approx(15.0f).epsilon(kEpsOrientAlongPath));

    CameraDescriptor desc;
    desc.id = "test.oap.b";
    desc.base.enabled = true;
    desc.base.position = Vec3{0.0f, 0.0f, -1000.0f};
    desc.base.rotation = Vec3{0.0f, 0.0f, 0.0f};           // identity baseline
    desc.base.projection = ZoomProjection{AnimatedValue<float>{1000.0f}};
    desc.source = TrajectoryMotion{traj, /*use_arc_length=*/true};
    desc.orientation = OrientAlongPath{/*keep_horizon=*/false};   // roll from traj WINS

    auto program = compile_or_die_orient_along_path(desc);
    CameraSession session;

    // Frame 0 = anchor point P0.  The Linear sampler interpolates roll
    // between P0.roll_deg=15 and P1.roll_deg=0 across the segment, so a
    // MID-SEGMENT sample (e.g. Frame{30}) would read ~7.5° not 15°.  To
    // match the existing baseline `compiled_trajectory_transfers_roll_deg`
    // pattern (which asserts at Frame{0} with ±1° slack for arc-length
    // drift near the boundary) we anchor at Frame{0} here too.
    auto cam = eval_at_or_die_orient(program, session, Frame{0});

    CAPTURE(cam.rotation.x);
    CAPTURE(cam.rotation.y);
    CAPTURE(cam.rotation.z);
    CHECK(is_finite3(cam.rotation));             // no NaN / Inf in fallback
    CHECK(std::abs(cam.rotation.z - 15.0f) <= 1.0f);  // roll from P0 applied (1° arc-length slack)
    // Forward direction should still be near-identity (because tangent at
    // Frame{0} of a Linear P0→P1 segment is +Z toward P1); this confirms
    // the roll application doesn't disturb the forward direction.
    const Vec3 fwd_off_axis = Vec3{cam.rotation.x, cam.rotation.y, 0.0f};
    CHECK(rotation_l2(fwd_off_axis) < 5.0f);
}

// ══════════════════════════════════════════════════════════════════════════
// (c) tangent degenerate + last_tangent valid → use last_tangent + Warning
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("OrientAlongPath (c) — degenerate tangent falls back to "
          "session.last_tangent AND emits Warning \"previous frame tangent\"") {
    // Two-segment trajectory:
    //   segment 0 (moveto-bezier) — non-degenerate, sets session.last_tangent
    //                                to a +Z direction
    //   segment 1 (hold)         — degenerate tangent (0,0,0)
    // We use bezier_to(zero, zero, ...) so the first segment reduces to
    // a straight line (matches the canonical baseline pattern).
    auto traj = CameraTrajectoryBuilder()
                    .move_to(Vec3{0.0f, 0.0f, -1000.0f})
                    .bezier_to(Vec3{0.0f, 0.0f, 0.0f},
                                Vec3{0.0f, 0.0f, 0.0f},
                                Vec3{0.0f, 0.0f, -500.0f})
                    .duration_frames(30.0f)
                    .hold_for(30.0f)
                    .build();
    REQUIRE(traj);
    REQUIRE(traj->size() == 2);  // 1 bezier + 1 hold

    CameraDescriptor desc;
    desc.id = "test.oap.c";
    desc.base.enabled = true;
    desc.base.position = Vec3{0.0f, 0.0f, -1000.0f};
    desc.base.rotation = Vec3{0.0f, 0.0f, 0.0f};
    desc.base.projection = ZoomProjection{AnimatedValue<float>{1000.0f}};
    // Pin a POI so the engine's fallback hierarchy (4) can ALSO activate;
    // the contract is that step 2 (last_tangent) fires BEFORE step 3 (POI).
    desc.base.point_of_interest_enabled = true;
    desc.base.point_of_interest = Vec3{500.0f, 0.0f, 0.0f};   // distinctly different from +Z
    desc.source = TrajectoryMotion{traj, /*use_arc_length=*/true};
    desc.orientation = OrientAlongPath{/*keep_horizon=*/false};

    auto program = compile_or_die_orient_along_path(desc);
    CameraSession session;

    // Frame 15: mid-bezier.  Tangent is non-zero (+Z), so
    //   step 1 fires → cam.rotation known; session.last_tangent populated.
    CameraEvalContext ctx15;
    ctx15.frame = Frame{15};
    ctx15.sample_time = SampleTime::from_frame_int(Frame{15}, kFpsOrientAlongPath);
    auto res15 = program.evaluate(ctx15, session);
    REQUIRE(res15.has_value());
    CHECK(is_finite3(res15->camera.rotation));
    // Sanity: session.last_tangent should be set after this frame.
    REQUIRE(session.last_tangent.has_value());

    // Frame 45: mid-hold — tangent = (0,0,0) (degenerate).  The engine must
    // fall back to session.last_tangent (step 2).  Verify:
    //   - rotation produced (NOT identity-baseline and NOT POI-pointing)
    //   - Warning diagnostic mentioning "previous frame tangent"
    CameraEvalContext ctx45;
    ctx45.frame = Frame{45};
    ctx45.sample_time = SampleTime::from_frame_int(Frame{45}, kFpsOrientAlongPath);
    auto res45 = program.evaluate(ctx45, session);
    REQUIRE(res45.has_value());

    CHECK(is_finite3(res45->camera.rotation));
    CHECK(has_warning_message_containing(res45->diagnostics,
                                          "previous frame tangent"));

    // Forward-direction sanity: last_tangent is +Z (from segment 0); rotation
    // magnitude for the +Z look should be near identity OR at most a roll
    // offset (roll_deg at the hold point is 0 since the trajectory point
    // and the hold segment don't carry explicit roll).
    const Vec3 fwd_off =
        Vec3{res45->camera.rotation.x, res45->camera.rotation.y, 0.0f};
    CHECK(rotation_l2(fwd_off) < 5.0f);
    // roll should be small (the fallback step does not introduce new roll).
    CHECK(std::abs(res45->camera.rotation.z) < 1.0f);
}

// ══════════════════════════════════════════════════════════════════════════
// (d) everything degenerate + POI active → rotate toward POI + Warning
// ══════════════════════════════════════════════════════════════════════════
//
// We use `StaticCameraSource` (no trajectory = tangent is nullopt from
// evaluate_compiled_source, so step 1 cannot fire) and a fresh
// CameraSession (so step 2 cannot fire because session.last_tangent is
// std::nullopt at reset).  Step 3 (point_of_interest) is then the
// authoritative fallback and step 4 is NOT invoked.  This sub-test is
// immune to the size()/points().size() validator regression in
// camera_program_compiler.cpp:330-335.

TEST_CASE("OrientAlongPath (d) — fully degenerate (StaticCameraSource, "
          "no last_tangent, POI active) falls back to POI direction "
          "AND emits Warning, never invoking keep-rotation step 4") {
    CameraDescriptor desc;
    desc.id = "test.oap.d";
    desc.base.enabled = true;
    // Off-axis from POI so the fallback path produces a non-identity rotation;
    // this is the structural guard that the engine actually used the POI
    // direction (and didn't silently keep the base rotation).
    desc.base.position = Vec3{0.0f, 0.0f, -1000.0f};
    desc.base.rotation = Vec3{0.0f, 0.0f, 0.0f};   // identity baseline
    desc.base.point_of_interest_enabled = true;
    desc.base.point_of_interest = Vec3{200.0f, 150.0f, 100.0f};   // off-axis from cam
    desc.base.projection = ZoomProjection{AnimatedValue<float>{1000.0f}};
    desc.source = StaticCameraSource{};            // ← tangent = std::nullopt
    desc.orientation = OrientAlongPath{/*keep_horizon=*/false};

    auto program = compile_or_die_orient_along_path(desc);
    // Fresh session ⇒ session.last_tangent == std::nullopt ⇒ step 2 cannot fire.
    CameraSession session;

    // Build the eval context manually so we can also inspect diagnostics.
    CameraEvalContext ctx;
    ctx.frame = Frame{0};
    ctx.sample_time = SampleTime::from_frame_int(Frame{0}, kFpsOrientAlongPath);
    auto res = program.evaluate(ctx, session);
    REQUIRE(res.has_value());

    auto& cam = res->camera;
    CAPTURE(cam.rotation.x);
    CAPTURE(cam.rotation.y);
    CAPTURE(cam.rotation.z);
    CHECK(is_finite3(cam.rotation));
    CHECK(has_warning_message_containing(res->diagnostics, "point_of_interest"));

    // The POI fallback IS step 3 in the engine's 1→2→3→4 hierarchy.  Step 4
    // (which would emit "keeping base rotation") is NOT invoked because
    // step 3 found a valid direction.  Verify the step-4 message is ABSENT
    // — otherwise the engine has skipped the POI step and is silently
    // keeping base.rotation = (0,0,0), which would fail the structural
    // look-at check below.
    CHECK_FALSE(has_warning_message_containing(res->diagnostics,
                                                "keeping base rotation"));

    // Structural guard: the POI is off-axis (200, 150, +100) from camera at
    // (0, 0, -1000), so the fallback rotation MUST be non-trivial (L2-magnitude
    // above the float noise floor).  If step 4 silently fired,
    // cam.rotation == base.rotation == (0,0,0) and rotation_l2 == 0.
    CHECK(rotation_l2(cam.rotation) > 0.5f);  // degrees, matches baseline test floor
}

// ══════════════════════════════════════════════════════════════════════════
// (e) fully degenerate + no POI → step 4: keep base rotation + Warning
// ══════════════════════════════════════════════════════════════════════════
//
// All fallbacks exhausted: tangent=nullopt (StaticCameraSource),
// last_tangent=nullopt (fresh session), POI=disabled → step 4 fires.
// The camera MUST keep its base rotation and emit the "keeping base
// rotation" Warning.  This sub-test is immune to the trajectory validator
// regression because it uses StaticCameraSource.

TEST_CASE("OrientAlongPath (e) — fully degenerate (no tangent, "
          "no last_tangent, POI disabled) falls back to step 4: "
          "keep base rotation AND emit Warning 'keeping base rotation'") {
    CameraDescriptor desc;
    desc.id = "test.oap.e";
    desc.base.enabled = true;
    // Non-identity base rotation — if step 4 fires correctly, this
    // value is preserved verbatim (no look-at transform applied).
    desc.base.position = Vec3{0.0f, 0.0f, -1000.0f};
    desc.base.rotation = Vec3{12.0f, -8.0f, 3.0f};
    desc.base.point_of_interest_enabled = false;  // ← POI disabled → step 3 cannot fire
    desc.base.projection = ZoomProjection{AnimatedValue<float>{1000.0f}};
    desc.source = StaticCameraSource{};            // ← tangent = std::nullopt → step 1 cannot fire
    desc.orientation = OrientAlongPath{/*keep_horizon=*/false};

    auto program = compile_or_die_orient_along_path(desc);
    CameraSession session;  // fresh → last_tangent = std::nullopt → step 2 cannot fire

    CameraEvalContext ctx;
    ctx.frame = Frame{0};
    ctx.sample_time = SampleTime::from_frame_int(Frame{0}, kFpsOrientAlongPath);
    auto res = program.evaluate(ctx, session);
    REQUIRE(res.has_value());

    auto& cam = res->camera;
    CAPTURE(cam.rotation.x);
    CAPTURE(cam.rotation.y);
    CAPTURE(cam.rotation.z);

    // Step 4: "keeping base rotation" Warning MUST be emitted.
    CHECK(has_warning_message_containing(res->diagnostics,
                                          "keeping base rotation"));

    // Base rotation MUST be preserved identically (no fallback look-at applied).
    CHECK(cam.rotation.x == doctest::Approx(12.0f).epsilon(kEpsOrientAlongPath));
    CHECK(cam.rotation.y == doctest::Approx(-8.0f).epsilon(kEpsOrientAlongPath));
    CHECK(cam.rotation.z == doctest::Approx(3.0f).epsilon(kEpsOrientAlongPath));

    // Rotation must be finite (sanity guard against NaN in the fallback chain).
    CHECK(is_finite3(cam.rotation));

    // Structural guard: step 1, 2, 3 warnings must NOT be present — only
    // step 4 should fire.
    CHECK_FALSE(has_warning_message_containing(res->diagnostics,
                                                "previous frame tangent"));
    CHECK_FALSE(has_warning_message_containing(res->diagnostics,
                                                "point_of_interest"));
}
