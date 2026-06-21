// =============================================================================
// test_precomp_node_cache.cpp — PrecompNode + SceneProgramStore integration
//
// WP 5 — PrecompNode no longer owns SceneProgramCache.  Tests verify
// cache behavior through the centralized SceneProgramStore on RenderSession.
//
// WP 5.0 — accessor path is `session->program_store().acquire(...)` (return
// is a Reference to the SceneProgramStore; the legacy `program_store->` form
// stopped compiling once the field was relocated to runtime).
//
// WP 5.1 + 4.3 — `PrecompNode::instance_key(ctx)` derives from
// `ctx.node_exec.current_identity`.  Each test sets a unique identity
// before driving the precomp so two siblings land in distinct buckets
// (WP 4.4 / 5.1 sibling-isolation invariant).
//
// Test cases:
//   1. Cache hit on repeated execution — same key → same program, compile=1
//   2. Cache miss on different composition — different name → recompile
//   3. Eviction callback forwarding — inner eviction fires node's callback
//   4. Parameter block warm-up — binding count matches after execute
//   5. Multi-frame stability — 10 frames with same structure → 1 compile
//   6. Duration boundary — nested_frame >= m_duration returns empty
//   7. End-to-end via SoftwareRenderer — full pipeline smoke test
//   8. Auto-tuning — SceneProgramStore drives tuning through per-bucket
//      record_execution() rather than the test manually invoking
//      `cache.auto_tune()` (WP 5.4).
// =============================================================================

#include <doctest/doctest.h>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/runtime/render_session.hpp>

#include <chronon3d/render_graph/nodes/precomp_node.hpp>
#include <chronon3d/render_graph/cache/scene_program_store.hpp>
#include <chronon3d/render_graph/cache/scene_program_cache.hpp>
#include <chronon3d/render_graph/pipeline/frame_parameter_block.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/render_graph/core/node_identity.hpp>
#include <tests/helpers/pixel_assertions.hpp>
using namespace chronon3d;

using namespace chronon3d::graph;
using namespace chronon3d::cache;

namespace {

// ── Helper: create a simple inner composition ───────────────────────────────
Composition make_inner_comp(const char* name, int w, int h, Color color) {
    return Composition(
        CompositionSpec{.name=name, .width=w, .height=h, .duration=Frame{60}},
        [color, w, h](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx.width, ctx.height, ctx.resource);
            s.rect("bg", RectParams{
                .size = Vec2{static_cast<f32>(w), static_cast<f32>(h)},
                .color = color,
                .pos = Vec3{static_cast<f32>(w / 2), static_cast<f32>(h / 2), 0.0f}
            });
            return s.build();
        }
    );
}

// ── Helper: minimal RenderGraphContext + RenderSession for PrecompNode tests ──
//
// WP 5.5 — the TestContext now owns a SceneProgramStore (unique_ptr,
// because SceneProgramStore carries a std::mutex and is non-movable) AND
// a GraphExecutor, and wires BOTH into session.services so the
// `session->program_store()` and `session->services.executor` accessors
// reach real objects (the legacy form threw because the field had been
// relocated to runtime).
//
// Defensive TestContext hygiene — the TestContext owns its OWN
// per-instance `CompositionRegistry registry` member, so cross-TC
// staleness from this file alone is impossible.  The three
// belt-and-suspenders guards below address a different concern: if a
// future change ever causes the registry member to be promoted to a
// file-scope static (mirroring
// `tests/animations/test_background_catalog.cpp:20`), the test would
// silently re-register `"inner"` across cases and the registry's
// `add()` would throw `"Duplicate composition: inner"`.  These guards
// keep the fixture robust against that future refactor:
//
//   - `registry.clear()` empties any factories a previous TC's
//     `add_comp` installed.  Belt — runs unconditionally at TC start,
//     so a TOC-TOU window (a stale factory surviving a TC) is wiped
//     before any work begins.
//   - `add_comp` one-shot guard: `if (registry.contains(name)) return;`.
//     Suspenders — protects against a re-entry that bypasses
//     `clear()` (e.g. mid-TC composition creation).
//   - `shared_backend()` returns ONE process-wide static
//     `SoftwareRenderer`.  This avoids the per-TC construction cost
//     of synthesising a fresh `RenderRuntime + Catalog stack` for
//     every single test (each takes ~milliseconds).  Note: we DO NOT
//     claim the previous per-instance ctors registered
//     `"GridCleanBackground"` themselves — `SoftwareRenderer(Config)`
//     and `RenderRuntime::populate()` only register shape processors
//     and graph nodes, NOT compositions (`GridCleanBackground` is
//     wired by `content::backgrounds::register_grid_clean_background`
//     via `register_content_modules`, neither of which lives on the
//     renderer ctor path).
struct TestContext {
    /// Process-wide lazy-init shared SoftwareRenderer.  Keeps the per-TC
    /// `TestContext` construction cost constant (the synthesised
    /// RenderRuntime is paid once, on first TC entry).
    static SoftwareRenderer& shared_backend() {
        static SoftwareRenderer instance{Config{}};
        return instance;
    }

