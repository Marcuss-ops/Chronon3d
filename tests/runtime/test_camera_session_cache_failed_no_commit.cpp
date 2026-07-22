// ==============================================================================
// tests/runtime/test_camera_session_cache_failed_no_commit.cpp
//
// TICKET-A3-CACHE-LEASE (Agent3 mission DoD gate (d)) — failed evaluation
// does NOT mutate the CameraSessionCache.
//
// Locks the contract documented at `CameraSessionCache::acquire` + the
// existing CAM-05 sentinel in camera_session_cache.cpp:
//
//   1.  acquire() never writes `last_evaluated_frame` directly.  The
//       `must_reprime` branch writes other fields (session reset, fingerprint,
//       shot_start, cut_seen = false) but explicitly leaves the
//       `last_evaluated_frame` sentinel untouched.
//   2.  commit() is the SOLE writepoint for `last_evaluated_frame` (via
//       `CameraSessionCache::commit_lease`).  A failed evaluation
//       (result.ok == false) MUST NOT call lease.commit() — and even a
//       successful evaluation MUST call lease.commit() explicitly to
//       advance the cache fingerprint.
//   3.  `CameraSessionLease::~CameraSessionLease()` does nothing if
//       `committed_` is false: state modifications in-flight on the
//       session pointer are POTENTIALLY retained via pointer aliasing
//       (the lease points directly at `checkpoint.session`), but the
//       public CONTRACT for the cache is `last_evaluated_frame` is
//       controlled by commit() alone — and that's what this test
//       asserts (gate (d)'s discriminating observation).
//
// Companion files:
//   - src/scene/camera/camera_v1/camera_session_cache.cpp   — acquire()
//                                                             + commit_lease()
//                                                             + lease destructor
//   - include/chronon3d/scene/camera/camera_v1/camera_session_cache.hpp
//   - include/chronon3d/scene/camera/camera_v1/camera_session.hpp
//                     (CameraSession::reset clears last_valid_camera, etc.)
//
// SUBCASE breakdown:
//
//   A.  gate (d) primary:  acquire + failed eval + NO commit()
//                         ⇒ cache.find(shot_idx)->last_evaluated_frame
//                           stays at -1 (Checkpoint default-init sentinel).
//
//   B.  positive control:  acquire + successful eval + commit()
//                         ⇒ cache.find(shot_idx)->last_evaluated_frame
//                           advances to target_frame.
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

constexpr FrameRate kLeaseFps{60, 1};

// Fixture: a descriptor that ALWAYS fails constraint evaluation.
// position == POI ⇒ DistanceConstraint check returns "distance-zero".
// `policy` controls the fallback branch under test (Stop for SUBCASE A;
//   Stop also fine for SUBCASE B as long as a non-constraint evaluator
//   path returns ok=true — for SUBCASE B we use SkipFailedConstraint.)
CameraDescriptor make_always_failing_desc(const std::string& id_str,
                                            CameraFailurePolicy policy) {
    CameraDescriptor desc;
    desc.id = id_str;
    desc.base.enabled = true;
    desc.base.position = Vec3{1.0f, 2.0f, 3.0f};
    desc.base.rotation = Vec3{0.0f, 0.0f, 0.0f};
    desc.base.point_of_interest_enabled = true;
    desc.base.point_of_interest = Vec3{1.0f, 2.0f, 3.0f};
    desc.base.projection = ZoomProjection{AnimatedValue<float>{1000.0f}};
    desc.orientation = FixedOrientation{};
    desc.source = StaticCameraSource{};
    desc.constraints.push_back(
        DistanceConstraint{/*min_distance=*/10.0f,
                           /*max_distance=*/1000.0f});
    desc.failure_policy = policy;
    return desc;
}

} // namespace

