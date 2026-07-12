// ==============================================================================
// tests/runtime/test_camera_session_cache_failed_no_commit_session_state.cpp
//
// TICKET-ZERO-A1 / TICKET-A3-CACHE-LEASE — locks the SESSION-STATE rollback
// contract of CameraSessionLease's RAII guard.
//
// The companion test `test_camera_session_cache_failed_no_commit.cpp` pins
// the `last_evaluated_frame` invariant: after a failed eval + uncommitted
// lease destruction, `cp_post->last_evaluated_frame` stays at the default
// sentinel `-1` (commit() being the SOLE writepoint).  This file pins the
// complementary invariant for the SESSION STATE: after ANY lease path
// (failed OR successful) where commit() is NOT called,
// `cp_post->session` MUST be byte-identical to the pre-acquire state.
//
// Discriminator: the test pre-injects a unique sentinel
// Camera2_5D{position = {13,13,13}} into `cache.find(idx)->session.last_valid_camera`
// between Phase 1 (setup commit) and Phase 2 (test eval).  After Phase 2:
//   - subcase (a) failed eval + no commit  → cache.session.last_valid_camera
//                                              is STILL the sentinel
//                                              (rollback succeeded).
//   - subcase (b) successful eval + commit → cache.session.last_valid_camera
//                                              is NOT the sentinel (writeback
//                                              happened) AND last_evaluated_frame
//                                              advanced to kTarget1.
//   - subcase (c) successful eval + no commit → cache.session.last_valid_camera
//                                                  is STILL the sentinel
//                                                  (rollback succeeded) AND
//                                                  last_evaluated_frame stays
//                                                  at kTarget0.
//
// The discriminator tests the working-copy pattern documented at
// include/chronon3d/scene/camera/camera_v1/camera_session_cache.hpp
// (`working_session` field on Entry, lease.session() returns *working_session,
// commit() copies working_session → checkpoint.session, uncommitted leases
// implicitly rollback).
//
// AGENTS.md v0.1 cat-2 freeze-compliant (no threads / no time / no PRNG /
// no font engine).  Pattern precedent: test_camera_session_cache_failed_no_commit.cpp
// (already on the chronon3d_core_tests target).
// ==============================================================================
#include <doctest/doctest.h>

#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp>
#include <chronon3d/internal/scene/camera/v1/camera_session.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_session_cache.hpp>
#include <chronon3d/core/types/sample_time.hpp>

using namespace chronon3d;
using namespace chronon3d::camera_v1;

