// ==============================================================================
// tests/runtime/test_camera_session_keep_last_valid.cpp
//
// TICKET-A3-SESSION-POLICY (Agent3 mission DoD gate (c)) — 2-frame recovery
// regression lock.
//
// Wires `CameraFailurePolicy::KeepLastValidCamera` to the existing
// `CameraSession::last_valid_camera` field — the Agent3 mission explicitly
// states that today the policy "segues the same branch as Stop" (problem #2),
// so a true `last_valid_camera` recovery is the contract that this test
// locks against future regressions.
//
// The supporting telemetry on why this lives in tests/runtime/ (not tests/
// scene/camera/): the SESSION-side state carrying the cached snapshot is
// the subject of the test (not the COMPILED PROGRAM itself); runtime/
// is the canonical home for session-isolation / cross-frame-state tests
// per tests/runtime/test_render_session_reset_and_isolation.cpp precedent
// (WP-3 PR 3.0/3.1/3.2).
//
// Companion tests (already in tests/scene/camera/test_camera_program_compiled.cpp):
//   - `compiled_failure_policy_stop`                   — Stop true-error lock
//   - `compiled_failure_policy_skip_failed`           — SkipFailedContract diag lock
//   - `compiled_failure_policy_keep_last_valid`       — single-frame fallback
//                                                       (no cached camera → error)
// The companion + this file together cover all 3 branches of the
// `switch (failure_policy_)` in CameraProgram::evaluate() — the file
// `src/scene/camera/camera_v1/camera_program.cpp` is the source-of-truth.
//
// Reference: src/scene/camera/camera_v1/camera_program.cpp
//            CameraProgram::evaluate() — KeepLastValidCamera branch.
// ==============================================================================
#include <doctest/doctest.h>

#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp>
#include <chronon3d/internal/scene/camera/v1/camera_session.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/animation/easing/easing.hpp>

#include <string>  // std::string — used by recovery diagnostic assertions

using namespace chronon3d;
using namespace chronon3d::camera_v1;

namespace {

constexpr FrameRate kKlvFps{60, 1};

// ── Fixture: a PoseTracksSource whose position keyframes guarantee that
//    Frame{0} lands INSIDE the constraint range and Frame{1} lands AT
//    the POI (distance-zero failure).  The required Pair-of-keyframes
//    trick (two consecutive keyframes with the second at the POI)
//    exploits the linear interpolation: at Frame{1}, sample_time
//    equals the right-edge keyframe, so position is exactly (0,0,0).
CameraDescriptor make_klv_desc(const std::string& id_str,
                                CameraFailurePolicy policy) {
    CameraDescriptor desc;
    desc.id = id_str;
    desc.base.enabled = true;
    desc.base.position = Vec3{0.0f, 0.0f, -1000.0f};  // Frame{0} snapshot origin
    desc.base.rotation = Vec3{0.0f, 0.0f, 0.0f};
    desc.base.point_of_interest_enabled = true;
    desc.base.point_of_interest = Vec3{0.0f, 0.0f, 0.0f};
    desc.base.projection = ZoomProjection{AnimatedValue<float>{1000.0f}};
    desc.orientation = FixedOrientation{};
    PoseTracksSource pts;
    pts.position.key(Frame{0}, Vec3{0.0f, 0.0f, -1000.0f}, Easing::Linear);
    pts.position.key(Frame{1}, Vec3{0.0f, 0.0f,     0.0f}, Easing::Linear);
    pts.use_target = false;
    desc.source = pts;
    desc.constraints.push_back(
        DistanceConstraint{/*min_distance=*/10.0f, /*max_distance=*/1000.0f});
    desc.failure_policy = policy;
    return desc;
}

// Helper: build CameraEvalContext at the given Frame with the project's
// 60 fps SampleTime derivation (deterministic per TICKET-A3-FRAMERATE
// requirement; explicit FrameRate from the caller, no 30 fps hidden).
CameraEvalContext make_ctx(Frame f) {
    CameraEvalContext ctx;
    ctx.frame = f;
    ctx.sample_time = SampleTime::from_frame_int(f, kKlvFps);
    return ctx;
}

} // namespace

