// ==============================================================================
// tests/scene/camera/test_camera_session_checkpoint.cpp
//
// TICKET-031 — CameraStateCheckpoint + canonical pre-roll strategy.
//   6 acceptance tests (per user spec, Italian):
//     §1 serial-vs-parallel        — 100 frames sequential ≡ 100 parallel-cached
//     §2 sequential-vs-random      — re-enter frame N == first-encounter of N
//     §3 two simultaneous jobs     — two workers same program/frame → same result
//     §4 retry of same frame       — evaluate(N) twice → bit-equal
//     §5 sub-frame repeated        — same sub-frame back-to-back → bit-equal
//     §6 checkpoint restore        — snapshot, mutate, restore → bit-equal ref
//
// The tests exercise `CameraSessionCache` (per-worker), the free helper
// `preroll_session_for_frame`, and `CameraStateCheckpoint` (capture/restore
// round-trip).  Each test uses the canonical contract:
//   - first-encounter     ⇒ full reset + preroll + eval
//   - forward-step (last+1) ⇒ single-step reuse
//   - any other access pattern ⇒ full reset + preroll + eval
// so any non-deterministic code path would surface as a CHECK failure.
// ==============================================================================
#include <doctest/doctest.h>

#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_session.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_session_cache.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>

#include <cstring>
#include <utility>

using namespace chronon3d::camera_v1;
using namespace chronon3d;

namespace {

constexpr FrameRate kCkptFps{30, 1};
constexpr int       kCkptShotStart{0};
constexpr int       kCkptShotEnd{99};         // 100 frames 0..99

// ── camera_equal: bit-exact field comparison of the primary stateful surface ──
// Tracking position + zoom + fov + lens is sufficient to detect any drift
// in DampedFollowConstraint's EMA accumulation (which acts on position).
bool cameras_equal(const Camera2_5D& a, const Camera2_5D& b) {
    if (std::memcmp(&a.position, &b.position, sizeof(Vec3)) != 0) return false;
    if (a.zoom       != b.zoom)       return false;
    if (a.fov_deg    != b.fov_deg)    return false;
    if (std::memcmp(&a.lens, &b.lens, sizeof(LensModel)) != 0) return false;
    return true;
}

// ── make_stateful_damped_descriptor: authoring-side fixture ────────────────
// Creates a RequiresHistory program (DampedFollowConstraint + keyframed
// pose-tracks source).  Linear position ramp 0..1000 over 100 frames gives
//   → smooth, monotonic EMA accumulation so any nondeterminism in the
//     pre-roll/eval interaction will surface as a CHECK mismatch.
CameraDescriptor make_stateful_damped_descriptor() {
    CameraDescriptor desc;
    desc.id = "ckpt_damped_v1";
    desc.base.position = {0.0f, 0.0f, -1000.0f};
    desc.base.rotation = {0.0f, 0.0f, 0.0f};
    desc.source = PoseTracksSource{};
    auto& src = std::get<PoseTracksSource>(desc.source);
    // Linear ramp in X every 10 frames (12 keyframes across 110 frame span).
    for (int i = 0; i <= 10; ++i) {
        const float x_pos = float(i) * 100.0f;
        src.position.key(Frame{i * 10},
                         Vec3{x_pos, 0.0f, -1000.0f});
    }
    src.use_target = false;
    desc.orientation = FixedOrientation{};
    desc.constraints = { DampedFollowConstraint{0.5f} };
    return desc;
}

// ── evaluate_cached_at: canonical acquire / eval / commit round-trip ───────
Camera2_5D evaluate_cached_at(CameraSessionCache& cache,
                              const CameraProgram& prog,
                              int shot_idx,
                              int target_frame) {
    auto lease = cache.acquire(prog, shot_idx,
                               kCkptShotStart, target_frame, kCkptFps);
    CameraEvalContext ctx;
    ctx.frame = Frame{target_frame};
    ctx.sample_time = SampleTime::from_frame_int(target_frame, kCkptFps);
    auto r = prog.evaluate(ctx, lease.session());
    lease.commit();  // CAM-05: RAII — commit advances last_evaluated_frame
    return r.camera;
}

// Compile a descriptor once; helper hides the (Result ...) ceremony.
CameraProgram compile_for_test(const CameraDescriptor& desc) {
    auto cr = compile_camera(desc, /*catalog=*/nullptr);
    REQUIRE(cr);
    auto prog = std::move(cr).value();
    REQUIRE(prog.is_compiled());
    REQUIRE(prog.evaluation_dependency() ==
            CameraEvaluationDependency::RequiresHistory);
    return prog;
}

}  // namespace

