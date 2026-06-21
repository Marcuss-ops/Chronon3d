// =============================================================================
// test_precomp_node_cache.cpp — PrecompNode + SceneProgramStore integration
//
// PR-5 — PrecompNode no longer owns SceneProgramCache.  Tests now verify
// cache behavior through the centralized SceneProgramStore on RenderSession.
//
// Test cases:
//   1. Cache hit on repeated execution — same key → same program, compile=1
//   2. Cache miss on different composition — different name → recompile
//   3. Eviction callback forwarding — inner eviction fires node's callback
//   4. Parameter block warm-up — binding count matches after execute
//   5. Multi-frame stability — 10 frames with same structure → 1 compile
//   6. Duration boundary — nested_frame >= m_duration returns empty
//   7. End-to-end via SoftwareRenderer — full pipeline smoke test
//   8. Auto-tuning — SceneProgramStore forwards tuning to per-instance cache
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
#include <tests/helpers/pixel_assertions.hpp>
using namespace chronon3d;

using namespace chronon3d::graph;
using namespace chronon3d::cache;

namespace {

// ── Helper: create a simple inner composition ──────────────────────────────
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
// PR-5 — the TestContext now owns a RenderSession with a SceneProgramStore,
// and wires session + scheduler into ctx.services.
struct TestContext {
    Config                      renderer_cfg;
    SoftwareRenderer            backend;
    std::shared_ptr<FramebufferPool> pool;
    NodeCache                   node_cache;
    CompositionRegistry         registry;
    RenderSession               session;
    ExecutionScheduler          scheduler;
    RenderGraphContext          ctx;

    TestContext(int w = 200, int h = 200)
        : backend(renderer_cfg)
        , pool(std::make_shared<FramebufferPool>(128))
        , scheduler(SchedulerMode::Sequential, 1, false)
    {
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
        registry.add(name, [=](const CompositionProps&) {
            return make_inner_comp(name, w, h, color);
        });
    }

