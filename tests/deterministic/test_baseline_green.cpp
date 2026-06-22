#include <doctest/doctest.h>

// ============================================================================
// tests/deterministic/test_baseline_green.cpp
//
// WP-6 PR 6.8 — Baseline verde verificabile per docs/02-determinism.md.
//
// 6 test che PASSANO OGGI e dimostrano il sottoinsieme bit-exact del
// determinismo del renderer.  Ciascuno chiude uno dei blocchi "rimasti"
// di docs/02 (§2 Serial path, §3 TBB path, §4 Composite path) usando
// pattern che ISOLANO lo stato del renderer tra render (fresh renderer
// per render / cache esplicita / arena pin per render).  I 5 test
// disabilitati TICKET-007.q/r/s/t/u di gradient_determinism_tests.cpp
// restano disabled finché il rot SIMD-path non viene risolto a livello
// di backend software_compositor.cpp (ticket separato) — vedi
// docs/FOLLOWUP_TICKETS.md.
//
// Strategia di isolamento:
//   • Fresh-renderer-per-render: ogni render ottiene un SoftwareRenderer
//     distintro (`make_renderer()` ritorna un nuovo oggetto per call).
//     Lo stato del renderer (buffer_ring, frame_history, scene_hasher,
//     program_store) è quindi fresh ogni volta e non può leakare.
//   • Precomp cache esplicita: il primo render missa la cache, il secondo
//     hit la stessa cache.  Due hit consecutivi devono dare hash identico.
//   • tbb::task_arena pin per render: ogni render è wrappato in un
//     arena.execute(body) locale, che azzera lo scheduler-state TBB tra
//     render (helper `render_in_arena(slots, ...)` di
//     gradient_determinism_tests.cpp è uno standard di fatto).
//
// WP-6 PR 6.8.5 — Q6 hybrid sentinel strategy (analogo a
// `test_scheduler_determinism.cpp::kRefStaticScene`):
//   • Self-consistency CHECK esistente rimane — cattura rot
//     scheduler-state locale (catches `tbb::worker-local closure`
//     rot che farebbe divergere due render anche su backend stabile).
//   • Nuovo sentinel-gated REQUIRE: ogni TEST_CASE confronta il primo
//     hash osservato con `kRefBaseline<Name>`, fallendo al primo
//     mismatch quando il sentinel è stato popolato.  Sentinella
//     inizializzata a `kUncapturedSentinel` (`0xDEADBEEFDEADBEEFULL`)
//     → prima CI run popola via `doctest::MESSAGE` nella
//     `else { MESSAGE(...) }` branch.
//   • Strategia ibrida: il CHECK loop protegge da rot runtime; il
//     REQUIRE sentinel protegge da regression backend-graph
//     (cambio di backend che sposta bit-pattern).  Insieme coprono
//     sia rot scheduler-state che rot pipeline.
//
// Test lattice: 6 TEST_CASE.  Ciascuno copre un blocco specifico di
// docs/02-determinism.md.  Lo stato di docs/02 viene aggiornato per
// riflettere il verdetto REALE (verde dove PASSATO, rosso dove rot
// persiste).
// ============================================================================

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <tests/helpers/test_utils.hpp>
#include <tests/helpers/pixel_assertions.hpp>

#include <vector>
#include <string>
#include <cstdint>
#include <tbb/global_control.h>
#include <tbb/task_arena.h>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

// ── Composizioni statiche + composite (no gradients SIMD-critical) ───────

// Composizione statica con rects colorate.  Identica a quella usata da
// test_determinism_harness.cpp §1 — è la baseline "must be green".
Composition make_baseline_static_comp() {
    return composition(
        {.name = "BaselineStatic", .width = 320, .height = 180, .duration = 10},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.rect("bg",         {.size = {320, 180}, .color = Color{0.1f, 0.15f, 0.2f, 1.0f}, .pos = {0, 0, 0}});
            s.rect("red_box",    {.size = {60, 60},   .color = Color::red(),                .pos = {-80, -40, 0}});
            s.rect("green_box",  {.size = {60, 60},   .color = Color::green(),              .pos = {0, -40, 0}});
            s.rect("blue_box",   {.size = {60, 60},   .color = Color::blue(),               .pos = {80, -40, 0}});
            return s.build();
        }
    );
}

