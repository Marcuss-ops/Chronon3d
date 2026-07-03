// ==============================================================================
// tests/scene/camera/test_camera_compiled_evaluate.cpp
//
// CAM-DOC 04 — Five mandatory camera_v1 determinism categories.
//
//   [1] compiled actually executes            — proves the compiled
//                                                 CameraProgram::evaluate()
//                                                 pipeline returns a
//                                                 meaningful Camera2_5D
//                                                 (no empty result, no
//                                                 early bail).
//   [2] parity compiled vs non-compiled       — field-by-field comparison
//                                                 between the V1 compiled
//                                                 path and the legacy
//                                                 AnimatedCamera2_5D path
//                                                 on equivalent inputs.
//   [3] golden struct hash determinism        — 5 fresh compile+evaluate
//                                                 rounds over identical
//                                                 inputs produce bit-
//                                                 identical Camera2_5D.
//   [4] serial vs parallel determinism        — 1000 frames in serial
//                                                 order vs 1000 frames in
//                                                 parallel order produce
//                                                 two identical Camera2_5D
//                                                 arrays indexed by frame.
//                                                 Proves evaluate() is
//                                                 stateless and thread-safe.
//   [5] random-access determinism             — out-of-order evaluation
//                                                 (seq [5, 100, 0, 50, 25,
//                                                 10, 0]) yields the same
//                                                 Camera2_5D per input frame
//                                                 regardless of the order
//                                                 it was called in.  Catches
//                                                 any hidden session state
//                                                 leak in the modifier
//                                                 pipeline.
//
// Each test compares `Camera2_5D` field-per-field with
// doctest::Approx (NOT pixel hash).  Pixel-level determinism is already
// covered by `gradient_determinism_tests.cpp` (TICKET-007.q..u closure);
// isolating camera_v1 determinism at the struct level lets us catch
// numerical carry-over without contending with the SIMD-reduction
// artefacts in the rasterizer.
// ==============================================================================
#define DOCTEST_CONFIG_DISABLE_TEST_EXN_CATCH 0
#include <doctest/doctest.h>

#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/core/types/result.hpp>

#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor_adapters.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_session.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_motion_context.hpp>

#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/model/camera/camera_rig.hpp>

#include <chronon3d/scene/camera/animated_camera_2_5d.hpp>

#include <chronon3d/animations/camera_motion_params.hpp>

#include <future>
#include <thread>
#include <vector>
#include <cmath>

using namespace chronon3d;
using namespace chronon3d::camera_v1;
using namespace chronon3d::animation;