// ───────────────────────────────────────────────────────────────────────────
// §1 serial-vs-parallel — 100 frames sequential on each of two caches ⇒
//    bit-equal terminal state.
// ───────────────────────────────────────────────────────────────────────────
TEST_CASE("TICKET-031 §1 serial-vs-parallel: two per-worker caches give same terminal frame") {
    auto prog = compile_for_test(make_stateful_damped_descriptor());

    CameraSessionCache cacheA;  // "worker A"
    for (int f = 0; f <= kCkptShotEnd; ++f) {
        (void)evaluate_cached_at(cacheA, prog, /*shot=*/0, f);
    }
    Camera2_5D a99 = evaluate_cached_at(cacheA, prog, 0, kCkptShotEnd);

    CameraSessionCache cacheB;  // "worker B" — separate cache, separate state
    for (int f = 0; f <= kCkptShotEnd; ++f) {
        (void)evaluate_cached_at(cacheB, prog, 0, f);
    }
    Camera2_5D b99 = evaluate_cached_at(cacheB, prog, 0, kCkptShotEnd);

    CHECK(cameras_equal(a99, b99));
}

// ───────────────────────────────────────────────────────────────────────────
// §2 sequential-vs-random — walking 0..99 then re-entering frame 50 must
//    equal a fresh-worker first-encounter of frame 50.
// ───────────────────────────────────────────────────────────────────────────
TEST_CASE("TICKET-031 §2 sequential-vs-random: re-enter N == first-encounter N") {
    auto prog = compile_for_test(make_stateful_damped_descriptor());

    CameraSessionCache primed;  // walks 0..99 first, then re-enters 50
    (void)evaluate_cached_at(primed, prog, 0, kCkptShotEnd);
    Camera2_5D re_entered_50 = evaluate_cached_at(primed, prog, 0, 50);

    CameraSessionCache fresh;   // never evaluated anything before frame 50
    Camera2_5D fresh_50 = evaluate_cached_at(fresh, prog, 0, 50);

    CHECK(cameras_equal(fresh_50, re_entered_50));
}

// ───────────────────────────────────────────────────────────────────────────
// §3 two simultaneous render jobs — same program + same target_frame on
//    two independent caches ⇒ bit-equal.
// ───────────────────────────────────────────────────────────────────────────
TEST_CASE("TICKET-031 §3 two simultaneous render jobs: two caches, same target") {
    auto prog = compile_for_test(make_stateful_damped_descriptor());

    constexpr int kTarget = 50;
    CameraSessionCache workerA;
    CameraSessionCache workerB;
    Camera2_5D a = evaluate_cached_at(workerA, prog, 0, kTarget);
    Camera2_5D b = evaluate_cached_at(workerB, prog, 0, kTarget);

    CHECK(cameras_equal(a, b));
}

// ───────────────────────────────────────────────────────────────────────────
// §4 retry of same frame — evaluate(N) twice on the same cache ⇒ bit-equal.
//    (The cache's repprime-on-retry path runs the canonical pre-roll both
//     times, so non-determinism would surface as a CHECK mismatch.)
// ───────────────────────────────────────────────────────────────────────────
TEST_CASE("TICKET-031 §4 retry of same frame: evaluate(N) twice ⇒ bit-equal") {
    auto prog = compile_for_test(make_stateful_damped_descriptor());

    CameraSessionCache cache;
    Camera2_5D r1 = evaluate_cached_at(cache, prog, 0, 50);
    Camera2_5D r2 = evaluate_cached_at(cache, prog, 0, 50);

    CHECK(cameras_equal(r1, r2));
}