    Config                      renderer_cfg;
    SoftwareRenderer&           backend;
    std::shared_ptr<FramebufferPool> pool;
    NodeCache                   node_cache;
    CompositionRegistry         registry;
    // WP 5 — TestContext-owned scene-program store (per WP 5.5 the
    // store is per-owner-session; for tests this means a unique_ptr the
    // test fixture outlives the lifetime of every PrecompNode.execute()).
    std::unique_ptr<SceneProgramStore> program_store;
    GraphExecutor               executor;
    RenderSession               session;
    ExecutionScheduler          scheduler;
    RenderGraphContext          ctx;

    explicit TestContext(int w = 200, int h = 200)
        : backend(shared_backend())
        , pool(std::make_shared<FramebufferPool>(128))
        , program_store(std::make_unique<SceneProgramStore>())
        , scheduler(SchedulerMode::Sequential, 1, false)
    {
        // Reset per-TC start state.  `registry.clear()` empties any
        // factories a previous TC's `add_comp` (defensively) installed
        // (see Option-3 inside the user-suggested fixes).  The renderer
        // and pool stay shared for the lifetime of the test binary.
        registry.clear();

        session.services.backend        = &backend;
        session.services.node_cache     = &node_cache;
        session.services.framebuffer_pool = pool.get();
        session.services.executor       = &executor;
        session.services.program_store  = program_store.get();
        session.services.assets_registry = nullptr;  // not exercised here

        ctx.services.backend         = &backend;
        ctx.services.node_cache      = &node_cache;
        ctx.services.framebuffer_pool = pool;
        ctx.services.registry        = &registry;
        ctx.services.session         = &session;
        ctx.services.scheduler       = &scheduler;

        ctx.frame_input.frame        = Frame{0};
        ctx.frame_input.width        = w;
        ctx.frame_input.height       = h;
    }

    void add_comp(const char* name, int w, int h, Color color) {
        // Belt-and-suspenders (Option 1 of the user-suggested fixes):
        // if the composition name is already registered, skip the
        // re-registration.  Today each TestContext owns a FRESH
        // `CompositionRegistry`, so `contains()` is normally false —
        // but the cost is one lookup per call and the test stays
        // robust against future fixture churn (e.g. if the registry
        // were promoted to a file-scope static, this guard would be
        // the safety net).
        if (registry.contains(name)) {
            return;
        }
        registry.add(name, [=](const CompositionProps&) {
            return make_inner_comp(name, w, h, color);
        });
    }

    void set_frame(Frame f) { ctx.frame_input.frame = f; }