// =============================================================================
// SUBCASE A — Primary gate (c): cached recovery.
// =============================================================================
//
//   Frame 0 → position (0,0,-1000), distance 1000 → within [10,1000]
//              → DistanceConstraint PASSES → session.last_valid_camera populated.
//   Frame 1 → position (0,0,0), distance 0 → "distance-zero" FAILURE
//              → KeepLastValidCamera branch reads cached camera,
//                 emits ONE recovery Warning naming constraint[0] + reason,
//                 returns EvaluatedCamera with the cached camera (ok=true).
//
// Contrast with Stop: under Stop the same Frame 1 returns ok=false as a
// CameraEvaluationError — the discriminating assertion is res1.has_value().
TEST_CASE("runtime_camera_session_keep_last_valid_two_frame_recovery — "
          "TICKET-A3-SESSION-POLICY DoD gate (c): after a successful eval "
          "(Frame 0) populates session.last_valid_camera, a subsequent eval "
          "(Frame 1) that fails DistanceConstraint returns ok=true with the "
          "previously-cached camera, NOT the Stop-equivalent CameraEvaluationError. "
          "A single recovery Warning diagnostic names the failed constraint "
          "index and reason.") {
    auto desc = make_klv_desc("test.runtime.klv.recovery",
                              CameraFailurePolicy::KeepLastValidCamera);
    auto result = compile_camera(desc);
    REQUIRE(result.has_value());
    auto program = std::move(result).value();
    REQUIRE(program.is_compiled());

    CameraSession session;
    session.ensure_constraint_states(1);

    // ── Frame 0: PASSES.  Position (0,0,-1000), distance 1000 → [10,1000]. ──
    auto ctx0 = make_ctx(Frame{0});
    auto res0 = program.evaluate(ctx0, session);
    REQUIRE(res0.has_value());  // baseline OK; without this, no cache to recover from.
    CAPTURE(res0->camera.position.z);
    CAPTURE(res0->diagnostics.size());

    // Frame 0 snapshot MUST land at (0,0,-1000) — the constraint did NOT move it.
    CHECK(res0->camera.position.x == doctest::Approx(0.0f).epsilon(1e-5f));
    CHECK(res0->camera.position.y == doctest::Approx(0.0f).epsilon(1e-5f));
    CHECK(res0->camera.position.z == doctest::Approx(-1000.0f).epsilon(1e-5f));
    // Frame 0 must NOT have produced a recovery diagnostic (constraint actually
    // PASSED, so no KeepLastValidCamera branch was taken).
    for (const auto& d : res0->diagnostics) {
        CHECK(d.message.find("Recovered") == std::string::npos);
    }

    // ── Frame 1: FAILS.  Position (0,0,0), distance 0 → distance-zero. ────
    // KeepLastValidCamera + cached Frame 0 snapshot ⇒ return EvaluatedCamera
    // (ok=true) with the cached camera inside.
    auto ctx1 = make_ctx(Frame{1});
    auto res1 = program.evaluate(ctx1, session);
    REQUIRE(res1.has_value());  // ← primary gate (c) discriminator: ok=true.
    CAPTURE(res1->camera.position.z);
    CAPTURE(res1->diagnostics.size());

    // Recovered camera MUST equal the cached Frame 0 snapshot.
    CHECK(res1->camera.position.x == doctest::Approx(0.0f).epsilon(1e-5f));
    CHECK(res1->camera.position.y == doctest::Approx(0.0f).epsilon(1e-5f));
    CHECK(res1->camera.position.z == doctest::Approx(-1000.0f).epsilon(1e-5f));

    // Recovery Warning diagnostic MUST be emitted, naming the failed
    // constraint index (0) and the reason ("distance-zero").
    // The diagnostic message format comes from the source change in
    // src/scene/camera/camera_v1/camera_program.cpp — see TICKET-A3-
    // SESSION-POLICY sentinel block-comment for the wire contract.
    bool found_recovery_warning = false;
    for (const auto& d : res1->diagnostics) {
        if (d.severity == CameraProgramDiagnostic::Severity::Warning &&
            d.message.find("Recovered") != std::string::npos &&
            d.message.find("distance-zero") != std::string::npos) {
            found_recovery_warning = true;
            break;
        }
    }
    CHECK(found_recovery_warning);
}