// Composizione multilayer con alpha-compositing (per §4 composite path).
// 2 strati traslucidi sovrapposti testano il CompositeNode end-to-end
// anche in assenza di gradient SIMD path.
Composition make_baseline_composite_comp() {
    return composition(
        {.name = "BaselineComposite", .width = 320, .height = 180, .duration = 10},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.rect("bg", {.size = {320, 180}, .color = Color{0.05f, 0.05f, 0.1f, 1.0f}, .pos = {0, 0, 0}});
            s.layer("layer_red_translucent", [](LayerBuilder& l) {
                l.position({-40.0f, -20.0f, 0.0f});
                l.rounded_rect("rr", {
                    .size = {120.0f, 80.0f},
                    .radius = 12.0f,
                    .color = Color{0.7f, 0.0f, 0.0f, 0.7f},   // 70% alpha — forces blend
                    .pos = {0, 0, 0},
                });
            });
            s.layer("layer_blue_translucent", [](LayerBuilder& l) {
                l.position({40.0f, 20.0f, 0.0f});
                l.rounded_rect("rr", {
                    .size = {120.0f, 80.0f},
                    .radius = 12.0f,
                    .color = Color{0.0f, 0.0f, 0.7f, 0.7f},   // 70% alpha — forces blend
                    .pos = {0, 0, 0},
                });
            });
            return s.build();
        }
    );
}

// ── Helper: tbb::task_arena pin per render (copiato da gradient_…) ──────
//
// Wrappa un singolo render in una task_arena locale: costruzione ed
// exit dell'arena forza TBB a liberare i worker-local caches, isolando
// l'execution state fra render successivi.  Questo è il pattern che il
// file gradient_determinism_tests.cpp chiama `render_in_arena`.
struct ArenaPinnedRenderResult {
    std::shared_ptr<Framebuffer> fb;
    std::uint64_t hash{0};
};

ArenaPinnedRenderResult render_in_arena(int slots, SoftwareRenderer& r,
                                          const Composition& comp, Frame frame) {
    ArenaPinnedRenderResult out;
    auto body = [&] {
        auto fb = r.render_frame(comp, frame);
        if (!fb) return;
        out.fb = std::move(fb);
        out.hash = framebuffer_hash(*out.fb);
    };
    if (slots > 0) {
        tbb::task_arena arena(slots);
        arena.execute(body);
    } else {
        tbb::task_arena arena;
        arena.execute(body);
    }
    return out;
}

// === Q6-hybrid sentinel constants =========================================
//
// Patterned after `tests/render_graph/executor/test_scheduler_determinism.cpp`
// `kRefStaticScene`/`kRefWarmCacheScene`/`kRefColdCacheScene` per
// reviewer round 2 della Q6 hybrid strategy.  Sentinel
// `0xDEADBEEFDEADBEEFULL` è distinto dall'FNV1a-64 offset basis
// `0xCBF29CE484222325` (usato da `fnv1a64` in scheduler test) per
// evitare collision silenziosa; è un puro "first-run marker".  Quando
// il sentinel è popolato (`kUncapturedSentinel` → real hash), il
// confronto `REQUIRE(hash == kRef...)` fallisce sul primo drift di
// backend roadmap (es. cambio di compositore, ottimizzazione path).
//
// To populate: run `ctest -R 'Baseline green' -V` once on a clean
// linux-ci checkout, copy each of the 6 MESSAGE-printed hashes into
// its corresponding `kRefBaseline*` constant, and re-build.  All
// subsequent runs REQUIRE exact match.
//
// Mapping (constants ↔ TEST_CASE):
//   kRefBaselineFreshShader  ↔ §1 "30 fresh renderers produce identical composite hashes"
//   kRefBaselineArenaPin     ↔ §2 "serial-mode (arena(1)) produces identical hashes over 30 renders"
//   kRefBaselineThreadEq     ↔ §3 "1t == 4t == 8t bit-exact under per-render tbb arena pin"
//   kRefBaselineComposite    ↔ §4 "30 consecutive composite-full-frame renders — pixel-identical"
//   kRefBaselineSsim         ↔ §5 "composite path SSIM ≥ 0.999 across 2 renders"
//   kRefBaselinePrecompCache ↔ §6 "precomp cache-hit determinism"

constexpr std::uint64_t kUncapturedSentinel = 0xDEADBEEFDEADBEEFULL;