namespace {

constexpr FrameRate kSessStateFps{60, 1};
constexpr int       kShotIdx      = 7;        // distinctive shot index
constexpr int       kShotStart    = 0;
constexpr int       kTarget0      = 10;       // Phase 1 committed target
constexpr int       kTarget1      = 11;       // Phase 2 evaluated target (forward-step)
constexpr Vec3      kSentinelPos{13.0f, 13.0f, 13.0f};

/// `make_sentinel_camera` — produces a unique Camera2_5D whose only distinguishing
/// field is position = {13,13,13}.  Used as a last_valid_camera overflow marker.
Camera2_5D make_sentinel_camera() {
    Camera2_5D cam{};
    cam.position = kSentinelPos;
    return cam;
}

/// Field-level position comparator (camera_program has no operator== for
/// Camera2_5D; mirrors the cameras_equal() pattern in
/// tests/scene/camera/test_camera_session_checkpoint.cpp).
bool camera_pos_is_sentinel(const Camera2_5D& c) {
    return c.position.x == doctest::Approx(kSentinelPos.x).epsilon(1e-5f)
        && c.position.y == doctest::Approx(kSentinelPos.y).epsilon(1e-5f)
        && c.position.z == doctest::Approx(kSentinelPos.z).epsilon(1e-5f);
}

/// Fixture for subcase (a): DistanceConstraint + Stop policy.  Position
/// ramps from Frame 10 → Frame 11; Frame 10 PASSES (distance 1000 > 10),
/// Frame 11 lands EXACTLY at POI (distance-zero, FAIL).
CameraDescriptor make_failing_at_target1_desc(const std::string& id_str) {
    CameraDescriptor desc;
    desc.id = id_str;
    desc.base.enabled = true;
    desc.base.position = Vec3{0.0f, 0.0f, -1000.0f};
    desc.base.rotation = Vec3{0.0f, 0.0f, 0.0f};
    desc.base.point_of_interest_enabled = true;
    desc.base.point_of_interest = Vec3{0.0f, 0.0f, 0.0f};
    desc.base.projection = ZoomProjection{AnimatedValue<float>{1000.0f}};
    desc.orientation = FixedOrientation{};
    PoseTracksSource pts;
    // Two keyframes so the linear interpolation at Frame 11 lands exactly at POI.
    pts.position.key(Frame{kTarget0}, Vec3{0.0f, 0.0f, -1000.0f}, Easing::Linear);
    pts.position.key(Frame{kTarget1}, Vec3{0.0f, 0.0f,     0.0f}, Easing::Linear);
    pts.use_target = false;
    desc.source = pts;
    desc.constraints.push_back(
        DistanceConstraint{/*min_distance=*/10.0f, /*max_distance=*/1000.0f});
    desc.failure_policy = CameraFailurePolicy::Stop;  // hard failure on distance-zero.
    return desc;
}

/// Fixture for subcases (b) + (c): no DistanceConstraint ⇒ all evals succeed.
/// Position ramps from Frame 10 → Frame 11; Frame 11 lands at {2,0,-999}
/// (arbitrary non-POI position — well inside any reasonable constraint range).
CameraDescriptor make_always_ok_desc(const std::string& id_str) {
    CameraDescriptor desc;
    desc.id = id_str;
    desc.base.enabled = true;
    desc.base.position = Vec3{0.0f, 0.0f, -1000.0f};
    desc.base.rotation = Vec3{0.0f, 0.0f, 0.0f};
    desc.base.point_of_interest_enabled = false;
    desc.base.point_of_interest = Vec3{0.0f, 0.0f, 0.0f};
    desc.base.projection = ZoomProjection{AnimatedValue<float>{1000.0f}};
    desc.orientation = FixedOrientation{};
    PoseTracksSource pts;
    pts.position.key(Frame{kTarget0}, Vec3{0.0f, 0.0f, -1000.0f}, Easing::Linear);
    pts.position.key(Frame{kTarget1}, Vec3{2.0f, 0.0f,  -999.0f}, Easing::Linear);
    pts.use_target = false;
    desc.source = pts;
    // No constraints: every eval returns ok=true.
    desc.failure_policy = CameraFailurePolicy::Stop;  // (default but explicit)
    return desc;
}

/// Build context at frame F with the project's 60 fps sample_time.
CameraEvalContext make_ctx(Frame f) {
    CameraEvalContext ctx;
    ctx.frame = f;
    ctx.sample_time = SampleTime::from_frame_int(f, kSessStateFps);
    return ctx;
}

} // namespace