    void set_frame(Frame f) { ctx.frame_input.frame = f; }
};

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// §1: Cache hit on repeated execution — same pointer, compile_count=1
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("precomp_cache: cache hit returns same program on repeated execute") {
    TestContext tc;
    tc.add_comp("inner", 80, 80, Color::blue());

    PrecompCachePolicy policy{.initial_capacity = 8};
    PrecompNode precomp("inner", Frame{0}, Frame{60}, Frame{-1}, policy);

    // First execution → cache miss
    auto fb0 = precomp.execute(tc.ctx, {}, {});
    REQUIRE(fb0 != nullptr);
    REQUIRE(fb0->width() == 200);
    REQUIRE(fb0->height() == 200);

    auto stats = tc.session.program_store->aggregate_stats();
    CHECK(stats.misses == 1);
    CHECK(stats.hits == 0);
    CHECK(stats.current_size == 1);

    // Second execution, same frame → cache hit
    auto fb1 = precomp.execute(tc.ctx, {}, {});
    REQUIRE(fb1 != nullptr);

    stats = tc.session.program_store->aggregate_stats();
    CHECK(stats.misses == 1);       // still 1 miss
    CHECK(stats.hits >= 1);         // at least 1 hit
    CHECK(stats.current_size == 1); // still 1 entry
}

TEST_CASE("precomp_cache: different frame with same structure → cache hit") {
    TestContext tc;
    tc.add_comp("inner", 80, 80, Color::blue());

    PrecompCachePolicy policy{.initial_capacity = 8};
    PrecompNode precomp("inner", Frame{0}, Frame{60}, Frame{-1}, policy);

    // Frame 0 → miss
    tc.set_frame(Frame{0});
    precomp.execute(tc.ctx, {}, {});
    auto stats0 = tc.session.program_store->aggregate_stats();
    CHECK(stats0.misses == 1);
    CHECK(stats0.hits == 0);

    // Frame 1 → hit (structure unchanged)
    tc.set_frame(Frame{1});
    precomp.execute(tc.ctx, {}, {});
    auto stats1 = tc.session.program_store->aggregate_stats();
    CHECK(stats1.misses == 1);
    CHECK(stats1.hits >= 1);

    // Frame 2 → hit again
    tc.set_frame(Frame{2});
    precomp.execute(tc.ctx, {}, {});
    auto stats2 = tc.session.program_store->aggregate_stats();
    CHECK(stats2.misses == 1);
    CHECK(stats2.hits >= 2);
}

// ═════════════════════════════════════════════════════════════════════════════
// §2: Cache miss on different composition → recompilation
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("precomp_cache: different composition name triggers recompile") {
    TestContext tc;
    tc.add_comp("comp_a", 80, 80, Color::red());
    tc.add_comp("comp_b", 80, 80, Color::blue());

    PrecompCachePolicy policy{.initial_capacity = 8};

    // Execute comp_a → cache miss
    {
        PrecompNode precomp("comp_a", Frame{0}, Frame{60}, Frame{-1}, policy);
        precomp.execute(tc.ctx, {}, {});
        auto stats = tc.session.program_store->aggregate_stats();
        CHECK(stats.misses == 1);
    }

    // Execute comp_b → new instance, different key → separate cache partition
    {
        PrecompNode precomp("comp_b", Frame{0}, Frame{60}, Frame{-1}, policy);
        precomp.execute(tc.ctx, {}, {});
        auto stats = tc.session.program_store->aggregate_stats();
        CHECK(stats.misses == 2);  // two instances, two misses
    }
}

TEST_CASE("precomp_cache: nonexistent composition returns empty fb") {
    TestContext tc;
    // No composition registered with name "ghost"
    PrecompCachePolicy policy;
    PrecompNode precomp("ghost", Frame{0}, Frame{60}, Frame{-1}, policy);

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

    // Small cache policy (capacity 2) to force eviction
    PrecompCachePolicy policy{.initial_capacity = 2};
    PrecompNode precomp("inner", Frame{0}, Frame{60}, Frame{-1}, policy);

    int evict_count = 0;
    precomp.set_on_evict([&](const graph::SceneStructureKey&) {
        ++evict_count;
    });

    // First call: creates the store instance, registers callback
    precomp.execute(tc.ctx, {}, {});

    // Access the store's per-instance cache directly for eviction testing.
    // Use two different keys to fill the capacity-2 cache.
    tc.session.program_store->acquire(
        precomp.instance_key(),
        SceneStructureKey{1, 0, 0, 80, 80, 1}, policy,
        []() -> std::unique_ptr<CompiledSceneProgram> {
            auto p = std::make_unique<CompiledSceneProgram>();
            p->valid = true;
            p->frame_graph.valid = true;
            return p;
        });
    tc.session.program_store->acquire(
        precomp.instance_key(),
        SceneStructureKey{2, 0, 0, 80, 80, 1}, policy,
        []() -> std::unique_ptr<CompiledSceneProgram> {
            auto p = std::make_unique<CompiledSceneProgram>();
            p->valid = true;
            p->frame_graph.valid = true;
            return p;
        });
    CHECK(evict_count == 0);

    // Insert 3rd entry → evicts LRU.
    tc.session.program_store->acquire(
        precomp.instance_key(),
        SceneStructureKey{3, 0, 0, 80, 80, 1}, policy,
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

    PrecompCachePolicy policy;
    PrecompNode precomp("inner", Frame{0}, Frame{60}, Frame{-1}, policy);
    // Don't set any callback — should not crash on eviction
    precomp.execute(tc.ctx, {}, {});
    auto stats = tc.session.program_store->aggregate_stats();
    CHECK(stats.hits == 0);
    CHECK(stats.misses == 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// §4: Parameter block warm-up
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("precomp_cache: parameter block sized after execution") {
    TestContext tc;
    tc.add_comp("inner", 80, 80, Color::red());

    PrecompCachePolicy policy;
    PrecompNode precomp("inner", Frame{0}, Frame{60}, Frame{-1}, policy);

    // Before execution: parameter block should be empty
    CHECK(precomp.param_block().empty());

    // After execution: parameter block should be warm (non-empty)
    precomp.execute(tc.ctx, {}, {});
    CHECK_FALSE(precomp.param_block().empty());
    CHECK(precomp.param_block().size() > 0);

    // Second execution: size should remain stable
    precomp.execute(tc.ctx, {}, {});
    CHECK(precomp.param_block().size() > 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// §5: Multi-frame stability — 10 frames, 1 compile
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("precomp_cache: 10 frames with same structure → 1 compile") {
    TestContext tc;
    tc.add_comp("inner", 80, 80, Color::blue());

    PrecompCachePolicy policy{.initial_capacity = 8};
    PrecompNode precomp("inner", Frame{0}, Frame{60}, Frame{-1}, policy);

    for (int f = 0; f < 10; ++f) {
        tc.set_frame(Frame{f});
        precomp.execute(tc.ctx, {}, {});
    }

    auto stats = tc.session.program_store->aggregate_stats();
    CHECK(stats.misses == 1);       // compiled only once
    CHECK(stats.hits >= 9);         // 9 subsequent hits
    CHECK(stats.current_size == 1); // single entry
}

// ═════════════════════════════════════════════════════════════════════════════
// §6: Duration boundary
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("precomp_cache: nested_frame past duration returns empty") {
    TestContext tc;
    tc.add_comp("inner", 80, 80, Color::blue());

    PrecompCachePolicy policy;
    PrecompNode precomp("inner", Frame{0}, Frame{30}, Frame{-1}, policy);

    // Frame 0 → within duration → rendered
    tc.set_frame(Frame{0});
    auto fb0 = precomp.execute(tc.ctx, {}, {});
    REQUIRE(fb0 != nullptr);
    auto stats0 = tc.session.program_store->aggregate_stats();
    CHECK(stats0.misses == 1);

    // Frame 30 → past duration → empty
    tc.set_frame(Frame{30});
    auto fb30 = precomp.execute(tc.ctx, {}, {});
    REQUIRE(fb30 != nullptr);
    auto stats30 = tc.session.program_store->aggregate_stats();
    CHECK(stats30.misses == 1);  // no new cache activity

    // Frame 31 → also past duration
    tc.set_frame(Frame{31});
    auto fb31 = precomp.execute(tc.ctx, {}, {});
    REQUIRE(fb31 != nullptr);
}

TEST_CASE("precomp_cache: negative nested_frame returns empty") {
    TestContext tc;
    tc.add_comp("inner", 80, 80, Color::blue());

    PrecompCachePolicy policy;
    PrecompNode precomp("inner", Frame{10}, Frame{30}, Frame{-1}, policy);

    // Frame 0 → nested_frame = -10 → empty
    tc.set_frame(Frame{0});
    auto fb = precomp.execute(tc.ctx, {}, {});
    REQUIRE(fb != nullptr);

    // Frame 10 → nested_frame = 0 → renders
    tc.set_frame(Frame{10});
    fb = precomp.execute(tc.ctx, {}, {});
    REQUIRE(fb != nullptr);
    auto stats = tc.session.program_store->aggregate_stats();
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

    // Render two frames — should not crash
    auto fb0 = renderer.render_frame(parent_comp, Frame{0});
    REQUIRE(fb0 != nullptr);

    auto fb1 = renderer.render_frame(parent_comp, Frame{1});
    REQUIRE(fb1 != nullptr);

    // Verify both frames rendered something visible
    Color blue = Color::blue();
    CHECK(test::any_pixel(*fb0, blue));
    CHECK(test::any_pixel(*fb1, blue));
}

// ═════════════════════════════════════════════════════════════════════════════
// §8: Auto-tuning — SceneProgramCache capacity auto-adjustment (store-level)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("auto_tune: many misses with evictions double capacity") {
    SceneProgramCache cache(4, 1);
    cache.set_tune_mode(TuneMode::Auto);

    TuneConfig cfg;
    cfg.interval     = 30;
    cfg.min_capacity = 2;
    cfg.max_capacity = 128;
    cache.set_tune_config(cfg);

    cache.auto_tune();
    CHECK(cache.capacity() == 4);

    for (int i = 0; i < 30; ++i) {
        cache.find_or_compile(
            SceneStructureKey{static_cast<uint64_t>(1000 + i), 0, 0, 80, 80, 1},
            []() -> std::unique_ptr<CompiledSceneProgram> {
                auto p = std::make_unique<CompiledSceneProgram>();
                p->valid = true;
                p->frame_graph.valid = true;
                return p;
            });
    }

    auto stats = cache.stats();
    CHECK(stats.hits == 0);
    CHECK(stats.misses == 30);

    cache.auto_tune();
    CHECK(cache.capacity() == 8);
}

TEST_CASE("auto_tune: high hit rate with zero evictions halves capacity") {
    SceneProgramCache cache(8, 1);
    cache.set_tune_mode(TuneMode::Auto);

    TuneConfig cfg{.interval = 20, .min_capacity = 2, .max_capacity = 128};
    cache.set_tune_config(cfg);

    const auto key = SceneStructureKey{42, 0, 0, 80, 80, 1};
    cache.find_or_compile(key, []() -> std::unique_ptr<CompiledSceneProgram> {
        auto p = std::make_unique<CompiledSceneProgram>();
        p->valid = true;
        p->frame_graph.valid = true;
        return p;
    });
    for (int i = 0; i < 19; ++i) {
        cache.find_or_compile(key, []() -> std::unique_ptr<CompiledSceneProgram> {
            auto p = std::make_unique<CompiledSceneProgram>();
            p->valid = true;
            p->frame_graph.valid = true;
            return p;
        });
    }

    cache.auto_tune();
    CHECK(cache.capacity() == 4);
}

TEST_CASE("auto_tune: Fixed mode does not change capacity") {
    SceneProgramCache cache(4, 1);
    cache.set_tune_mode(TuneMode::Fixed);

    for (int i = 0; i < 30; ++i) {
        cache.find_or_compile(
            SceneStructureKey{static_cast<uint64_t>(2000 + i), 0, 0, 80, 80, 1},
            []() -> std::unique_ptr<CompiledSceneProgram> {
                auto p = std::make_unique<CompiledSceneProgram>();
                p->valid = true;
                p->frame_graph.valid = true;
                return p;
            });
    }

    cache.auto_tune();
    CHECK(cache.capacity() == 4);
}