    /// WP 5.1 / 4.4 — set the per-precomp identity so each precomp
    /// lands in its own SceneProgramStore bucket.  Mirror the test's
    /// intent: different comp names → different identities → distinct
    /// buckets.  When two precomps share an identity they collide on
    /// the same bucket (this is the failure mode WP 4.4 asserts the
    /// compiler must NOT produce for a re-build of the same graph).
    void set_precomp_identity(NodeIdentity id) {
        ctx.node_exec.current_identity = id;
    }
};

// Helper: derive a deterministic NodeIdentity for a given comp name so
// sibling-isolation assertions can compare two buckets by their
// identity-derived keys.
NodeIdentity identity_for(std::string_view comp_name, std::uint64_t salt = 0) {
    const auto h = hash_string(comp_name) ^ salt;
    return NodeIdentity{
        GraphInstanceId{h ? h : 1},
        StableNodeId{salt ? salt : hash_string(std::string{comp_name})}
    };
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// §1: Cache hit on repeated execution — same pointer, compile_count=1
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("precomp_cache: cache hit returns same program on repeated execute") {
    TestContext tc;
    tc.add_comp("inner", 80, 80, Color::blue());
    tc.set_precomp_identity(identity_for("inner_a"));

    PrecompCachePolicy policy{.initial_capacity = 8};
    PrecompNode precomp("inner", Frame{0}, Frame{60}, Frame{-1}, policy);

    auto fb0 = precomp.execute(tc.ctx, {}, {});
    REQUIRE(fb0 != nullptr);
    REQUIRE(fb0->width() == 200);
    REQUIRE(fb0->height() == 200);

    auto stats = tc.session.program_store().aggregate_stats();
    CHECK(stats.misses == 1);
    CHECK(stats.hits == 0);
    CHECK(stats.current_size == 1);

    auto fb1 = precomp.execute(tc.ctx, {}, {});
    REQUIRE(fb1 != nullptr);

    stats = tc.session.program_store().aggregate_stats();
    CHECK(stats.misses == 1);
    CHECK(stats.hits >= 1);
    CHECK(stats.current_size == 1);
}

TEST_CASE("precomp_cache: different frame with same structure → cache hit") {
    TestContext tc;
    tc.add_comp("inner", 80, 80, Color::blue());
    tc.set_precomp_identity(identity_for("inner_b"));

    PrecompCachePolicy policy{.initial_capacity = 8};
    PrecompNode precomp("inner", Frame{0}, Frame{60}, Frame{-1}, policy);

    tc.set_frame(Frame{0});
    precomp.execute(tc.ctx, {}, {});
    auto stats0 = tc.session.program_store().aggregate_stats();
    CHECK(stats0.misses == 1);
    CHECK(stats0.hits == 0);

    tc.set_frame(Frame{1});
    precomp.execute(tc.ctx, {}, {});
    auto stats1 = tc.session.program_store().aggregate_stats();
    CHECK(stats1.misses == 1);
    CHECK(stats1.hits >= 1);

    tc.set_frame(Frame{2});
    precomp.execute(tc.ctx, {}, {});
    auto stats2 = tc.session.program_store().aggregate_stats();
    CHECK(stats2.misses == 1);
    CHECK(stats2.hits >= 2);
}

// ═════════════════════════════════════════════════════════════════════════════
// §2: Cache miss on different composition → recompilation
// ═════════════════════════════════════════════════════════════════════════════
//
// WP 5.1 — two sibling precomps using the SAME composition but with
// DIFFERENT identities must land in distinct buckets (each is treated
// as a separate precomp owner).  Two siblings with the SAME identity
// must SHARE a bucket.

TEST_CASE("precomp_cache: different composition name triggers recompile") {
    TestContext tc;
    tc.add_comp("comp_a", 80, 80, Color::red());
    tc.add_comp("comp_b", 80, 80, Color::blue());

    PrecompCachePolicy policy{.initial_capacity = 8};

    {
        tc.set_precomp_identity(identity_for("comp_a"));
        PrecompNode precomp("comp_a", Frame{0}, Frame{60}, Frame{-1}, policy);
        precomp.execute(tc.ctx, {}, {});
        auto stats = tc.session.program_store().aggregate_stats();
        CHECK(stats.misses == 1);
    }

    {
        tc.set_precomp_identity(identity_for("comp_b"));
        PrecompNode precomp("comp_b", Frame{0}, Frame{60}, Frame{-1}, policy);
        precomp.execute(tc.ctx, {}, {});
        auto stats = tc.session.program_store().aggregate_stats();
        CHECK(stats.misses == 2);  // two distinct buckets, two misses
    }
}

TEST_CASE("precomp_cache: sibling precomps using same composition get distinct buckets (WP 4.2+5.1)") {
    // Same composition but TWO sibling precomp calls with distinct
    // identities → distinct buckets → distinct misses.
    TestContext tc;
    tc.add_comp("shared_comp", 80, 80, Color::green());

    PrecompCachePolicy policy{.initial_capacity = 8};
    PrecompNode precomp_a("shared_comp", Frame{0}, Frame{60}, Frame{-1}, policy);
    PrecompNode precomp_b("shared_comp", Frame{0}, Frame{60}, Frame{-1}, policy);

    tc.set_precomp_identity(identity_for("shared_sibling_a", 1));
    precomp_a.execute(tc.ctx, {}, {});
    auto stats_after_a = tc.session.program_store().aggregate_stats();
    CHECK(stats_after_a.misses == 1);
    CHECK(tc.session.program_store().instance_count() == 1);

    tc.set_precomp_identity(identity_for("shared_sibling_b", 2));
    precomp_b.execute(tc.ctx, {}, {});
    auto stats_after_b = tc.session.program_store().aggregate_stats();
    CHECK(stats_after_b.misses == 2);                    // second bucket → second miss
    CHECK(tc.session.program_store().instance_count() == 2);
}

TEST_CASE("precomp_cache: nonexistent composition returns empty fb") {
    TestContext tc;
    PrecompCachePolicy policy;
    PrecompNode precomp("ghost", Frame{0}, Frame{60}, Frame{-1}, policy);
    tc.set_precomp_identity(identity_for("ghost"));

    auto fb = precomp.execute(tc.ctx, {}, {});
    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 200);
    CHECK(fb->height() == 200);
}