// ==============================================================================
// SUBCASE (a) — Failed eval + no commit → checkpoint.session preserved
// ==============================================================================
TEST_CASE("runtime_camera_session_cache_failed_no_commit_session_state — "
          "TICKET-A3-CACHE-LEASE / TICKET-ZERO-A1: after Phase 1 commits Frame "
          "10 successfully and a sentinel Camera2_5D{13,13,13} is injected into "
          "cache.session.last_valid_camera, a Phase 2 evaluation at Frame 11 that "
          "FAILS the DistanceConstraint (Stop policy, distance-zero) followed by "
          "lease destruction WITHOUT commit() MUST leave cache.session byte-"
          "identical to the pre-Phase-2 state.  This locks the working-copy-on-"
          "Entry pattern documented at camera_session_cache.hpp + ADR-013 "
          "Decision 3.") {

    auto desc = make_failing_at_target1_desc("test.runtime.rollback.sessstate.a");
    auto comp = compile_camera(desc);
    REQUIRE(comp.has_value());
    auto program = std::move(comp).value();
    REQUIRE(program.is_compiled());

    CameraSessionCache cache;

    // ── Phase 1: prime with a successful Frame 10 evaluation + commit. ────
    {
        auto lease0 = cache.acquire(program, kShotIdx, kShotStart,
                                     kTarget0, kSessStateFps);
        auto ctx0 = make_ctx(Frame{kTarget0});
        auto res0 = program.evaluate(ctx0, lease0.session());
        REQUIRE(res0.has_value());
        lease0.commit();
    }
    // Cache now has last_evaluated_frame = 10 + post-Frame-10 session state.
    REQUIRE(cache.find(kShotIdx) != nullptr);
    CHECK(cache.find(kShotIdx)->last_evaluated_frame == kTarget0);

    // ── Inject sentinel into cache.checkpoint.session.last_valid_camera. ──
    cache.find(kShotIdx)->session.last_valid_camera = make_sentinel_camera();
    REQUIRE(cache.find(kShotIdx)->session.last_valid_camera.has_value());
    REQUIRE(camera_pos_is_sentinel(
        *cache.find(kShotIdx)->session.last_valid_camera));

    // ── Phase 2 (a): acquire + failed eval at Frame 11 + NO commit. ───────
    // Forward-step path: target(11) == last_evaluated_frame(10) + 1 ⇒
    // forward_step == true ⇒ must_reprime == false ⇒ no preroll.  The
    // setter above (sentinel on checkpoint.session) flows into
    // working_session via the deep copy in acquire().  eval() mutates the
    // working_session; the failure is observed by the test; commit() is
    // intentionally NOT called.
    //
    // IMPORTANT — failure-path semantics: under CameraFailurePolicy::Stop,
    // CameraProgram::evaluate() returns `!ok` BEFORE the end-of-evaluate
    // `session.last_valid_camera = result.camera` write happens.  This
    // means a failed eval does NOT touch working_session.last_valid_camera
    // via the post-eval write.  But the rollback contract (working_session
    // is the lock-evaluated scratch, commit() is the SOLE writepath)
    // STILL holds: an uncommitted lease implicitly drops its working_session
    // mutations regardless of which path the eval took.  The CAPTURE below
    // documents that the failure-path was actually taken so this test does
    // not silently degenerate into "sentinel preserved by accident".
    {
        auto lease_a = cache.acquire(program, kShotIdx, kShotStart,
                                       kTarget1, kSessStateFps);
        auto ctx_a = make_ctx(Frame{kTarget1});
        auto res_a = program.evaluate(ctx_a, lease_a.session());
        CAPTURE(res_a.error().code);  // docs which failure branch was taken.
        CAPTURE(res_a.error().message);
        REQUIRE_FALSE(res_a.has_value());  // gate feature: Frame 11 = distance-zero.
        CHECK(res_a.error().code ==
              CameraErrorCode::ConstraintFailure);
        CHECK(res_a.error().message.find("distance-zero") != std::string::npos);

        // Discriminator for the working-copy contract: working_session is
        // the post-acquire copy of checkpoint.session (sentinel-seeded).
        // Under Stop, evaluate does NOT write last_valid_camera.  So even
        // without the rollback, the working copy's last_valid_camera IS
        // still the sentinel.  To make this a TRUE regression lock for
        // the rollback contract, mutate working_session directly with a
        // dummy write that prove-only exists in the working copy if commit
        // is skipped.
        lease_a.session().banking_roll = -777.0f;  // working-only mutation.

        // NB: explicit NO commit() — lease destructor runs at scope exit
        // without writing checkpoint.session.  This is the contract under
        // test; passing through the destructor should NOT touch
        // checkpoint.session (rollback must be real, not just an implicit
        // statement of intent).
    }

    // ── Post-destruction: checkpoint.session.byte-equality check. ────────
    auto* cp_post_a = cache.find(kShotIdx);
    REQUIRE(cp_post_a != nullptr);

    // last_valid_camera MUST still be the sentinel (rollback preserved it).
    CHECK(cp_post_a->session.last_valid_camera.has_value());
    CHECK(camera_pos_is_sentinel(*cp_post_a->session.last_valid_camera));

    // STRONGER DISCRIMINATOR for the working-copy-on-Entry contract:
    // the lease_a.session().banking_roll = -777.0f mutation above reached
    // the working_session scratch field (NOT checkpoint.session, since
    // commit() was NOT called).  If the contract holds, checkpoint.session
    // still has the pre-Phase-2 banking_roll (= 0.0f default-init because
    // it was reset()'d by preroll in Phase 1 then never mutated).  This
    // positively discriminates the rollback: the last_valid_camera sentinel
    // check alone could pass by accident (Stop+distance-zero failure path
    // doesn't write last_valid_camera); the banking_roll check CANNOT pass
    // by accident because the test code DID write -777.0f to the working
    // copy.
    CHECK(cp_post_a->session.banking_roll == doctest::Approx(0.0f).epsilon(1e-5f));

    // last_evaluated_frame MUST still be 10 (commit() was not called; this
    // is the existing companion-test invariant — re-asserted here for
    // cross-test consistency).
    CHECK(cp_post_a->last_evaluated_frame == kTarget0);  // NOT advanced to 11.
}