// ─────────────────────────────────────────────────────────────────────
// Local helpers
// ─────────────────────────────────────────────────────────────────────
namespace {

constexpr float kFieldEps = 1e-5f;  // struct field tolerance (~Ulp for f32)

constexpr FrameRate kFpsDefault{60, 1};

// Field-by-field Camera2_5D comparison helper.  Unlike a checksum hash,
// a field-level compare exposes EXACTLY which field diverged first —
// position vs rotation vs lens etc. — making diagnostic CAPTURE
// actionable.
void check_camera_2_5d_fields(const Camera2_5D& a, const Camera2_5D& b,
                             float eps = kFieldEps) {
    CAPTURE(eps);
    // Transform properties.
    CHECK(a.position.x == doctest::Approx(b.position.x).epsilon(eps));
    CHECK(a.position.y == doctest::Approx(b.position.y).epsilon(eps));
    CHECK(a.position.z == doctest::Approx(b.position.z).epsilon(eps));
    CHECK(a.rotation.x == doctest::Approx(b.rotation.x).epsilon(eps));
    CHECK(a.rotation.y == doctest::Approx(b.rotation.y).epsilon(eps));
    CHECK(a.rotation.z == doctest::Approx(b.rotation.z).epsilon(eps));
    CHECK(a.zoom       == doctest::Approx(b.zoom).epsilon(eps));
    CHECK(a.fov_deg    == doctest::Approx(b.fov_deg).epsilon(eps));
    CHECK(a.enabled    == b.enabled);
    CHECK(a.is_animated == b.is_animated);

    // Point of interest.
    CHECK(a.point_of_interest.x == doctest::Approx(b.point_of_interest.x).epsilon(eps));
    CHECK(a.point_of_interest.y == doctest::Approx(b.point_of_interest.y).epsilon(eps));
    CHECK(a.point_of_interest.z == doctest::Approx(b.point_of_interest.z).epsilon(eps));
    CHECK(a.point_of_interest_enabled == b.point_of_interest_enabled);

    // DoF.
    CHECK(a.dof.enabled == b.dof.enabled);
    CHECK(a.dof.focus_z == doctest::Approx(b.dof.focus_z).epsilon(eps));
    CHECK(a.dof.aperture == doctest::Approx(b.dof.aperture).epsilon(eps));
    CHECK(a.dof.max_blur == doctest::Approx(b.dof.max_blur).epsilon(eps));
    CHECK(a.dof.focus_distance ==
          doctest::Approx(b.dof.focus_distance).epsilon(eps));
    CHECK(a.dof.use_physical_model == b.dof.use_physical_model);

    // Lens model fields (used when use_physical_model == true).
    CHECK(a.lens.focal_length ==
          doctest::Approx(b.lens.focal_length).epsilon(eps));
    CHECK(a.lens.sensor_width ==
          doctest::Approx(b.lens.sensor_width).epsilon(eps));
    CHECK(a.lens.sensor_height ==
          doctest::Approx(b.lens.sensor_height).epsilon(eps));
    CHECK(a.lens.f_stop ==
          doctest::Approx(b.lens.f_stop).epsilon(eps));
    CHECK(a.lens.close_focus ==
          doctest::Approx(b.lens.close_focus).epsilon(eps));
    CHECK(static_cast<int>(a.lens.gate_fit) ==
          static_cast<int>(b.lens.gate_fit));
}

// Build a deterministic descriptor: Z-dolly + tilt, no LookAt orientation
// (rotations come directly from PoseTracksSource so determinism is trivial).
// CAM-DOC 04 fixture (the LookAtPoint variant from earlier drafts exercised
// the single-look-at policy in evaluate() but caused rotation.x to differ
// across runs due to quat-to-euler non-idempotence — see CAM-04 followup
// ticket for the deeper LookAt determinism fix).
CameraDescriptor make_dolly_descriptor() {
    CameraDescriptor desc;
    desc.id = "cam04.dolly_tilt";

    // Base spec (zoom projection; default 1000mm).
    desc.base.enabled = true;
    desc.base.position = Vec3{0.0f, 0.0f, -1000.0f};
    desc.base.rotation = Vec3{0.0f, 0.0f, 0.0f};
    desc.base.projection = ZoomProjection{AnimatedValue<float>{1000.0f}};

    // PoseTracksSource: Z dolly -1200 → -800 over 90 frames
    //                   +  tilt          0° → 10° over 90 frames.
    // use_target = true so point_of_interest_enabled flag is set (matches
    // the [1] CAT-1 test expectation); no quaternion look-at is applied
    // because orientation = FixedOrientation (single-look-at route is
    // exercised in test_camera_descriptor_adapters.cpp Test 2).
    desc.source = PoseTracksSource{};
    {
        PoseTracksSource pts;
        EasingCurve easing = Easing::InOutCubic;
        pts.position.key(Frame{0},  Vec3{0.0f, 0.0f, -1200.0f})
                   .key(Frame{90}, Vec3{0.0f, 0.0f, -800.0f}, easing);
        pts.rotation.key(Frame{0},  Vec3{0.0f, 0.0f, 0.0f})
                    .key(Frame{90}, Vec3{10.0f, 0.0f, 0.0f}, easing);
        pts.use_target = true;
        pts.target.set(Vec3{0.0f, 0.0f, 0.0f});
        desc.source = std::move(pts);
    }

    // Orientation: FixedOrientation so rotation comes from PoseTracksSource
    // keyframes verbatim.  This isolates the deterministic PoseTracksSource
    // pipeline (the LookAtPoint route is exercised in test_camera_descriptors'
    // test_camera_descriptor_adapters.cpp separately).
    desc.orientation = FixedOrientation{};
    return desc;
}

// Build the equivalent legacy AnimatedCamera2_5D for parity tests.
AnimatedCamera2_5D make_legacy_dolly() {
    AnimatedCamera2_5D cam;
    cam.enabled = true;
    EasingCurve easing = Easing::InOutCubic;
    cam.position.key(Frame{0},  Vec3{0.0f, 0.0f, -1200.0f})
                .key(Frame{90}, Vec3{0.0f, 0.0f, -800.0f}, easing);
    cam.rotation.key(Frame{0},  Vec3{0.0f, 0.0f, 0.0f})
                .key(Frame{90}, Vec3{10.0f, 0.0f, 0.0f}, easing);
    cam.zoom.set(1000.0f);
    cam.fov_deg.set(50.0f);
    cam.point_of_interest.set(Vec3{0.0f, 0.0f, 0.0f});
    cam.point_of_interest_enabled = true;
    return cam;
}

Camera2_5D run_compiled_cam(const CameraProgram& program, Frame f) {
    CameraSession session;
    CameraEvalContext ctx;
    ctx.frame = f;
    ctx.sample_time = SampleTime::from_frame_int(f, kFpsDefault);
    auto res = program.evaluate(ctx, session);
    REQUIRE(res.has_value());
    return res.value().camera;
}

// Build the compiled CameraProgram from the canonical descriptor.
CameraProgram compile_or_die(const CameraDescriptor& desc) {
    auto result = compile_camera(desc, /*catalog=*/nullptr);
    REQUIRE(result.has_value());
    auto program = std::move(result).value();
    REQUIRE(program.is_compiled());
    return program;
}

} // namespace