// =============================================================================
// SUBCASE A — Gate-(d) primary: failed eval + no commit leaves cache unchanged.
// =============================================================================
TEST_CASE("runtime_camera_session_cache_failed_eval_does_not_mutate_cache — "
          "TICKET-A3-CACHE-LEASE DoD gate (d): after acquire() primes the "
          "entry and the subsequent evaluate() returns !ok under Stop policy, "
          "the cache's `last_evaluated_frame` sentinel MUST stay at the "
          "default-init value -1 because the lease was destroyed without "
          "commit().  This locks the contract that commit() is the SOLE "
          "writepoint for last_evaluated_frame.") {

    SUBCASE("A. failed eval + no commit → last_evaluated_frame stays at default sentinel") {
        CameraSessionCache cache;
        CameraDescriptor desc = make_always_failing_desc(
            "test.runtime.lease.failed", CameraFailurePolicy::Stop);
        auto comp = compile_camera(desc);
        REQUIRE(comp.has_value());
        auto program = std::move(comp).value();
        REQUIRE(program.is_compiled());

        const int kShotIdx    = 0;
        const int kShotStart  = 0;
        const int kTargetFrame = 10;

        // Acquire → must-reprime path (entry.valid is false initially, so
        // forward_step is false; the reprime primes session + fingerprint
        // but leaves last_evaluated_frame untouched per CAM-05).
        {
            auto lease = cache.acquire(program, kShotIdx, kShotStart,
                                         kTargetFrame, kLeaseFps);

            // Mid-lease sanity: cache entry exists, last_evaluated_frame
            // is still the default-init sentinel (-1).
            auto* cp_mid = cache.find(kShotIdx);
            REQUIRE(cp_mid != nullptr);
            CHECK(cp_mid->last_evaluated_frame == -1);

            // Evaluate: position == POI ⇒ distance-zero + Stop ⇒ ok=false.
            CameraEvalContext ctx;
            ctx = ctx.with_frame(Frame{kTargetFrame}, kLeaseFps);
            ctx.sample_time = SampleTime::from_frame_int(Frame{kTargetFrame},
                                                          kLeaseFps);
            auto res = program.evaluate(ctx, lease.session());
            REQUIRE_FALSE(res.has_value());  // gate (d) feature: failed eval.

            // Explicit NO commit() — only path that takes advantage of the
            // lease's destructor's discard-state contract.
        }
        // Lease destroyed here without commit().  Post-destruction check:
        // cache MUST be untouched (last_evaluated_frame still -1).
        auto* cp_post = cache.find(kShotIdx);
        REQUIRE(cp_post != nullptr);
        CHECK(cp_post->last_evaluated_frame == -1);  // gate (d) PRIMARY lock.
    }

    // =================================================================
    // SUBCASE B — Positive control: commit() advances last_evaluated_frame.
    // =================================================================
    SUBCASE("B. successful eval + commit → last_evaluated_frame advances") {
        CameraSessionCache cache;
        // SkipFailedConstraint policy: failing DistanceConstraint is
        // skipped, eval returns ok=true with a Warning diag (matching
        // `compiled_failure_policy_skip_failed` in test_camera_program_compiled.cpp).
        CameraDescriptor desc = make_always_failing_desc(
            "test.runtime.lease.commit", CameraFailurePolicy::SkipFailedConstraint);
        auto comp = compile_camera(desc);
        REQUIRE(comp.has_value());
        auto program = std::move(comp).value();
        REQUIRE(program.is_compiled());

        const int kShotIdx    = 0;
        const int kShotStart  = 0;
        const int kTargetFrame = 17;

        {
            auto lease = cache.acquire(program, kShotIdx, kShotStart,
                                         kTargetFrame, kLeaseFps);

            // Pre-commit: still default sentinel.
            auto* cp_pre = cache.find(kShotIdx);
            REQUIRE(cp_pre != nullptr);
            CHECK(cp_pre->last_evaluated_frame == -1);

            // Evaluate: SkipFailedConstraint ⇒ ok=true with Warning diag.
            CameraEvalContext ctx;
            ctx = ctx.with_frame(Frame{kTargetFrame}, kLeaseFps);
            ctx.sample_time = SampleTime::from_frame_int(Frame{kTargetFrame},
                                                          kLeaseFps);
            auto res = program.evaluate(ctx, lease.session());
            REQUIRE(res.has_value());
            REQUIRE_FALSE(res->diagnostics.empty());  // Warning diag emitted.

            // Explicit commit() — sole writepoint for last_evaluated_frame.
            lease.commit();
        }
        // Post-commit + post-destruction: last_evaluated_frame advanced.
        auto* cp_post = cache.find(kShotIdx);
        REQUIRE(cp_post != nullptr);
        CHECK(cp_post->last_evaluated_frame == kTargetFrame);  // positive control.
    }
}