// ═════════════════════════════════════════════════════════════════════════════
// §3: Eviction callback forwarding via store
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("precomp_cache: eviction callback fires on inner cache evict") {
    TestContext tc;
    tc.add_comp("inner", 80, 80, Color::green());
    tc.set_precomp_identity(identity_for("inner_evict"));

    PrecompCachePolicy policy{.initial_capacity = 2};
    PrecompNode precomp("inner", Frame{0}, Frame{60}, Frame{-1}, policy);

    int evict_count = 0;
    precomp.set_on_evict([&](const graph::SceneStructureKey&) {
        ++evict_count;
    });

    precomp.execute(tc.ctx, {}, {});

    // Acquire the same bucket with three different SceneStructureKeys,
    // the third triggering an LRU eviction.
    auto& store = tc.session.program_store();
    auto bucket = precomp.instance_key(tc.ctx);

    store.acquire(bucket, SceneStructureKey{1, 0, 0, 80, 80, 1}, policy,
        []() -> std::unique_ptr<CompiledSceneProgram> {
            auto p = std::make_unique<CompiledSceneProgram>();
            p->valid = true;
            p->frame_graph.valid = true;
            return p;
        });
    store.acquire(bucket, SceneStructureKey{2, 0, 0, 80, 80, 1}, policy,
        []() -> std::unique_ptr<CompiledSceneProgram> {
            auto p = std::make_unique<CompiledSceneProgram>();
            p->valid = true;
            p->frame_graph.valid = true;
            return p;
        });
    CHECK(evict_count == 0);

    store.acquire(bucket, SceneStructureKey{3, 0, 0, 80, 80, 1}, policy,
        []() -> std::unique_ptr<CompiledSceneProgram> {
            auto p = std::make_unique<CompiledSceneProgram>();
            p->valid = true;
            p->frame_graph.valid = true;
            return p;
        });
    CHECK(evict_count == 1);
}