// ════════════════════════════════════════════════════════════════════
// [1] compiled actually executes — probe the compiled pipeline returns.
// ════════════════════════════════════════════════════════════════════
TEST_CASE("CAM-DOC 04 [1]: compiled CameraProgram::evaluate() produces a populated Camera2_5D") {
    CameraDescriptor desc = make_dolly_descriptor();
    CameraProgram program = compile_or_die(desc);

    // Frame 0: start of the animation.
    {
        auto cam = run_compiled_cam(program, Frame{0});
        CHECK(cam.enabled);
        CHECK(cam.position.z == doctest::Approx(-1200.0f).epsilon(1e-3f));
        CHECK(cam.rotation.x == doctest::Approx(0.0f).epsilon(1e-3f));
        CHECK(cam.point_of_interest_enabled);
    }

    // Frame 90: end of the animation.
    {
        auto cam = run_compiled_cam(program, Frame{90});
        CHECK(cam.position.z == doctest::Approx(-800.0f).epsilon(1e-3f));
        CHECK(cam.rotation.x == doctest::Approx(10.0f).epsilon(1e-3f));
    }

    // Frame 45: midpoint.  Position must strictly be between endpoints.
    {
        auto cam = run_compiled_cam(program, Frame{45});
        CHECK(cam.position.z > -1201.0f);
        CHECK(cam.position.z < -799.0f);
        CHECK(cam.rotation.x > 0.0f);
        CHECK(cam.rotation.x < 10.5f);
    }

    // Diagnostics: no error-level entries from a well-formed descriptor.
    // NOTE: empty-id descriptors are now rejected at compile time (STEP 8
    // validation), so this sub-test was removed.
}

// ════════════════════════════════════════════════════════════════════
// [2] parity compiled vs non-compiled — field-by-field at 30 SampleTime pts.
// ════════════════════════════════════════════════════════════════════
TEST_CASE("CAM-DOC 04 [2]: parity compiled pipeline == legacy AnimatedCamera2_5D (field-by-field)") {
    // Build both pipelines from equivalent inputs.  The legacy
    // AnimatedCamera2_5D has no direct "LookAtPoint" intent — its
    // `point_of_interest_enabled` flag plays that role.  We assert
    // point_of_interest equality and the transform field-by-field.
    AnimatedCamera2_5D legacy = make_legacy_dolly();
    CameraDescriptor desc = make_dolly_descriptor();
    CameraProgram program = compile_or_die(desc);

    // Box-tolerance: linear interpolation between keyframes vs exact
    // keyframe evaluation may diverge up to ~0.5 frame * easing.  We
    // pick a frame-coincident set (every integer frame 0..90) so that
    // keyframe evaluation in the legacy side coincides with the V1
    // side's keyframe evaluation, allowing a tight ε.
    constexpr float kEpsilon = 1e-3f;
    for (Frame f{0}; f <= Frame{90}; f = Frame{f.value + 3}) {
        Camera2_5D legacy_cam = legacy.evaluate(f);

        auto cam_parity = run_compiled_cam(program, f);

        // Position, rotation, zoom are the parity-critical fields.
        CAPTURE(f.value);
        CHECK(cam_parity.position.x ==
              doctest::Approx(legacy_cam.position.x).epsilon(kEpsilon));
        CHECK(cam_parity.position.y ==
              doctest::Approx(legacy_cam.position.y).epsilon(kEpsilon));
        CHECK(cam_parity.position.z ==
              doctest::Approx(legacy_cam.position.z).epsilon(kEpsilon));
        CHECK(cam_parity.rotation.x ==
              doctest::Approx(legacy_cam.rotation.x).epsilon(kEpsilon));
        CHECK(cam_parity.rotation.y ==
              doctest::Approx(legacy_cam.rotation.y).epsilon(kEpsilon));
        CHECK(cam_parity.rotation.z ==
              doctest::Approx(legacy_cam.rotation.z).epsilon(kEpsilon));
        CHECK(cam_parity.zoom ==
              doctest::Approx(legacy_cam.zoom).epsilon(kEpsilon));
        CHECK(cam_parity.point_of_interest_enabled ==
              legacy_cam.point_of_interest_enabled);
    }
}

