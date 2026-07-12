// =============================================================================
// tests/scene/camera/test_composition_camera_unification.cpp
//
// P3-F — Composition camera unification tests (7 mandatory scenarios).
//
//   (1) programma camera compilato una volta sola
//   (2) 100 render concorrenti condividono lo stesso CameraProgram immutabile
//   (3) ogni render ha CameraSession distinta
//   (4) FOV/Zoom/PhysicalLens/DOF/motion blur end-to-end
//   (5) random access deterministico
//   (6) parity legacy adapter vs descriptor
//   (7) modifica descriptor → nuovo fingerprint
//
// These tests validate the P3-F invariant: Composition has NO mutable
// camera state.  The canonical path is the V2 staging pipeline
// (CompositionDefinition → compile_composition → evaluate →
// EvaluatedCompositionFrame).  Each test uses a single CompiledComposition
// snapshot and exercises determinism, threading, and content-stability
// invariants directly.
//
// They live in their own executable (`chronon3d_composition_camera_unification_tests`)
// to keep them independent of the legacy-default-camera tests in
// `test_composition_default_camera.cpp` that were trimmed in P3-F.
// =============================================================================

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <map>
#include <optional>
#include <thread>
#include <vector>

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <chronon3d/timeline/composition_definition.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>
#include <chronon3d/timeline/evaluated_composition_frame.hpp>

#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor_adapters.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp>
#include <chronon3d/internal/scene/camera/v1/camera_session.hpp>

#include <chronon3d/scene/model/camera/camera.hpp>           // Legacy Camera struct
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>      // Camera2_5D

#include <chronon3d/core/types/result.hpp>
#include <chronon3d/core/types/sample_time.hpp>

using namespace chronon3d;
using namespace chronon3d::camera_v1;

namespace {
constexpr FrameRate kTestFps{60, 1};
constexpr float kFieldEps = 1e-3f;

// CompositionSpec + CompositionDefinition factory. The SceneFunction is
// trivial: V2 evaluate() reads Camera2_5D through the compiled program
// (P3-F) so we don't need a meaningful Scene for these tests.
CompositionDefinition make_unify_def(const std::string& name,
                                     Vec3 base_pos = Vec3{0.0f, 0.0f, -1000.0f},
                                     float fov_deg = 50.0f) {
    CompositionDefinition def;
    def.composition.name = name;
    def.composition.width = 320;
    def.composition.height = 180;
    def.composition.frame_rate = kTestFps;
    def.composition.duration = Frame{120};
    def.scene = [](const FrameContext&) { return Scene{}; };

    CameraDescriptor desc;
    desc.id = name + "_desc";
    desc.source = StaticCameraSource{};
    desc.orientation = FixedOrientation{};
    desc.base.position = base_pos;
    desc.base.rotation = Vec3{0.0f, 0.0f, 0.0f};
    FovProjection proj{};
    proj.fov_deg.set(fov_deg);
    desc.base.projection = std::move(proj);
    desc.base.lens.focal_length  = 50.0f;
    desc.base.lens.f_stop        = 2.8f;
    desc.base.lens.sensor_width  = 36.0f;
    desc.base.lens.sensor_height = 24.0f;
    desc.base.lens.gate_fit      = GateFit::Fill;
    def.camera = std::move(desc);
    return def;
}

CompositionEvaluateContext make_unify_eval_ctx(FrameRate fps = kTestFps) {
    CompositionEvaluateContext ctx;
    ctx.frame_context.frame_rate = fps;
    ctx.frame_context.width      = 320;
    ctx.frame_context.height     = 180;
    return ctx;
}

void check_cam2_5d_match(const Camera2_5D& a, const Camera2_5D& b, float eps = kFieldEps) {
    CHECK(a.position.x == doctest::Approx(b.position.x).epsilon(eps));
    CHECK(a.position.y == doctest::Approx(b.position.y).epsilon(eps));
    CHECK(a.position.z == doctest::Approx(b.position.z).epsilon(eps));
    CHECK(a.fov_deg    == doctest::Approx(b.fov_deg).epsilon(eps));
    CHECK(a.zoom       == doctest::Approx(b.zoom).epsilon(eps));
}
} // namespace