// ==============================================================================
// SUBCASE (b) — Successful eval + commit → checkpoint.session shows post-eval state
// ==============================================================================
TEST_CASE("runtime_camera_session_cache_commit_writes_session_state — "
          "TICKET-A3-CACHE-LEASE / TICKET-ZERO-A1: after Phase 1 commits Frame "
          "10 successfully and the sentinel Camera2_5D is injected into "
          "cache.session.last_valid_camera, a Phase 2 successful evaluation "
          "at Frame 11 (no constraint failure) followed by lease.commit() "
          "MUST overwrite checkpoint.session.last_valid_camera with the "
          "post-Frame-11 camera and MUST advance last_evaluated_frame to 11.  "
          "This locks the positive control: commit() is the SINGLE writepoint "
          "for BOTH session state AND last_evaluated_frame.") {

    auto desc = make_always_ok_desc("test.runtime.rollback.sessstate.b");
    auto comp = compile_camera(desc);
    REQUIRE(comp.has_value());
    auto program = std::move(comp).value();
    REQUIRE(program.is_compiled());

    CameraSessionCache cache;

    // ── Phase 1: prime Frame 10 + commit. ─────────────────────────────────
    {
        auto lease0 = cache.acquire(program, kShotIdx, kShotStart,
                                     kTarget0, kSessStateFps);
        auto ctx0 = make_ctx(Frame{kTarget0});
        auto res0 = program.evaluate(ctx0, lease0.session());
        REQUIRE(res0.has_value());
        lease0.commit();
    }
    REQUIRE(cache.find(kShotIdx) != nullptr);
    CHECK(cache.find(kShotIdx)->last_evaluated_frame == kTarget0);

    // ── Inject sentinel. ────────────────────────────────────────────────
    cache.find(kShotIdx)->session.last_valid_camera = make_sentinel_camera();
    REQUIRE(camera_pos_is_sentinel(
        *cache.find(kShotIdx)->session.last_valid_camera));

    // ── Phase 2 (b): acquire + successful eval at Frame 11 + commit(). ────
    Vec3 post_eval_position{};
    {
        auto lease_b = cache.acquire(program, kShotIdx, kShotStart,
                                       kTarget1, kSessStateFps);
        auto ctx_b = make_ctx(Frame{kTarget1});
        auto res_b = program.evaluate(ctx_b, lease_b.session());
        REQUIRE(res_b.has_value());

        // Capture the post-eval camera position from the (still-live) working copy.
        post_eval_position = res_b->camera.position;
        // Sanity: working_session was the lock-evaluated camera.
        REQUIRE(lease_b.session().last_valid_camera.has_value());
        // Component-wise Vec3 comparison (doctest::Approx has no
        // constructor for Vec3; mirror the camera_pos_is_sentinel pattern).
        CHECK(lease_b.session().last_valid_camera->position.x ==
              doctest::Approx(post_eval_position.x).epsilon(1e-5f));
        CHECK(lease_b.session().last_valid_camera->position.y ==
              doctest::Approx(post_eval_position.y).epsilon(1e-5f));
        CHECK(lease_b.session().last_valid_camera->position.z ==
              doctest::Approx(post_eval_position.z).epsilon(1e-5f));

        lease_b.commit();  // ← the SOLE writepoint for BOTH fields.
    }

    // ── Post-destruction: cache.session.last_valid_camera must hold the
    //    post-eval camera (NOT the sentinel). ────────────────────────────
    auto* cp_post_b = cache.find(kShotIdx);
    REQUIRE(cp_post_b != nullptr);

    CHECK(cp_post_b->session.last_valid_camera.has_value());
    CHECK_FALSE(camera_pos_is_sentinel(*cp_post_b->session.last_valid_camera));
    // Component-wise Vec3 comparison (doctest::Approx has no
    // constructor for Vec3).
    CHECK(cp_post_b->session.last_valid_camera->position.x ==
          doctest::Approx(post_eval_position.x).epsilon(1e-5f));
    CHECK(cp_post_b->session.last_valid_camera->position.y ==
          doctest::Approx(post_eval_position.y).epsilon(1e-5f));
    CHECK(cp_post_b->session.last_valid_camera->position.z ==
          doctest::Approx(post_eval_position.z).epsilon(1e-5f));

    // last_evaluated_frame MUST have advanced.
    CHECK(cp_post_b->last_evaluated_frame == kTarget1);  // 11, not 10.
}