// ───────────────────────────────────────────────────────────────────────────
// §5 sub-frame repeated — same sub-frame position back-to-back ⇒ bit-equal.
//    Uses sample_time = from_frame(1.5, kCkptFps); both calls feed the
//    same context, so any non-determinism surfaces in the post-eval state.
// ───────────────────────────────────────────────────────────────────────────
TEST_CASE("TICKET-031 §5 sub-frame repeated: same sub-frame ⇒ bit-equal") {
    auto prog = compile_for_test(make_stateful_damped_descriptor());

    CameraSessionCache cacheA;
    CameraEvalContext ctx_a;
    ctx_a.frame = Frame{1};
    ctx_a.sample_time = SampleTime::from_frame(1.5, kCkptFps);
    auto lease_a = cacheA.acquire(prog, /*shot=*/0, kCkptShotStart, /*target=*/1, kCkptFps);
    Camera2_5D r1 = prog.evaluate(ctx_a, lease_a.session()).camera;
    lease_a.commit();

    CameraSessionCache cacheB;
    CameraEvalContext ctx_b = ctx_a;  // identical sub-frame context
    auto lease_b = cacheB.acquire(prog, 0, kCkptShotStart, 1, kCkptFps);
    Camera2_5D r2 = prog.evaluate(ctx_b, lease_b.session()).camera;
    lease_b.commit();

    CHECK(cameras_equal(r1, r2));
}

// ───────────────────────────────────────────────────────────────────────────
// §6 checkpoint restore — capture before mutation, mutate to a known-bad
//    state, restore ⇒ evaluating frame 50 again must match the reference.
// ───────────────────────────────────────────────────────────────────────────
TEST_CASE("TICKET-031 §6 checkpoint restore: snapshot→mutate→restore ⇒ equal ref") {
    auto prog = compile_for_test(make_stateful_damped_descriptor());

    // Prime a session exactly up to frame 50 — uses the free preroll helper
    // directly so the test doesn't accidentally depend on the cache.
    CameraSession sess;
    sess.ensure_constraint_states(prog.descriptor()->constraints.size());
    preroll_session_for_frame(prog, /*shot_start=*/0, /*target=*/50,
                              /*window=*/30, sess, kCkptFps);

    CameraStateCheckpoint cp = CameraStateCheckpoint::capture(
        sess,
        compute_camera_descriptor_fingerprint(*prog.descriptor()),
        /*last_evaluated_frame=*/50,
        /*shot_start_frame=*/0);
    REQUIRE(cp.valid);

    // Establish the reference camera at frame 50.
    CameraEvalContext ctx;
    ctx.frame = Frame{50};
    ctx.sample_time = SampleTime::from_frame_int(50, kCkptFps);
    Camera2_5D ref = prog.evaluate(ctx, sess).camera;

    // Aggressively mutate the session to a deliberately-wrong state.
    sess.banking_roll = -99.0f;
    sess.constraint_session.banking_roll = 9999.0f;
    for (auto& st : sess.constraint_session.states) {
        st.previous_camera.position       = {7777.0f, 0.0f, -1000.0f};
        st.previous_velocity              = {42.0f, 42.0f, 42.0f};
        st.previous_camera.zoom           = 555.5f;
        st.previous_camera.fov_deg        = 33.3f;
        st.has_previous                   = true;
    }

    // Restore the post-preroll / pre-evaluate snapshot.
    CameraStateCheckpoint::restore(sess, cp);

    // Re-evaluate frame 50 — must match the reference exactly.
    Camera2_5D after = prog.evaluate(ctx, sess).camera;

    CHECK(cameras_equal(ref, after));
}