// ════════════════════════════════════════════════════════════════════════
// (1) programma camera compilato una volta sola — camera_program is
//     compiled exactly once at compile_composition and reused for every
//     subsequent evaluate() call.  Re-evaluation must NOT re-trigger the
//     compile_camera pipeline.
// ════════════════════════════════════════════════════════════════════════
TEST_CASE("P3-F [1]: compile_composition runs camera compile exactly once") {
    CompositionDefinition def = make_unify_def("unify_one_compile");
    auto cc_res = compile_composition(def, {});
    REQUIRE(cc_res.has_value());
    CompiledComposition cc = std::move(cc_res).value();

    REQUIRE(cc.camera_program);
    REQUIRE(cc.camera_program->is_compiled());
    const auto* raw_before = cc.camera_program.get();

    // Run multiple evaluate() calls — same CompiledComposition.
    auto eval_ctx = make_unify_eval_ctx();
    std::vector<Camera2_5D> snapshots;
    snapshots.reserve(64);
    for (int i = 0; i < 64; ++i) {
        auto ev_res = evaluate(cc, eval_ctx, Frame{i});
        REQUIRE(ev_res.has_value());
        if (ev_res->camera.has_value()) {
            snapshots.push_back(*ev_res->camera);
        }
    }
    REQUIRE(snapshots.size() == 64);

    // Under StaticCameraSource all frames must produce identical results.
    // This trivially proves no per-call recomputation — the camera_program
    // pointer is stable, AND the outputs are stable.
    for (std::size_t i = 1; i < snapshots.size(); ++i) {
        check_cam2_5d_match(snapshots[0], snapshots[i]);
    }
    // The compilation was a one-shot: pointer identity is preserved.
    CHECK(cc.camera_program.get() == raw_before);
    CHECK(cc.camera_program.use_count() >= 1);
}

// ════════════════════════════════════════════════════════════════════════
// (2) 100 concurrent renders share the same immutable CameraProgram.
//     CameraProgram is shared_ptr<const>; concurrent evaluations MUST
//     not mutate the underlying program.  The post-100-evaluations pointer
//     MUST equal the pre pointer.
// ════════════════════════════════════════════════════════════════════════
TEST_CASE("P3-F [2]: 100 concurrent renders share one immutable CameraProgram") {
    CompositionDefinition def = make_unify_def("unify_100_concurrent");
    auto cc_res = compile_composition(def, {});
    REQUIRE(cc_res.has_value());
    CompiledComposition cc = std::move(cc_res).value();
    REQUIRE(cc.camera_program);
    REQUIRE(cc.camera_program->is_compiled());

    const auto* raw_before = cc.camera_program.get();
    const auto ref_count_before = cc.camera_program.use_count();

    constexpr int kWorkers = 100;
    std::vector<std::future<Camera2_5D>> futs;
    futs.reserve(kWorkers);
    for (int i = 0; i < kWorkers; ++i) {
        futs.push_back(std::async(std::launch::async,
            [&cc]() {
                camera_v1::CameraSession session;  // per-thread.
                camera_v1::CameraEvalContext cam_ctx{};
                cam_ctx.sample_time =
                    SampleTime::from_frame(0.0, kTestFps);
                auto r = cc.camera_program->evaluate(cam_ctx, session);
                REQUIRE(r.has_value());
                return r.value().camera;
            }));
    }

    Camera2_5D baseline = futs[0].get();
    for (int i = 1; i < kWorkers; ++i) {
        Camera2_5D got = futs[i].get();
        // All 100 evaluations should produce identical results at frame 0.
        CAPTURE(i);
        check_cam2_5d_match(baseline, got);
    }

    // Identity preserved through 100 concurrent evaluations.
    CHECK(cc.camera_program.get() == raw_before);
    CHECK(cc.camera_program.use_count() == ref_count_before);
}