// ==============================================================================
// SUBCASE (c) — Successful eval + no commit → checkpoint.session preserved
// ==============================================================================
TEST_CASE("runtime_camera_session_cache_successful_no_commit_session_state — "
          "TICKET-A3-CACHE-LEASE / TICKET-ZERO-A1: identical setup to subcase (b) "
          "but the commit() call is DROPPED.  The post-destruction invariant "
          "is: cache.session.last_valid_camera is STILL the sentinel camera "
          "(the working-copy mutations were discarded along with the lease "
          "destructor) AND last_evaluated_frame stays at 10 (companion-test "
          "invariant).  This is the strongest discriminator between commit() "
          "and no-commit() on the same successful evaluation path.") {

    auto desc = make_always_ok_desc("test.runtime.rollback.sessstate.c");
    auto comp = compile_camera(desc);
    REQUIRE(comp.has_value());
    auto program = std::move(comp).value();
    REQUIRE(program.is_compiled());

    CameraSessionCache cache;

    // ── Phase 1: same setup as subcase (b). ───────────────────────────────
    {
        auto lease0 = cache.acquire(program, kShotIdx, kShotStart,
                                     kTarget0, kSessStateFps);
        auto ctx0 = make_ctx(Frame{kTarget0});
        auto res0 = program.evaluate(ctx0, lease0.session());
        REQUIRE(res0.has_value());
        lease0.commit();
    }
    REQUIRE(cache.find(kShotIdx) != nullptr);
    CHECK(cache.find(kShotIdx)->last_evaluated_frame == kTarget0);

    // ── Inject sentinel. ────────────────────────────────────────────────
    cache.find(kShotIdx)->session.last_valid_camera = make_sentinel_camera();
    REQUIRE(camera_pos_is_sentinel(
        *cache.find(kShotIdx)->session.last_valid_camera));

    // ── Phase 2 (c): acquire + successful eval at Frame 11 + NO commit. ───
    // The successful eval writes lease.session().last_valid_camera = the
    // post-Frame-11 camera.  Without commit(), this mutation is rolled back
    // when the lease destructor runs.
    {
        auto lease_c = cache.acquire(program, kShotIdx, kShotStart,
                                       kTarget1, kSessStateFps);
        auto ctx_c = make_ctx(Frame{kTarget1});
        auto res_c = program.evaluate(ctx_c, lease_c.session());
        REQUIRE(res_c.has_value());

        // Sanity: working copy IS mutated to the post-eval camera (this is
        // what the rollback has to discard).
        REQUIRE(lease_c.session().last_valid_camera.has_value());
        CHECK_FALSE(camera_pos_is_sentinel(*lease_c.session().last_valid_camera));

        // NB: explicit NO commit() — the rollback contract under test.
    }

    // ── Post-destruction: checkpoint.session.last_valid_camera is STILL
    //    the sentinel (rollback succeeded). ─────────────────────────────
    auto* cp_post_c = cache.find(kShotIdx);
    REQUIRE(cp_post_c != nullptr);

    CHECK(cp_post_c->session.last_valid_camera.has_value());
    CHECK(camera_pos_is_sentinel(*cp_post_c->session.last_valid_camera));

    // last_evaluated_frame MUST stay at 10 (companion invariant).
    CHECK(cp_post_c->last_evaluated_frame == kTarget0);  // NOT advanced to 11.
}