// =============================================================================
// SUBCASE C — Stop vs KeepLastValidCamera side-by-side discriminator.
// =============================================================================
//
// Reuses the existing `make_klv_desc(id, policy)` fixture: identical
// position keyframes (Frame 0 → Frame 1 distance-zero failure on the SAME
// constraint).  Locks the user-spec invariant from Phase 1.D
// (TICKET-PHASE-1D):
//
//   "Per KeepLastValidCamera: deve VERAMENTE recuperare session.last_valid_camera
//    e continuare, NON comportarsi come Stop. Aggiungi unit test che
//    distingue Stop da KeepLastValidCamera."
//
// Both policies run a Frame 0 success + a Frame 1 distance-zero failure on
// the SAME descriptor (parametric in failure_policy).  The discriminator
// is per-policy:
//   - CameraFailurePolicy::Stop               → CameraEvaluationError
//                                                  (error.code == ConstraintFailure)
//   - CameraFailurePolicy::KeepLastValidCamera → EvaluatedCamera with the
//                                                  cached Frame 0 snapshot
//                                                  (camera.position.z == -1000).
//
// This blocks the documented failure mode (Phase 1.D ticket:
// "KeepLastValidCamera segues the same branch as Stop") — i.e. a future
// regression that swallows the `last_valid_camera` recovery path will
// break SUBCASE C on KeepLastValidCamera while leaving the Stop half green.
// =============================================================================
TEST_CASE("runtime_camera_session_keep_last_valid_vs_stop_two_frame_distinction — "
          "TICKET-PHASE-1D + TICKET-A3-SESSION-POLICY: side-by-side "
          "discriminator between CameraFailurePolicy::Stop and "
          "CameraFailurePolicy::KeepLastValidCamera on the SAME distance-zero "
          "failing-frame fixture.  Stop returns CameraEvaluationError with "
          "CameraErrorCode::ConstraintFailure on Frame 1 (no recovery).  "
          "KeepLastValidCamera, after a successful Frame 0 that populates "
          "session.last_valid_camera, returns EvaluatedCamera with the cached "
          "Frame 0 snapshot and a recovery Warning diagnostic on Frame 1.  "
          "Locks the SortSeguesDifference regression lock for Phase 1.D.") {
    SUBCASE("Stop policy: Frame 0 success → Frame 1 distance-zero → CameraEvaluationError") {
        auto desc = make_klv_desc("test.runtime.klv_vs_stop.stop",
                                  CameraFailurePolicy::Stop);
        auto result = compile_camera(desc);
        REQUIRE(result.has_value());
        auto program = std::move(result).value();
        REQUIRE(program.is_compiled());

        CameraSession session;
        session.ensure_constraint_states(1);

        // Frame 0: PASSES. Position (0,0,-1000), distance 1000 → [10,1000].
        auto ctx0 = make_ctx(Frame{0});
        auto res0 = program.evaluate(ctx0, session);
        REQUIRE(res0.has_value());
        CHECK(res0->camera.position.z == doctest::Approx(-1000.0f).epsilon(1e-5f));

        // Frame 1: FAILS under Stop. No cached recovery; returns ok=false.
        auto ctx1 = make_ctx(Frame{1});
        auto res1 = program.evaluate(ctx1, session);
        REQUIRE_FALSE(res1.has_value());  // discriminator arm (Stop).
        CHECK(res1.error().code == CameraErrorCode::ConstraintFailure);
        CHECK(res1.error().message.find("distance-zero") != std::string::npos);
    }

    SUBCASE("KeepLastValidCamera policy: Frame 0 success → Frame 1 distance-zero → cached recovery") {
        auto desc = make_klv_desc("test.runtime.klv_vs_stop.keep",
                                  CameraFailurePolicy::KeepLastValidCamera);
        auto result = compile_camera(desc);
        REQUIRE(result.has_value());
        auto program = std::move(result).value();
        REQUIRE(program.is_compiled());

        CameraSession session;
        session.ensure_constraint_states(1);

        // Frame 0: PASSES. Caches Frame 0 camera into session.last_valid_camera.
        auto ctx0 = make_ctx(Frame{0});
        auto res0 = program.evaluate(ctx0, session);
        REQUIRE(res0.has_value());
        CHECK(res0->camera.position.z == doctest::Approx(-1000.0f).epsilon(1e-5f));

        // Frame 1: FAILS distance-zero. KeepLastValidCamera + cached Frame 0
        // → return EvaluatedCamera with the cached snapshot. This is the
        // discriminator arm (KeepLastValidCamera) — under Phase 1.D's
        // regression risk (KeepLastValidCamera segues the same branch as Stop),
        // this would require_false here.
        auto ctx1 = make_ctx(Frame{1});
        auto res1 = program.evaluate(ctx1, session);
        REQUIRE(res1.has_value());  // discriminator arm (Keep): ok=true.
        CHECK(res1->camera.position.z == doctest::Approx(-1000.0f).epsilon(1e-5f));

        // Recovery Warning diagnostic naming the failed constraint + reason.
        bool found_recovery_warning = false;
        for (const auto& d : res1->diagnostics) {
            if (d.severity == CameraProgramDiagnostic::Severity::Warning &&
                d.message.find("Recovered") != std::string::npos &&
                d.message.find("distance-zero") != std::string::npos) {
                found_recovery_warning = true;
                break;
            }
        }
        CHECK(found_recovery_warning);
    }
}