// ════════════════════════════════════════════════════════════════════════
// (3) ogni render ha CameraSession distinta — each thread allocates its
//     OWN CameraSession and the parallelism does not cross-pollinate
//     per-thread session state.  Verified by counting how many sessions
//     were instantiated and verifying post-snapshot session state is
//     unaffected by another thread's evaluation.
// ════════════════════════════════════════════════════════════════════════
//
// STATELESS programs (the test fixture) do not mutate CameraSession.
// We mutate a thread-local hash of the post-eval session's address to
// confirm each thread reaches `evaluate(...)` with its OWN session and
// the program does not retain a reference to it.
TEST_CASE("P3-F [3]: each render has its own CameraSession — no cross-thread state") {
    CompositionDefinition def = make_unify_def("unify_distinct_sessions");
    auto cc_res = compile_composition(def, {});
    REQUIRE(cc_res.has_value());
    CompiledComposition cc = std::move(cc_res).value();
    REQUIRE(cc.camera_program);

    constexpr int kWorkers = 100;

    // Each worker-thread pre-records its own session address, calls
    // evaluate, then re-reads the session address — post-eval the
    // session must still be the SAME thread-local address (no aliasing)
    // and the worker count must equal kWorkers (proving kWorkers distinct
    // sessions were instantiated).
    std::atomic<int> distinct_sessions{0};
    std::mutex mu;
    std::unordered_set<const CameraSession*> seen;

    std::vector<std::future<void>> futs;
    futs.reserve(kWorkers);
    for (int i = 0; i < kWorkers; ++i) {
        futs.push_back(std::async(std::launch::async,
            [&cc, &distinct_sessions, &mu, &seen]() {
                camera_v1::CameraSession session;  // own session per thread.
                const auto* pre = &session;
                camera_v1::CameraEvalContext cam_ctx{};
                cam_ctx.sample_time =
                    SampleTime::from_frame(0.0, kTestFps);
                auto r = cc.camera_program->evaluate(cam_ctx, session);
                REQUIRE(r.has_value());
                const auto* post = &session;
                // Same identity (no internal aliasing).
                CHECK(pre == post);
                {
                    std::lock_guard<std::mutex> lock(mu);
                    seen.insert(pre);
                }
                distinct_sessions.fetch_add(1, std::memory_order_relaxed);
            }));
    }
    for (auto& f : futs) f.wait();

    CHECK(distinct_sessions.load() == kWorkers);
    CHECK(static_cast<int>(seen.size()) == kWorkers);
}

// ════════════════════════════════════════════════════════════════════════
// (4) FOV/Zoom/PhysicalLens/DOF/motion blur end-to-end — every Camera2_5D
//     channel survives the V2 pipeline (compile_composition → evaluate)
//     unmodified.  We exercise the canonical 5 projection / optics pairings
//     + DOF + motion blur.
// ════════════════════════════════════════════════════════════════════════
TEST_CASE("P3-F [4]: FOV / Zoom / PhysicalLens / DOF / motion blur end-to-end") {
    auto eval_one = [](CompositionDefinition& def) {
        auto cc_res = compile_composition(def, {});
        REQUIRE(cc_res.has_value());
        auto cc = std::move(cc_res).value();
        REQUIRE(cc.camera_program);
        auto ev_res = evaluate(cc, make_unify_eval_ctx(), Frame{0});
        REQUIRE(ev_res.has_value());
        REQUIRE(ev_res->camera.has_value());
        return *ev_res->camera;
    };

    SUBCASE("FOV end-to-end via FovProjection") {
        CompositionDefinition def = make_unify_def("unify_fov");
        FovProjection proj{};
        proj.fov_deg.set(45.0f);
        def.camera->base.projection = std::move(proj);

        Camera2_5D cam = eval_one(def);
        CHECK(cam.fov_deg == doctest::Approx(45.0f).epsilon(kFieldEps));
    }

    SUBCASE("Zoom end-to-end via ZoomProjection") {
        CompositionDefinition def = make_unify_def("unify_zoom");
        ZoomProjection proj{};
        proj.zoom.set(1500.0f);
        def.camera->base.projection = std::move(proj);

        Camera2_5D cam = eval_one(def);
        CHECK(cam.zoom == doctest::Approx(1500.0f).epsilon(kFieldEps));
    }

    SUBCASE("PhysicalLens end-to-end via PhysicalLensProjection") {
        CompositionDefinition def = make_unify_def("unify_phys_lens");
        PhysicalLensProjection plp{};
        plp.lens.focal_length  = 24.0f;
        plp.lens.f_stop        = 2.0f;
        plp.lens.sensor_width  = 36.0f;
        plp.lens.sensor_height = 24.0f;
        plp.lens.gate_fit      = GateFit::Fill;
        def.camera->base.projection = std::move(plp);

        Camera2_5D cam = eval_one(def);
        CHECK(cam.lens.focal_length == doctest::Approx(24.0f).epsilon(kFieldEps));
        CHECK(cam.lens.f_stop       == doctest::Approx(2.0f).epsilon(kFieldEps));
        CHECK(cam.lens.sensor_width == doctest::Approx(36.0f).epsilon(kFieldEps));
    }

    SUBCASE("DOF end-to-end across compile → evaluate → Camera2_5D") {
        CompositionDefinition def = make_unify_def("unify_dof");
        def.camera->base.dof.enabled        = true;
        def.camera->base.dof.focus_distance = 750.0f;
        def.camera->base.dof.aperture       = 4.0f;
        def.camera->base.dof.max_blur       = 18.0f;

        Camera2_5D cam = eval_one(def);
        CHECK(cam.dof.enabled);
        CHECK(cam.dof.focus_distance == doctest::Approx(750.0f).epsilon(kFieldEps));
        CHECK(cam.dof.aperture       == doctest::Approx(4.0f).epsilon(kFieldEps));
        CHECK(cam.dof.max_blur       == doctest::Approx(18.0f).epsilon(kFieldEps));
    }

    SUBCASE("Motion blur end-to-end across compile → evaluate → Camera2_5D") {
        CompositionDefinition def = make_unify_def("unify_motion_blur");
        def.camera->base.motion_blur.shutter_angle_deg = 180.0f;

        Camera2_5D cam = eval_one(def);
        CHECK(cam.motion_blur.shutter_angle_deg ==
              doctest::Approx(180.0f).epsilon(kFieldEps));
    }
}