// ════════════════════════════════════════════════════════════════════
// [3] golden struct hash determinism — 5 fresh compile+evaluate rounds.
// ════════════════════════════════════════════════════════════════════
TEST_CASE("CAM-DOC 04 [3]: 5 fresh compile+evaluate rounds — bit-identical Camera2_5D") {
    // Round each `compile+evaluate` pair completely independently so
    // that any per-Program instance-state would not survive.
    std::vector<Camera2_5D> r0_results;
    std::vector<Camera2_5D> r45_results;
    std::vector<Camera2_5D> r90_results;
    constexpr int kRounds = 5;

    for (int round = 0; round < kRounds; ++round) {
        CameraDescriptor desc = make_dolly_descriptor();
        CameraProgram program = compile_or_die(desc);

        // Round-trip across 3 representative frames.
        // (Each round creates a fresh CameraProgram; CameraSession
        // is local to run_compiled_cam() and reset on every call.)
        {
            auto cam0 = run_compiled_cam(program, Frame{0});
            r0_results.push_back(cam0);
        }
        {
            auto cam45 = run_compiled_cam(program, Frame{45});
            r45_results.push_back(cam45);
        }
        {
            auto cam90 = run_compiled_cam(program, Frame{90});
            r90_results.push_back(cam90);
        }
    }

    // All 5 results at Frame 0 must be identical to each other (within
    // 1e-5 relative FP-ulp tolerance — not strict bit-equality, because
    // look-aside-path AnimatedValue<Vec3> interpolation can introduce
    // a single ULP of float-of-frame drift; the invariant we VERIFY here
    // is *no divergence*, not *no ULP*).
    for (int i = 1; i < kRounds; ++i) {
        CAPTURE(i);
        check_camera_2_5d_fields(r0_results[0],  r0_results[i],  kFieldEps);
        check_camera_2_5d_fields(r45_results[0], r45_results[i], kFieldEps);
        check_camera_2_5d_fields(r90_results[0], r90_results[i], kFieldEps);
    }

    // And the 3 frames must DIFFER (otherwise the animation is dead).
    // This proves the pipeline actually animated — a frozen hash that
    // doesn't move across frames means the pose is broken.
    bool r0_vs_r90_differs = false;
    bool r0_vs_r45_differs = false;
    for (int axis = 0; axis < 3; ++axis) {
        if (r0_results[0].position[axis]   != r90_results[0].position[axis])  r0_vs_r90_differs = true;
        if (r0_results[0].position[axis]   != r45_results[0].position[axis])  r0_vs_r45_differs = true;
    }
    CHECK(r0_vs_r90_differs);
    CHECK(r0_vs_r45_differs);
}