constexpr std::uint64_t kRefBaselineFreshShader  = kUncapturedSentinel;
constexpr std::uint64_t kRefBaselineArenaPin     = kUncapturedSentinel;
constexpr std::uint64_t kRefBaselineThreadEq     = kUncapturedSentinel;
constexpr std::uint64_t kRefBaselineComposite    = 610724219931969400ULL;
constexpr std::uint64_t kRefBaselineSsim         = 610724219931969400ULL;
constexpr std::uint64_t kRefBaselinePrecompCache = 15056832347528341909ULL;

inline bool is_reference_captured(std::uint64_t r) noexcept {
    return r != kUncapturedSentinel;
}

} // anonymous namespace


// ═══════════════════════════════════════════════════════════════════════
// §1 — docs/02 §3 (TBB path) mitigated: fresh-renderer-per-render
//      guarantees the cross-run hash equality that TICKET-007.q/r/s
//      currently assert violated for the gradient renderer.
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("Baseline green §1: 30 fresh renderers produce identical composite hashes") {
    // 30 distinct SoftwareRenderer instances, each rendering the same
    // composition once.  Each renderer is built `make_renderer()` which
    // allocates a fresh RenderRuntime + session + buffer_ring.  Since
    // the state is per-instance fresh, no cross-render state can leak
    // — and the FNV-1a framebuffer hash is identical for the identical
    // input.  This is the work-around path documented in TICKET-007
    // as "rendering contract works on fresh renderers".
    auto comp = make_baseline_static_comp();
    std::vector<std::uint64_t> hashes;
    hashes.reserve(30);

    for (int i = 0; i < 30; ++i) {
        auto renderer = make_renderer();   // fresh per render
        auto fb = renderer.render_frame(comp, 0);
        REQUIRE(fb != nullptr);
        hashes.push_back(framebuffer_hash(*fb));
    }

    for (std::size_t i = 1; i < hashes.size(); ++i) {
        INFO("Render " << i << " differs from render 0");
        CHECK(hashes[i] == hashes[0]);
    }

    // Q6-hybrid backend-graph regression guard (PR 6.8.5):
    // populate kRefBaselineFreshShader from this test's MESSAGE branch
    // on the FIRST clean linux-ci run, then REQUIRE exact match thereafter.
    if (is_reference_captured(kRefBaselineFreshShader)) {
        REQUIRE(hashes[0] == kRefBaselineFreshShader);
    } else {
        MESSAGE("kRefBaselineFreshShader unset; first hash to capture: " << hashes[0]);
    }
}


// ═══════════════════════════════════════════════════════════════════════
// §2 — docs/02 §2 (Serial path) mitigato: explicit tbb::task_arena(1)
//      guarantees the single-threaded output that the serial baseline
//      requires, with cross-render isolation provided by the arena
//      exit destructor.
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("Baseline green §2: serial-mode (arena(1)) produces identical hashes over 30 renders") {
    // Each render is wrapped in a fresh tbb::task_arena(1) — the arena
    // destructor at scope exit returns the worker thread to the TBB
    // global pool with its local cache released.  This isolates
    // scheduler-state across renders, so the same architecture that
    // causes TICKET-007.q rot is forced into the deterministic mode
    // documented in `docs/02 §2` ("Serial path = truth reference,
    // `arena(1) caps nested tbb::parallel_for`").
    auto comp = make_baseline_static_comp();
    std::vector<std::uint64_t> hashes;
    hashes.reserve(30);

    for (int i = 0; i < 30; ++i) {
        auto renderer = make_renderer();
        auto res = render_in_arena(/*slots=*/1, renderer, comp, 0);
        REQUIRE(res.fb != nullptr);
        hashes.push_back(res.hash);
    }

    for (std::size_t i = 1; i < hashes.size(); ++i) {
        INFO("Render " << i << " differs from render 0");
        CHECK(hashes[i] == hashes[0]);
    }

    // Q6-hybrid backend-graph regression guard (PR 6.8.5):
    // populate kRefBaselineArenaPin from this test's MESSAGE branch on
    // the FIRST clean linux-ci run, then REQUIRE exact match thereafter.
    if (is_reference_captured(kRefBaselineArenaPin)) {
        REQUIRE(hashes[0] == kRefBaselineArenaPin);
    } else {
        MESSAGE("kRefBaselineArenaPin unset; first hash to capture: " << hashes[0]);
    }
}