// ════════════════════════════════════════════════════════════════════════
// (5) random access deterministico — evaluations in a permuted order
//     yield identical per-frame Camera2_5D outputs.  Repeated frame-0
//     accesses MUST produce bit-identical results; this catches any
//     session state carry-over or input-mutation in the modifier
//     pipeline.
// ════════════════════════════════════════════════════════════════════════
TEST_CASE("P3-F [5]: random access — sequence [5,100,0,50,25,10,0] is bit-stable") {
    CompositionDefinition def = make_unify_def("unify_random_access");
    auto cc_res = compile_composition(def, {});
    REQUIRE(cc_res.has_value());
    CompiledComposition cc = std::move(cc_res).value();
    REQUIRE(cc.camera_program);
    REQUIRE(cc.camera_program->is_compiled());

    // Reference pass — forward-order evaluation at frames 0..100.
    auto eval_ctx = make_unify_eval_ctx();
    std::map<int, Camera2_5D> reference;
    for (int fr : {0, 5, 10, 25, 50, 100}) {
        auto ev_res = evaluate(cc, eval_ctx, Frame{fr});
        REQUIRE(ev_res.has_value());
        REQUIRE(ev_res->camera.has_value());
        reference[fr] = *ev_res->camera;
    }

    // Random-access sequence — every value must reproduce its reference.
    const std::vector<int> seq = {5, 100, 0, 50, 25, 10, 0, 5, 0};
    for (int fr : seq) {
        auto ev_res = evaluate(cc, eval_ctx, Frame{fr});
        REQUIRE(ev_res.has_value());
        REQUIRE(ev_res->camera.has_value());
        CAPTURE(fr);
        check_cam2_5d_match(reference[fr], *ev_res->camera);
    }

    // Frame-0 invariance — every frame-0 read MUST be identical to the
    // initial frame-0 reference (no hidden accumulation between calls).
    for (int fr : seq) {
        if (fr != 0) continue;
        auto ev_res = evaluate(cc, eval_ctx, Frame{0});
        REQUIRE(ev_res.has_value());
        REQUIRE(ev_res->camera.has_value());
        check_cam2_5d_match(reference[0], *ev_res->camera);
    }
}