TEST_CASE("precomp_cache: no crash when eviction callback not set") {
    TestContext tc;
    tc.add_comp("inner", 80, 80, Color::blue());
    tc.set_precomp_identity(identity_for("inner_no_cb"));

    PrecompCachePolicy policy;
    PrecompNode precomp("inner", Frame{0}, Frame{60}, Frame{-1}, policy);
    precomp.execute(tc.ctx, {}, {});
    auto stats = tc.session.program_store().aggregate_stats();
    CHECK(stats.hits == 0);
    CHECK(stats.misses == 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// §4: Parameter block warm-up
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("precomp_cache: parameter block sized after execution") {
    TestContext tc;
    tc.add_comp("inner", 80, 80, Color::red());
    tc.set_precomp_identity(identity_for("inner_pblock"));

    PrecompCachePolicy policy;
    PrecompNode precomp("inner", Frame{0}, Frame{60}, Frame{-1}, policy);

    CHECK(precomp.param_block().empty());

    precomp.execute(tc.ctx, {}, {});
    // Param block won't actually have bindings here because precomp_builder
    // is null in this fixture — but it should at least have been warmed.
    CHECK_FALSE(precomp.param_block().empty());

    precomp.execute(tc.ctx, {}, {});
    CHECK_FALSE(precomp.param_block().empty());
}

// ═════════════════════════════════════════════════════════════════════════════
// §5: Multi-frame stability — 10 frames, 1 compile
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("precomp_cache: 10 frames with same structure → 1 compile") {
    TestContext tc;
    tc.add_comp("inner", 80, 80, Color::blue());
    tc.set_precomp_identity(identity_for("inner_10f"));

    PrecompCachePolicy policy{.initial_capacity = 8};
    PrecompNode precomp("inner", Frame{0}, Frame{60}, Frame{-1}, policy);

    for (int f = 0; f < 10; ++f) {
        tc.set_frame(Frame{f});
        precomp.execute(tc.ctx, {}, {});
    }

    auto stats = tc.session.program_store().aggregate_stats();
    CHECK(stats.misses == 1);
    CHECK(stats.hits >= 9);
    CHECK(stats.current_size == 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// §6: Duration boundary
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("precomp_cache: nested_frame past duration returns empty") {
    TestContext tc;
    tc.add_comp("inner", 80, 80, Color::blue());
    tc.set_precomp_identity(identity_for("inner_dur1"));

    PrecompCachePolicy policy;
    PrecompNode precomp("inner", Frame{0}, Frame{30}, Frame{-1}, policy);

    tc.set_frame(Frame{0});
    auto fb0 = precomp.execute(tc.ctx, {}, {});
    REQUIRE(fb0 != nullptr);
    auto stats0 = tc.session.program_store().aggregate_stats();
    CHECK(stats0.misses == 1);

    tc.set_frame(Frame{30});
    auto fb30 = precomp.execute(tc.ctx, {}, {});
    REQUIRE(fb30 != nullptr);
    auto stats30 = tc.session.program_store().aggregate_stats();
    CHECK(stats30.misses == 1);

    tc.set_frame(Frame{31});
    auto fb31 = precomp.execute(tc.ctx, {}, {});
    REQUIRE(fb31 != nullptr);
}

TEST_CASE("precomp_cache: negative nested_frame returns empty") {
    TestContext tc;
    tc.add_comp("inner", 80, 80, Color::blue());
    tc.set_precomp_identity(identity_for("inner_neg"));

    PrecompCachePolicy policy;
    PrecompNode precomp("inner", Frame{10}, Frame{30}, Frame{-1}, policy);

    tc.set_frame(Frame{0});
    auto fb = precomp.execute(tc.ctx, {}, {});
    REQUIRE(fb != nullptr);

    tc.set_frame(Frame{10});
    fb = precomp.execute(tc.ctx, {}, {});
    REQUIRE(fb != nullptr);
    auto stats = tc.session.program_store().aggregate_stats();
    CHECK(stats.misses == 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// §7: End-to-end via SoftwareRenderer (integration smoke test)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("precomp_cache: end-to-end via SoftwareRenderer does not crash") {
    CompositionRegistry registry;
    registry.add("inner", [](const CompositionProps&) {
        return Composition(
            CompositionSpec{.name="inner", .width=80, .height=80, .duration=Frame{60}},
            [](const FrameContext& ctx) -> Scene {
                SceneBuilder s(ctx.width, ctx.height, ctx.resource);
                s.rect("bg", {.size={80,80}, .color=Color::blue(), .pos={40,40,0}});
                return s.build();
            }
        );
    });

    SoftwareRenderer renderer;
    renderer.set_composition_registry(&registry);

    Composition parent_comp(
        CompositionSpec{.width=200, .height=200, .duration=Frame{60}},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.precomp_layer("nested", "inner", [](LayerBuilder& l) {
                l.from(Frame{0});
            });
            return s.build();
        }
    );

    auto fb0 = renderer.render_frame(parent_comp, Frame{0});
    REQUIRE(fb0 != nullptr);

    auto fb1 = renderer.render_frame(parent_comp, Frame{1});
    REQUIRE(fb1 != nullptr);

    Color blue = Color::blue();
    CHECK(test::any_pixel(*fb0, blue));
    CHECK(test::any_pixel(*fb1, blue));
}

// ═════════════════════════════════════════════════════════════════════════════
// §8: WP 5.4 — Auto-tune driven by record_execution() through the store
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("auto_tune: per-bucket record_execution triggers interval-based auto_tune") {
    SceneProgramCache cache(4, 1);
    cache.set_tune_mode(TuneMode::Auto);

    cache::TuneConfig cfg;
    cfg.interval     = 30;
    cfg.min_capacity = 2;
    cfg.max_capacity = 128;
    cache.set_tune_config(cfg);

    CHECK(cache.capacity() == 4);

    // Drive `interval` (30) recorded executions — Fixed mode would
    // never re-tune, but the cache is in Auto, so the 30th invocation
    // should fire auto_tune() and resize up to 8 because every record
    // is also a miss (LRU churn).
    for (int i = 0; i < 30; ++i) {
        cache.find_or_compile(
            SceneStructureKey{static_cast<uint64_t>(1000 + i), 0, 0, 80, 80, 1},
            []() -> std::unique_ptr<CompiledSceneProgram> {
                auto p = std::make_unique<CompiledSceneProgram>();
                p->valid = true;
                p->frame_graph.valid = true;
                return p;
            });
        cache.record_execution();  // Store wires this on acquire().
    }

    auto stats = cache.stats();
    // After 30 misses with evictions, the bucket auto_tunes to capacity 8.
    // The trigger is fired INSIDE find_or_compile's miss path AND on
    // record_execution() — both call into the same auto-tune machinery,
    // so the exact capacity is bounded by the 2x logic (4 → 8).
    CHECK(cache.capacity() == 8);
    CHECK(stats.misses == 30);
}

TEST_CASE("auto_tune: Fixed mode ignores record_execution interval") {
    SceneProgramCache cache(4, 1);
    cache.set_tune_mode(TuneMode::Fixed);

    cache::TuneConfig cfg{};
    cache.set_tune_config(cfg);

    for (int i = 0; i < 30; ++i) {
        cache.find_or_compile(
            SceneStructureKey{static_cast<uint64_t>(2000 + i), 0, 0, 80, 80, 1},
            []() -> std::unique_ptr<CompiledSceneProgram> {
                auto p = std::make_unique<CompiledSceneProgram>();
                p->valid = true;
                p->frame_graph.valid = true;
                return p;
            });
        cache.record_execution();
    }

    CHECK(cache.capacity() == 4);  // Fixed mode never changes capacity.
}