// ═══════════════════════════════════════════════════════════════════════
// §3 — docs/02 §3 (TBB path) all-modes parity under arena-pin isolation
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("Baseline green §3: 1t == 4t == 8t bit-exact under per-render tbb arena pin") {
    // Mirrors the structure of `gradient_determinism_tests.cpp` §3
    // (TICKET-007.t/.u disabled tests).  For the non-gradient
    // static-scene composition + fresh renderer + per-render arena
    // pinning, all three thread configurations MUST produce bit-exact
    // hashes.  The rot inside `gradient_determinism_tests.cpp §3` is
    // gradient+SSE path-specific (see `tools/verify_downsample_blur.cpp`
    // for the floating-point associativity contract investigation), not
    // present in the static-scene path.
    auto comp = make_baseline_static_comp();

    auto r1t = make_renderer();
    auto r4t = make_renderer();
    auto r8t = make_renderer();
    auto res_1t = render_in_arena(1, r1t, comp, 0);
    auto res_4t = render_in_arena(4, r4t, comp, 0);
    auto res_8t = render_in_arena(8, r8t, comp, 0);

    REQUIRE(res_1t.fb != nullptr);
    REQUIRE(res_4t.fb != nullptr);
    REQUIRE(res_8t.fb != nullptr);

    INFO("1t hash=" << res_1t.hash << " 4t hash=" << res_4t.hash
         << " 8t hash=" << res_8t.hash);
    CHECK(res_1t.hash == res_4t.hash);
    CHECK(res_1t.hash == res_8t.hash);

    // Q6-hybrid backend-graph regression guard (PR 6.8.5):
    // populate kRefBaselineThreadEq from the 1t arm on the FIRST
    // clean linux-ci run, then REQUIRE exact match thereafter.  The
    // 4t/8t arms are transitively verified by their CHECK equality
    // with res_1t.hash above (kRefBaselineThreadEq == res_1t.hash).
    if (is_reference_captured(kRefBaselineThreadEq)) {
        REQUIRE(res_1t.hash == kRefBaselineThreadEq);
    } else {
        MESSAGE("kRefBaselineThreadEq unset; first hash to capture: " << res_1t.hash);
    }
}


// ═══════════════════════════════════════════════════════════════════════
// §4 — docs/02 §4 (Composite path): full-frame composite determinism
//      over 30 consecutive renders (alpha-compositing layers).
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("Baseline green §4: 30 consecutive composite-full-frame renders — pixel-identical") {
    // 30 invokes of `render_frame(comp, 0)` on a SINGLE renderer holding
    // a multilayer alpha-composite scene.  Each render reuses the
    // renderer (buffer_ring carries the previous framebuffer), which is
    // the WIN-TICKET case for tile-coordinate reuse but historically a
    // rot source for state-carry-over tests.  The composite path with
    // translucent rounded rects exercises the CompositeNode + alpha
    // blender end-to-end — proving that the path is bit-stable for
    //     repeated full-frame composite invokes, NOT isolating the
    //     gradient SIMD rot that disables TICKET-007.q/r/s.
    auto comp = make_baseline_composite_comp();
    auto renderer = make_renderer();

    std::vector<std::uint64_t> hashes;
    hashes.reserve(30);

    for (int i = 0; i < 30; ++i) {
        auto fb = renderer.render_frame(comp, 0);
        REQUIRE(fb != nullptr);
        hashes.push_back(framebuffer_hash(*fb));
    }

    for (std::size_t i = 1; i < hashes.size(); ++i) {
        INFO("Render " << i << " differs from render 0");
        CHECK(hashes[i] == hashes[0]);
    }

    // Q6-hybrid backend-graph regression guard (PR 6.8.5):
    // populate kRefBaselineComposite from this test's MESSAGE branch
    // on the FIRST clean linux-ci run, then REQUIRE exact match thereafter.
    if (is_reference_captured(kRefBaselineComposite)) {
        REQUIRE(hashes[0] == kRefBaselineComposite);
    } else {
        MESSAGE("kRefBaselineComposite unset; first hash to capture: " << hashes[0]);
    }
}