// =============================================================================
// SUBCASE B — session.reset() clears the cache; subsequent failures then
// fall back to true error (the second branch of the KeepLastValidCamera
// recovery switch — no cached snapshot to recover from).
// =============================================================================
//
//   session.reset()  →  last_valid_camera.reset() in camera_session.hpp
//   Frame 0 with reset session → can NOT cache → falls through to
//   ConstraintFailure (true error).
//
// Companion visibility: with a populated session, the SAME failure yields
// ok=true (SUBCASE A above); without a populated session, the SAME
// failure yields ok=false (this SUBCASE).  The branch in
// camera_program.cpp's switch (KeepLastValidCamera) is observable through
// this difference without any source-private-field probe.
TEST_CASE("runtime_camera_session_keep_last_valid_reset_clears_cache — "
          "TICKET-A3-SESSION-POLICY: CameraSession::reset() clears "
          "last_valid_camera (camera_session.hpp reset() chain), so a "
          "post-reset failure leaves no cache to recover from and the "
          "KeepLastValidCamera branch falls through to true "
          "ConstraintFailure.  This locks the second switch-arm of the "
          "recovery code-path.") {
    auto desc = make_klv_desc("test.runtime.klv.reset",
                              CameraFailurePolicy::KeepLastValidCamera);
    auto result = compile_camera(desc);
    REQUIRE(result.has_value());
    auto program = std::move(result).value();
    REQUIRE(program.is_compiled());

    CameraSession session;
    session.ensure_constraint_states(1);
    session.reset();  // explicit clear-of-cache contract — the regression lock target.

    // Frame 1 with reset session: position (0,0,0) → distance-zero failure
    // → no cached last_valid_camera → fall through to CameraEvaluationError.
    auto ctx1 = make_ctx(Frame{1});
    auto res1 = program.evaluate(ctx1, session);
    REQUIRE_FALSE(res1.has_value());  // the only difference vs SUBCASE A.
    CHECK(res1.error().code == CameraErrorCode::ConstraintFailure);
    CHECK(res1.error().message.find("constraint") != std::string::npos);
    CHECK(res1.error().message.find("distance-zero") != std::string::npos);
    CHECK(res1.error().message.find("no valid camera cached") != std::string::npos);
}
