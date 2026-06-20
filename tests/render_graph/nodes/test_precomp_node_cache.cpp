// =============================================================================
// test_precomp_node_cache.cpp — PrecompNode + SceneProgramCache integration
//
// Tests that PrecompNode::execute() uses the inner SceneProgramCache to avoid
// redundant graph rebuilding when the nested composition's structure is
// unchanged across frames.
//
// Test cases:
//   1. Cache hit on repeated execution — same key → same program, compile=1
//   2. Cache miss on different composition — different name → recompile
//   3. Eviction callback forwarding — inner eviction fires node's callback
//   4. Parameter block warm-up — binding count matches after execute
//   5. Multi-frame stability — 10 frames with same structure → 1 compile
//   6. Duration boundary — nested_frame >= m_duration returns empty
//   7. Invalidate entry — explicit invalidation clears inner cache
//   8. Auto-tuning — capacity doubles on many misses, halves on high hit rate
// =============================================================================

#include <doctest/doctest.h>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

#include <chronon3d/render_graph/nodes/precomp_node.hpp>
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
// Returns a composition that draws a colored rect. The color lets tests
// distinguish between different compositions at the cache level.
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

// ── Helper: minimal RenderGraphContext for PrecompNode tests ───────────────
struct TestContext {
    SoftwareRenderer            backend;
    std::shared_ptr<FramebufferPool> pool;
    NodeCache                   node_cache;
    CompositionRegistry         registry;
    RenderGraphContext          ctx;

    TestContext(int w = 200, int h = 200)
        : pool(std::make_shared<FramebufferPool>(128))
    {
        ctx.resources.backend         = &backend;
        ctx.resources.node_cache      = &node_cache;
        ctx.resources.framebuffer_pool = pool;
        ctx.resources.registry        = &registry;
        ctx.frame                     = {Frame{0}, 0.0f, 30, w, h};
    }

    void add_comp(const char* name, int w, int h, Color color) {
        registry.add(name, [=](const CompositionProps&) {
            return make_inner_comp(name, w, h, color);
        });
    }

    void set_frame(Frame f) { ctx.frame.frame = f; }
};

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// §1: Cache hit on repeated execution — same pointer, compile_count=1
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("precomp_cache: cache hit returns same program on repeated execute") {
    TestContext tc;
    tc.add_comp("inner", 80, 80, Color::blue());

    PrecompNode precomp("inner", Frame{0}, Frame{60});

    // First execution → cache miss
    auto fb0 = precomp.execute(tc.ctx, {}, {});
    REQUIRE(fb0 != nullptr);
    REQUIRE(fb0->width() == 200);
    REQUIRE(fb0->height() == 200);

    auto stats = precomp.inner_cache().stats();
    CHECK(stats.misses == 1);
    CHECK(stats.hits == 0);
    CHECK(stats.current_size == 1);

    // Second execution, same frame → cache hit
    auto fb1 = precomp.execute(tc.ctx, {}, {});
    REQUIRE(fb1 != nullptr);

    stats = precomp.inner_cache().stats();
    CHECK(stats.misses == 1);       // still 1 miss
    CHECK(stats.hits >= 1);         // at least 1 hit
    CHECK(stats.current_size == 1); // still 1 entry
}

TEST_CASE("precomp_cache: different frame with same structure → cache hit") {
    TestContext tc;
    tc.add_comp("inner", 80, 80, Color::blue());

    PrecompNode precomp("inner", Frame{0}, Frame{60});

    // Frame 0 → miss
    tc.set_frame(Frame{0});
    precomp.execute(tc.ctx, {}, {});
    CHECK(precomp.inner_cache().stats().misses == 1);
    CHECK(precomp.inner_cache().stats().hits == 0);

    // Frame 1 → hit (structure unchanged)
    tc.set_frame(Frame{1});
    precomp.execute(tc.ctx, {}, {});
    CHECK(precomp.inner_cache().stats().misses == 1);
    CHECK(precomp.inner_cache().stats().hits >= 1);

    // Frame 2 → hit again
    tc.set_frame(Frame{2});
    precomp.execute(tc.ctx, {}, {});
    CHECK(precomp.inner_cache().stats().misses == 1);
    CHECK(precomp.inner_cache().stats().hits >= 2);
}