// ═══════════════════════════════════════════════════════════════════════
// §5 — Composite path + semantic SSIM guard.
//      Demonstrates the composite path's output isn't just bit-stable
//      across renders but also meets the perceptual SSIM ≥ 0.999
//      threshold used by `test_determinism_harness` §3 (semantic-equal
//      identical frames).
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("Baseline green §5: composite path SSIM ≥ 0.999 across 2 renders") {
    auto comp = make_baseline_composite_comp();
    auto renderer = make_renderer();

    auto fb1 = renderer.render_frame(comp, 0);
    auto fb2 = renderer.render_frame(comp, 0);
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);

    auto diff = compare_framebuffers_semantic(*fb1, *fb2);
    CHECK(diff.mismatched_pixels == 0);
    CHECK(diff.max_channel_error == 0.0f);
    CHECK(diff.psnr >= 100.0f);

    const float ssim = ssim_luminance(*fb1, *fb2);
    CHECK(ssim >= 0.999f);

    // Q6-hybrid backend-graph regression guard (PR 6.8.5):
    // for §5 we lock the bit-stable framebuffer_hash(*fb1) rather than
    // the float SSIM value (the SSIM check is already enforced above
    // as `ssim >= 0.999f`; the hash locks the actual paint path that
    // produces the pixels — bit-for-bit backend identity contract).
    const std::uint64_t hash5 = framebuffer_hash(*fb1);
    if (is_reference_captured(kRefBaselineSsim)) {
        REQUIRE(hash5 == kRefBaselineSsim);
    } else {
        MESSAGE("kRefBaselineSsim unset; first hash to capture: " << hash5);
    }
}


// ═══════════════════════════════════════════════════════════════════════
// §6 — PrecompNode cache-hit determinism.  Verifies that two consecutive
//      frames hit the SAME cache partition (cache hit)
//
// ─ docs/02 §4 (Composite path) interlocking with §3 PR 6.3 — once
//    ProgramLease is held for the complete child scope (PR 6.3 from
//    docs/03-§4.3), `same scene twice = identical hash` becomes the
//    PROOF-FLOOR for the determinism of the PrecompNode inner path.
//    In the meantime the test below uses the per-session
//    `SceneProgramStore` (per-session state per WP-3 PR 3.1) and
//    verifies cache-hit determinism WITHOUT touching the precomp
//    signature.
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("Baseline green §6: precomp cache-hit determinism — two consecutive frames ≡ same hash") {
    // Cremiamo un composition parent con un precomp layer nidificato.
    // Il primo render MISS la cache, il secondo HIT.  Se la cache-
    // hit path fosse non deterministica, i due hash divergerebbero.
    CompositionRegistry registry;
    registry.add("inner", [](const CompositionProps&) {
        return Composition(
            CompositionSpec{.name = "inner", .width = 80, .height = 80, .duration = Frame{60}},
            [](const FrameContext& ctx) -> Scene {
                SceneBuilder s(ctx.width, ctx.height, ctx.resource);
                s.rect("bg", RectParams{
                    .size = Vec2{static_cast<f32>(ctx.width), static_cast<f32>(ctx.height)},
                    .color = Color::blue(),
                    .pos = Vec3{static_cast<f32>(ctx.width) / 2.0f,
                                static_cast<f32>(ctx.height) / 2.0f, 0.0f}
                });
                return s.build();
            }
        );
    });

    Composition parent_comp(
        CompositionSpec{.width = 200, .height = 200, .duration = Frame{60}},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.precomp_layer("nested", "inner", [](LayerBuilder& l) {
                l.from(Frame{0});
            });
            return s.build();
        }
    );

    auto renderer = make_renderer();
    renderer.set_composition_registry(&registry);

    // First frame: cache miss (compile from registry)
    auto fb_miss = renderer.render_frame(parent_comp, Frame{0});
    REQUIRE(fb_miss != nullptr);
    const std::uint64_t hash_miss = framebuffer_hash(*fb_miss);

    // Second frame (identical input): cache hit
    auto fb_hit = renderer.render_frame(parent_comp, Frame{1});
    REQUIRE(fb_hit != nullptr);
    const std::uint64_t hash_hit = framebuffer_hash(*fb_hit);

    INFO("cache-miss hash=" << hash_miss << "  cache-hit hash=" << hash_hit);
    CHECK(hash_miss == hash_hit);

    // Q6-hybrid backend-graph regression guard (PR 6.8.5):
    // we lock hash_miss; hash_hit is transitively validated by the
    // CHECK(hash_miss == hash_hit) above.  One sentinel is enough
    // because the equality check is already in the test body.
    if (is_reference_captured(kRefBaselinePrecompCache)) {
        REQUIRE(hash_miss == kRefBaselinePrecompCache);
    } else {
        MESSAGE("kRefBaselinePrecompCache unset; first hash to capture: " << hash_miss);
    }
}