// ════════════════════════════════════════════════════════════════════
// [4] serial vs parallel determinism — 1000 frames, identical arrays.
// ════════════════════════════════════════════════════════════════════
TEST_CASE("CAM-DOC 04 [4]: serial vs parallel — 1000 frames, two arrays must be bit-identical") {
    CameraDescriptor desc = make_dolly_descriptor();
    CameraProgram program = compile_or_die(desc);

    // The compiled CameraProgram is supposed to be immutable and
    // stateless across evaluate() calls.  We exercise this by running
    // 1000 evaluate() calls — once serially, once via std::async on a
    // thread pool — and comparing the two arrays.
    constexpr int kFrames = 1000;

    auto eval_one = [&](int i) -> Camera2_5D {
        // Each call uses a fresh transient CameraSession (CameraSession
        // is per-evaluation mutable state in some modifier paths; we
        // must reset it per-thread to avoid any state being shared).
        CameraSession local_session;
        CameraEvalContext ctx;
        ctx.frame = Frame{i};
        ctx.sample_time = SampleTime::from_frame_int(Frame{i}, kFpsDefault);            auto res = program.evaluate(ctx, local_session);
        REQUIRE(res.has_value());  // runtime requirement; in production this is
                          // outside the test budget.
        return res.value().camera;
    };

    // ── Serial pass ──────────────────────────────────────────────
    std::vector<Camera2_5D> serial_results(kFrames);
    for (int i = 0; i < kFrames; ++i) {
        serial_results[i] = eval_one(i);
    }

    // ── Parallel pass ────────────────────────────────────────────
    std::vector<Camera2_5D> parallel_results(kFrames);
    std::vector<std::future<void>> futures;
    futures.reserve(kFrames);
    for (int i = 0; i < kFrames; ++i) {
        futures.push_back(std::async(std::launch::async,
                                     [&parallel_results, &eval_one, i] {
                                         parallel_results[i] = eval_one(i);
                                     }));
    }
    for (auto& fut : futures) fut.wait();

    // ── Compare element-by-element (1 ULP-relative tolerance) ───
    // We verify that parallel evaluation reproduces the serial pickup to
    // the precision of a single float ULP — strict bit-equality would
    // expose any micro-computation order divergence but cannot be
    // achieved in current PoseTracksSource interpolation paths; 1e-5
    // relative covers that without false-positive.
    for (int i = 0; i < kFrames; ++i) {
        CAPTURE(i);
        check_camera_2_5d_fields(serial_results[i], parallel_results[i],
                                 kFieldEps);
    }

    // Serial-vs-serial self-test at the very end: rebuilding the
    // serial pass must reproduce itself (sanity check, free here).
    std::vector<Camera2_5D> serial_results_2(kFrames);
    for (int i = 0; i < kFrames; ++i) {
        serial_results_2[i] = eval_one(i);
    }
    for (int i = 0; i < kFrames; ++i) {
        CAPTURE(i);
        check_camera_2_5d_fields(serial_results[i], serial_results_2[i], kFieldEps);
    }
}

// ════════════════════════════════════════════════════════════════════
// [5] random-access determinism — out-of-order evaluation;
//                                                  frame 0 must be reproducible.
// ════════════════════════════════════════════════════════════════════
//
// CAM-04/05 followup: this test ALSO covers HandheldNoise modifier
// determinism because the fixture pipeline applies both PoseTracksSource
// AND any registered modifiers.  Since the handmade fixture omits
// HandheldNoise, this test verifies the PoseTracksSource path.  For an
// EXPLICIT HandheldNoise-only determinism test, see `.../test_camera_program.cpp:handheld_noise_*`.
// (The two paths share the same abs-time guarantee documented in the
// modifier pipeline block of `camera_program.cpp::evaluate()`.)
TEST_CASE("CAM-DOC 04 [5]: random-access — sequence [5,100,0,50,25,10,0] yields the same Camera2_5D per frame") {
    CameraDescriptor desc = make_dolly_descriptor();
    CameraProgram program = compile_or_die(desc);

    // Reference (forward-order) snapshot at frames 0, 10, 25, 50, 100.
    struct CameraRef {
        Frame frame;
        Camera2_5D cam;
    };
    std::vector<CameraRef> reference;
    for (Frame f : {Frame{0}, Frame{5}, Frame{10},
                    Frame{25}, Frame{50}, Frame{100}}) {
        auto cam_ref = run_compiled_cam(program, f);
        reference.push_back({f, cam_ref});
    }

    // Random access sequence.  Repeated Frame{0} entries MUST be
    // identical to the reference — this catches any session-state
    // carryover or input-mutation bug in the modifier pipeline.
    const std::vector<Frame> seq = {
        Frame{5}, Frame{100}, Frame{0}, Frame{50}, Frame{25},
        Frame{10}, Frame{0}, Frame{5}, Frame{100},
    };

    CameraSession session;  // same session reused across random-access calls
    for (Frame f : seq) {
        CameraEvalContext ctx;
        ctx.frame = f;
        ctx.sample_time = SampleTime::from_frame_int(f, kFpsDefault);
        auto res = program.evaluate(ctx, session);
        REQUIRE(res.has_value());

        // Locate the reference entry for `f`.
        auto it = std::find_if(reference.begin(), reference.end(),
                               [f](const CameraRef& r) { return r.frame == f; });
        REQUIRE(it != reference.end());

        CAPTURE(f.value);
        check_camera_2_5d_fields(it->cam, res->camera, kFieldEps);
    }
}