// ═════════════════════════════════════════════════════════════════════════════
// §2: Cache miss on different composition → recompilation
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("precomp_cache: different composition name triggers recompile") {
    TestContext tc;
    tc.add_comp("comp_a", 80, 80, Color::red());
    tc.add_comp("comp_b", 80, 80, Color::blue());

    // Execute comp_a → cache miss
    {
        PrecompNode precomp("comp_a", Frame{0}, Frame{60});
        precomp.execute(tc.ctx, {}, {});
        CHECK(precomp.inner_cache().stats().misses == 1);
    }

    // Execute comp_b → new cache (different node instance), should miss
    {
        PrecompNode precomp("comp_b", Frame{0}, Frame{60});
        precomp.execute(tc.ctx, {}, {});
        CHECK(precomp.inner_cache().stats().misses == 1);
    }
}

TEST_CASE("precomp_cache: nonexistent composition returns empty fb") {
    TestContext tc;
    // No composition registered with name "ghost"
    PrecompNode precomp("ghost", Frame{0}, Frame{60});

    auto fb = precomp.execute(tc.ctx, {}, {});
    REQUIRE(fb != nullptr);
    // Should return an empty (uncleared) framebuffer of the parent size
    CHECK(fb->width() == 200);
    CHECK(fb->height() == 200);
    // No cache activity for nonexistent comp
    CHECK(precomp.inner_cache().stats().misses == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// §3: Eviction callback forwarding
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("precomp_cache: eviction callback fires on inner cache evict") {
    TestContext tc;
    tc.add_comp("inner", 80, 80, Color::green());

    // Small cache (capacity 2, single shard) to force eviction
    PrecompNode precomp("inner", Frame{0}, Frame{60}, Frame{-1}, 2);

    int evict_count = 0;
    precomp.set_on_evict([&](const graph::SceneStructureKey&) {
        ++evict_count;
    });

    // Insert 2 valid entries into the capacity-2 cache.
    precomp.inner_cache().find_or_compile(
        SceneStructureKey{1, 0, 0, 80, 80, 1},
        []() -> std::unique_ptr<CompiledSceneProgram> {
            auto p = std::make_unique<CompiledSceneProgram>();
            p->valid = true;
            p->frame_graph.valid = true;
            return p;
        });
    precomp.inner_cache().find_or_compile(
        SceneStructureKey{2, 0, 0, 80, 80, 1},
        []() -> std::unique_ptr<CompiledSceneProgram> {
            auto p = std::make_unique<CompiledSceneProgram>();
            p->valid = true;
            p->frame_graph.valid = true;
            return p;
        });
    CHECK(evict_count == 0);  // cache not full yet
    CHECK(precomp.inner_cache().size() == 2);

    // Insert 3rd entry → evicts LRU (key 1).
    precomp.inner_cache().find_or_compile(
        SceneStructureKey{3, 0, 0, 80, 80, 1},
        []() -> std::unique_ptr<CompiledSceneProgram> {
            auto p = std::make_unique<CompiledSceneProgram>();
            p->valid = true;
            p->frame_graph.valid = true;
            return p;
        });
    CHECK(evict_count == 1);  // LRU eviction fired callback
    CHECK(precomp.inner_cache().size() == 2);

    // erase() also fires callback
    precomp.inner_cache().erase(SceneStructureKey{3, 0, 0, 80, 80, 1});
    CHECK(evict_count == 2);
}

TEST_CASE("precomp_cache: no crash when eviction callback not set") {
    TestContext tc;
    tc.add_comp("inner", 80, 80, Color::blue());

    PrecompNode precomp("inner", Frame{0}, Frame{60});
    // Don't set any callback — should not crash on eviction
    precomp.execute(tc.ctx, {}, {});
    CHECK(precomp.inner_cache().stats().hits == 0);
    CHECK(precomp.inner_cache().stats().misses == 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// §4: Parameter block warm-up
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("precomp_cache: parameter block sized after execution") {
    TestContext tc;
    tc.add_comp("inner", 80, 80, Color::red());

    PrecompNode precomp("inner", Frame{0}, Frame{60});

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

    PrecompNode precomp("inner", Frame{0}, Frame{60});

    for (int f = 0; f < 10; ++f) {
        tc.set_frame(Frame{f});
        precomp.execute(tc.ctx, {}, {});
    }

    auto stats = precomp.inner_cache().stats();
    CHECK(stats.misses == 1);  // compiled only once
    CHECK(stats.hits >= 9);    // 9 subsequent hits
    CHECK(stats.current_size == 1);  // single entry
}

// ═════════════════════════════════════════════════════════════════════════════
// §6: Duration boundary
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("precomp_cache: nested_frame past duration returns empty") {
    TestContext tc;
    tc.add_comp("inner", 80, 80, Color::blue());

    PrecompNode precomp("inner", Frame{0}, Frame{30});  // duration 30

    // Frame 0 → within duration → rendered (cache miss)
    tc.set_frame(Frame{0});
    auto fb0 = precomp.execute(tc.ctx, {}, {});
    REQUIRE(fb0 != nullptr);
    CHECK(precomp.inner_cache().stats().misses == 1);

    // Frame 30 → nested_frame = 30, which is >= duration 30 → empty
    tc.set_frame(Frame{30});
    auto fb30 = precomp.execute(tc.ctx, {}, {});
    REQUIRE(fb30 != nullptr);
    // No cache activity — returned early before cache lookup
    CHECK(precomp.inner_cache().stats().misses == 1);

    // Frame 31 → also past duration
    tc.set_frame(Frame{31});
    auto fb31 = precomp.execute(tc.ctx, {}, {});
    REQUIRE(fb31 != nullptr);
    CHECK(precomp.inner_cache().stats().misses == 1);
}

TEST_CASE("precomp_cache: negative nested_frame returns empty") {
    TestContext tc;
    tc.add_comp("inner", 80, 80, Color::blue());

    PrecompNode precomp("inner", Frame{10}, Frame{30});  // starts at frame 10

    // Frame 0 → nested_frame = -10 → returns empty
    tc.set_frame(Frame{0});
    auto fb = precomp.execute(tc.ctx, {}, {});
    REQUIRE(fb != nullptr);
    CHECK(precomp.inner_cache().stats().misses == 0);  // no cache lookup

    // Frame 10 → nested_frame = 0 → renders
    tc.set_frame(Frame{10});
    fb = precomp.execute(tc.ctx, {}, {});
    REQUIRE(fb != nullptr);
    CHECK(precomp.inner_cache().stats().misses == 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// §7: Invalidate entry clears inner cache
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("precomp_cache: invalidate entry clears cached program") {
    TestContext tc;
    tc.add_comp("inner", 80, 80, Color::blue());

    PrecompNode precomp("inner", Frame{0}, Frame{60});

    // Execute → cache miss
    precomp.execute(tc.ctx, {}, {});
    CHECK(precomp.inner_cache().stats().current_size == 1);

    // Invalidate the entry using the key
    SceneHasher hasher;
    Scene nested_scene = tc.registry.create("inner").evaluate(Frame{0});
    SceneStructureKey key;
    key.topology_hash = hasher.compute_structure_fingerprint(nested_scene);
    key.active_set_hash = hasher.compute_active_at_fingerprint(nested_scene, Frame{0});
    key.render_options_hash = hash_combine(0, static_cast<uint64_t>(tc.ctx.options.ssaa_factor));
    key.width = 80;
    key.height = 80;

    precomp.invalidate_entry(key);
    CHECK(precomp.inner_cache().stats().current_size == 0);

    // Next execution → cache miss again (recompiles)
    precomp.execute(tc.ctx, {}, {});
    CHECK(precomp.inner_cache().stats().misses == 2);  // second compile
}

// ═════════════════════════════════════════════════════════════════════════════
// End-to-end via SoftwareRenderer (integration smoke test)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("precomp_cache: end-to-end via SoftwareRenderer does not crash") {
    // Verify that the cached PrecompNode works correctly within the full
    // render pipeline (SoftwareRenderer::render_frame).
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
// §8: Auto-tuning — SceneProgramCache capacity auto-adjustment
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("auto_tune: many misses with evictions double capacity") {
    // Create a small cache with Auto tuning.
    // Single shard ensures deterministic eviction behavior.
    SceneProgramCache cache(4, 1);
    cache.set_tune_mode(TuneMode::Auto);

    TuneConfig cfg;
    cfg.interval     = 30;
    cfg.min_capacity = 2;
    cfg.max_capacity = 128;
    cache.set_tune_config(cfg);

    // No data yet → auto_tune is a no-op.
    cache.auto_tune();
    CHECK(cache.capacity() == 4);

    // Insert 30 different keys — all miss, first 4 fill the cache,
    // keys 5-30 each cause an LRU eviction (26 evictions total).
    for (int i = 0; i < 30; ++i) {
        cache.find_or_compile(
            SceneStructureKey{
                .topology_hash = static_cast<uint64_t>(1000 + i),
                .active_set_hash = 0,
                .render_options_hash = 0,
                .width = 80,
                .height = 80,
                .ssaa_factor = 1
            },
            []() -> std::unique_ptr<CompiledSceneProgram> {
                auto p = std::make_unique<CompiledSceneProgram>();
                p->valid = true;
                p->frame_graph.valid = true;
                return p;
            });
    }

    // Verify: hit_rate=0% (zero hits), evictions=26, miss_rate=100%
    auto stats = cache.stats();
    CHECK(stats.hits == 0);
    CHECK(stats.misses == 30);
    CHECK(stats.evictions >= 26);

    // auto_tune should double capacity (4→8) because:
    //   Condition 1: evictions > 0 && hit_rate < 80% ✓
    cache.auto_tune();
    CHECK(cache.capacity() == 8);

    // After tuning, internal counters should be reset
    stats = cache.stats();
    CHECK(stats.hits == 0);
    CHECK(stats.misses == 0);
    CHECK(stats.evictions == 0);
}

TEST_CASE("auto_tune: high hit rate with zero evictions halves capacity") {
    SceneProgramCache cache(8, 1);
    cache.set_tune_mode(TuneMode::Auto);

    TuneConfig cfg;
    cfg.interval     = 20;
    cfg.min_capacity = 2;
    cfg.max_capacity = 128;
    cache.set_tune_config(cfg);

    // Insert one key (1 miss).
    const auto key = SceneStructureKey{
        .topology_hash = 42,
        .active_set_hash = 0,
        .render_options_hash = 0,
        .width = 80,
        .height = 80,
        .ssaa_factor = 1
    };
    cache.find_or_compile(key, []() -> std::unique_ptr<CompiledSceneProgram> {
        auto p = std::make_unique<CompiledSceneProgram>();
        p->valid = true;
        p->frame_graph.valid = true;
        return p;
    });

    // Hit the same key 19 more times (19 hits).
    // Total: 19 hits + 1 miss = 20 ops, hit_rate = 95%.
    for (int i = 0; i < 19; ++i) {
        cache.find_or_compile(key, []() -> std::unique_ptr<CompiledSceneProgram> {
            auto p = std::make_unique<CompiledSceneProgram>();
            p->valid = true;
            p->frame_graph.valid = true;
            return p;
        });
    }

    // Verify: evictions=0, hit_rate=95%
    auto stats = cache.stats();
    CHECK(stats.hits == 19);
    CHECK(stats.misses == 1);
    CHECK(stats.evictions == 0);

    // auto_tune should halve capacity (8→4) because:
    //   Condition 3: evictions == 0 && hit_rate >= 95% && capacity > min ✓
    cache.auto_tune();
    CHECK(cache.capacity() == 4);
}

TEST_CASE("auto_tune: no data yet does not change capacity") {
    SceneProgramCache cache(8, 1);
    cache.set_tune_mode(TuneMode::Auto);

    TuneConfig cfg;
    cfg.interval     = 1;
    cfg.min_capacity = 2;
    cfg.max_capacity = 128;
    cache.set_tune_config(cfg);

    // No find_or_compile calls → total_ops == 0 → no change
    cache.auto_tune();
    CHECK(cache.capacity() == 8);
}

TEST_CASE("auto_tune: Fixed mode does not change capacity") {
    SceneProgramCache cache(4, 1);
    cache.set_tune_mode(TuneMode::Fixed);  // explicit Fixed

    // Fill cache with many misses (would trigger doubling in Auto mode)
    for (int i = 0; i < 30; ++i) {
        cache.find_or_compile(
            SceneStructureKey{
                .topology_hash = static_cast<uint64_t>(2000 + i),
                .active_set_hash = 0,
                .render_options_hash = 0,
                .width = 80,
                .height = 80,
                .ssaa_factor = 1
            },
            []() -> std::unique_ptr<CompiledSceneProgram> {
                auto p = std::make_unique<CompiledSceneProgram>();
                p->valid = true;
                p->frame_graph.valid = true;
                return p;
            });
    }

    // auto_tune should NOT change capacity in Fixed mode
    cache.auto_tune();
    CHECK(cache.capacity() == 4);
}

TEST_CASE("auto_tune: respects max capacity bound") {
    SceneProgramCache cache(4, 1);
    cache.set_tune_mode(TuneMode::Auto);

    TuneConfig cfg;
    cfg.interval     = 30;
    cfg.min_capacity = 2;
    cfg.max_capacity = 6;  // small max to test bound
    cache.set_tune_config(cfg);

    // Many misses with evictions should double (4→8) but capped at 6
    for (int i = 0; i < 30; ++i) {
        cache.find_or_compile(
            SceneStructureKey{
                .topology_hash = static_cast<uint64_t>(3000 + i),
                .active_set_hash = 0,
                .render_options_hash = 0,
                .width = 80,
                .height = 80,
                .ssaa_factor = 1
            },
            []() -> std::unique_ptr<CompiledSceneProgram> {
                auto p = std::make_unique<CompiledSceneProgram>();
                p->valid = true;
                p->frame_graph.valid = true;
                return p;
            });
    }

    cache.auto_tune();
    CHECK(cache.capacity() == 6);  // capped by max_capacity
}

TEST_CASE("auto_tune: respects min capacity bound") {
    SceneProgramCache cache(8, 1);
    cache.set_tune_mode(TuneMode::Auto);

    TuneConfig cfg;
    cfg.interval     = 20;
    cfg.min_capacity = 3;  // larger min to test bound
    cfg.max_capacity = 128;
    cache.set_tune_config(cfg);

    // High hit rate with zero evictions → halve (8→4) but capped at min 3
    const auto key = SceneStructureKey{
        .topology_hash = 42,
        .active_set_hash = 0,
        .render_options_hash = 0,
        .width = 80,
        .height = 80,
        .ssaa_factor = 1
    };
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
    // 8/2 = 4, but min_capacity=3, so result is 4 (not capped)
    // To test the min bound, we'd need capacity/2 < min
    // After first halve: 8→4 (min 3, so 4 is fine)
    CHECK(cache.capacity() == 4);

    // Call auto_tune again with fresh stats → halve again: 4→2, but capped at 3
    // Need to set up fresh high hit rate stats
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
    CHECK(cache.capacity() == 3);  // 4/2=2 < min=3 → capped at 3
}

TEST_CASE("auto_tune: PrecompNode integration — capacity changes after interval") {
    // Test that PrecompNode with auto-tuning actually changes the cache
    // capacity after enough executions.
    TestContext tc;
    tc.add_comp("inner", 80, 80, Color::blue());

    // PrecompNode with capacity 4, Auto tuning, interval 3, min 2, max 128
    PrecompNode precomp("inner", Frame{0}, Frame{60}, Frame{-1},
                        4,                        // cache_capacity
                        TuneMode::Auto,            // tune_mode
                        3,                         // tune_interval (every 3 executes)
                        2,                         // tune_min_cap
                        128);                      // tune_max_cap

    CHECK(precomp.inner_cache().capacity() == 4);

    // Execute 3 frames with different structures → 3 misses → interval hit
    // We need different compositions to trigger misses (same structure = hit)
    // Since our test composition is always a rect, structure is identical,
    // so all frames after the first are hits.  This tests Condition 3 (shrink).

    // Frame 0: miss (first compile)
    tc.set_frame(Frame{0});
    precomp.execute(tc.ctx, {}, {});
    CHECK(precomp.inner_cache().stats().misses == 1);

    // Frame 1: hit (same structure)
    tc.set_frame(Frame{1});
    precomp.execute(tc.ctx, {}, {});
    CHECK(precomp.inner_cache().stats().hits >= 1);

    // Frame 2: hit (counter hits tune_interval=3, runs auto_tune)
    // auto_tune sees: hits=1, misses=1, evictions=0, hit_rate=50%
    // None of the conditions fire (no evictions, hit_rate < 95%)
    // → capacity stays 4
    tc.set_frame(Frame{2});
    precomp.execute(tc.ctx, {}, {});
    CHECK(precomp.inner_cache().capacity() == 4);

    // After auto_tune, counters are reset. Execute 3 more frames with hits.
    // Frame 3: hit (first after reset)
    tc.set_frame(Frame{3});
    precomp.execute(tc.ctx, {}, {});

    // Frame 4: hit
    tc.set_frame(Frame{4});
    precomp.execute(tc.ctx, {}, {});

    // Frame 5: hit, counter hits interval=3, runs auto_tune
    // After reset + 3 hits: hits=3, misses=0, evictions=0, hit_rate=100%
    // Condition 3 fires (evictions==0, hit_rate >= 95%, capacity 4 > min 2)
    // → capacity halves to 4/2 = 2
    tc.set_frame(Frame{5});
    precomp.execute(tc.ctx, {}, {});
    CHECK(precomp.inner_cache().capacity() == 2);
}