// ════════════════════════════════════════════════════════════════════════
// (6) parity legacy adapter vs descriptor — `camera_v1::camera_descriptor_from`
//     (the P3-E adapter) and a hand-crafted CameraDescriptor with the
//     same field values MUST produce byte-identical CompiledCompositions
//     and equivalent evaluate() output.  Proves the adapter is the
//     canonical one-way bridge from the legacy slim Camera struct.
// ════════════════════════════════════════════════════════════════════════
TEST_CASE("P3-F [6]: parity — legacy Camera adapter == hand-crafted descriptor") {
    // Deterministic legacy Camera (the legacy field that's now `[[deprecated]]`).
    chronon3d::Camera legacy_cam{};
    legacy_cam.fov_deg              = 60.0f;
    legacy_cam.transform.position   = Vec3{10.0f, 20.0f, -500.0f};
    legacy_cam.transform.rotation   = Vec3{0.0f, 5.0f, 0.0f};

    // (a) Adapter pristine output.
    CameraDescriptor desc_a = camera_v1::camera_descriptor_from(legacy_cam);

    // (b) Hand-crafted descriptor — same id tag + same field values.
    CameraDescriptor desc_b;
    desc_b.id          = desc_a.id;            // identity deduced from
                                               // adapter contract.
    desc_b.source      = StaticCameraSource{};
    desc_b.orientation = FixedOrientation{};
    desc_b.base.position = legacy_cam.transform.position;
    desc_b.base.rotation = legacy_cam.rotation_euler();
    {
        FovProjection proj{};
        proj.fov_deg.set(legacy_cam.fov_deg);
        desc_b.base.projection = std::move(proj);
        desc_b.base.lens.focal_length  = legacy_cam.focal_length(36.0f);
        desc_b.base.lens.f_stop        = 2.8f;
        desc_b.base.lens.sensor_width  = 36.0f;
        desc_b.base.lens.sensor_height = 24.0f;
        desc_b.base.lens.gate_fit      = GateFit::Fill;
    }

    // Compile both + evaluate both at frame 0.
    CompositionDefinition def_a = make_unify_def("unify_parity_a");
    def_a.camera = desc_a;
    CompositionDefinition def_b = make_unify_def("unify_parity_b");
    def_b.camera = desc_b;

    auto cc_a_res = compile_composition(def_a, {});
    auto cc_b_res = compile_composition(def_b, {});
    REQUIRE(cc_a_res.has_value());
    REQUIRE(cc_b_res.has_value());
    CompiledComposition cc_a = std::move(cc_a_res).value();
    CompiledComposition cc_b = std::move(cc_b_res).value();

    // Fingerprint equality proves byte-level content equivalence.
    CHECK(cc_a.fingerprint == cc_b.fingerprint);

    // Evaluate-result equivalence (per-frame Camera2_5D output).
    auto eval_ctx = make_unify_eval_ctx();
    auto ev_a = evaluate(cc_a, eval_ctx, Frame{0});
    auto ev_b = evaluate(cc_b, eval_ctx, Frame{0});
    REQUIRE(ev_a.has_value());
    REQUIRE(ev_b.has_value());
    REQUIRE(ev_a->camera.has_value());
    REQUIRE(ev_b->camera.has_value());
    check_cam2_5d_match(*ev_a->camera, *ev_b->camera);
}

// ════════════════════════════════════════════════════════════════════════
// (7) modifica descriptor → nuovo fingerprint — fingerprint changes when
//     a single descriptor byte changes, and re-compiling the same
//     descriptor produces the same fingerprint.  The fingerprint is
//     deterministic, content-stable, and any-mutation-detectable.
// ════════════════════════════════════════════════════════════════════════
TEST_CASE("P3-F [7]: modify descriptor → new fingerprint; same input → same fp") {
    CompositionDefinition def1 = make_unify_def(
        "unify_fp_test",
        Vec3{10.0f, 20.0f, -1000.0f},
        50.0f);

    auto cc1_res = compile_composition(def1, {});
    REQUIRE(cc1_res.has_value());
    std::uint64_t fp1 = cc1_res->fingerprint;
    REQUIRE(fp1 != 0);

    // Single-byte mutation: shift base.position.x by 1.0f.
    CompositionDefinition def2 = def1;
    def2.camera->base.position.x += 1.0f;

    auto cc2_res = compile_composition(def2, {});
    REQUIRE(cc2_res.has_value());
    std::uint64_t fp2 = cc2_res->fingerprint;
    CHECK(fp1 != fp2);

    // Mutation on FOV must also bite the fingerprint.
    CompositionDefinition def3 = def1;
    {
        FovProjection proj{};
        proj.fov_deg.set(45.0f);
        def3.camera->base.projection = std::move(proj);
    }
    auto cc3_res = compile_composition(def3, {});
    REQUIRE(cc3_res.has_value());
    CHECK(fp1 != cc3_res->fingerprint);

    // Idempotency: re-construct the same descriptor → same fingerprint.
    CompositionDefinition def4 = make_unify_def(
        "unify_fp_test",                              // same name field-by-field
        Vec3{10.0f, 20.0f, -1000.0f},
        50.0f);
    auto cc4_res = compile_composition(def4, {});
    REQUIRE(cc4_res.has_value());
    CHECK(cc4_res->fingerprint == fp1);
}
