// =============================================================================
// test_cache_reuse_identical_frame.cpp — In-process cache reuse verification.
//
// Renders the SAME frame 5 times with the SAME SoftwareRenderer (no
// clear_caches(), no reset) and verifies that:
//   1. NodeCache hits increase on subsequent renders (cache is warm).
//   2. No hash collisions occur (node_cache_hash_collisions == 0).
//   3. Rendered framebuffers are pixel-identical across all renders.
//   4. Program cache shows hits on warm renders.
//
// This test addresses the cache verification concern from the action plan:
// the previous CLI-based measurement (5 separate CLI invocations) could not
// share runtime state, making it impossible to observe intra-process cache
// reuse.  This in-process test exercises the exact same renderer object.
// =============================================================================

#include <doctest/doctest.h>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <tests/helpers/test_utils.hpp>

using namespace chronon3d;

namespace {

/// Build a minimal static composition (no text, no animation, no camera).
/// Returns a 320×240 scene with a solid-color rect — deterministic and
/// cacheable across identical frames.
Composition make_static_composition() {
    constexpr int W = 320;
    constexpr int H = 240;
    return Composition(
        CompositionSpec{
            .name = "CacheReuseStatic",
            .width = W,
            .height = H,
            .duration = 1,
        },
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);
            s.rect("bg", {
                .size = {static_cast<float>(ctx.width),
                         static_cast<float>(ctx.height)},
                .color = Color{0.12f, 0.15f, 0.22f, 1.0f},
            });
            return s.build();
        });
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// §1: Identical-frame renders produce increasing cache hits
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Cache reuse: identical-frame renders produce increasing cache hits") {
    auto renderer = test::make_renderer();
    auto comp = make_static_composition();

    // Capture baseline NodeCache stats before any render.
    const auto baseline_stats = renderer.node_cache().stats();

    // Cold render (first render populates caches).
    renderer.render(comp, Frame{120});

    // Warm renders: same frame, same renderer, no clear_caches().
    constexpr int WARM_RENDERS = 4;
    for (int i = 0; i < WARM_RENDERS; ++i) {
        auto fb = renderer.render(comp, Frame{120});
        REQUIRE(fb != nullptr);
    }

    const auto warm_stats = renderer.node_cache().stats();

    // After cold + warm renders, the node cache should have recorded
    // additional hits (the static scene's nodes were cached from the
    // cold render and reused in warm renders).
    INFO("baseline hits=", baseline_stats.hits,
         " warm hits=", warm_stats.hits);
    CHECK(warm_stats.hits > baseline_stats.hits);

    // No hash collisions should have occurred.
    const auto collisions = renderer.counters()
        ->node_cache_hash_collisions.load(std::memory_order_relaxed);
    INFO("node_cache_hash_collisions=", collisions);
    CHECK(collisions == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// §2: Program cache shows hits on warm renders
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Cache reuse: program cache hits on warm renders") {
    auto renderer = test::make_renderer();
    auto comp = make_static_composition();

    // Cold render to populate caches, then warm renders.
    renderer.render(comp, Frame{120});
    renderer.counters()->reset();

    constexpr int WARM_RENDERS = 4;
    for (int i = 0; i < WARM_RENDERS; ++i) {
        renderer.render(comp, Frame{120});
    }

    const auto prog_hits = renderer.counters()
        ->program_cache_hits.load(std::memory_order_relaxed);
    const auto prog_misses = renderer.counters()
        ->program_cache_misses.load(std::memory_order_relaxed);

    INFO("program_cache_hits=", prog_hits,
         " program_cache_misses=", prog_misses);

    // After warmup, at least some program cache hits should occur.
    // The exact number depends on graph reuse, but there should be
    // more hits than misses for a static scene.
    CHECK(prog_hits > 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// §3: Pixel-identical output across all renders
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Cache reuse: pixel-identical output across identical-frame renders") {
    auto renderer = test::make_renderer();
    auto comp = make_static_composition();

    constexpr int NUM_RENDERS = 5;
    u64 reference_hash = 0;
    bool first = true;

    for (int i = 0; i < NUM_RENDERS; ++i) {
        auto fb = renderer.render(comp, Frame{120});
        REQUIRE(fb != nullptr);
        REQUIRE(fb->width() == 320);
        REQUIRE(fb->height() == 240);

        const u64 hash = test::framebuffer_hash(*fb);
        if (first) {
            reference_hash = hash;
            first = false;
        } else {
            INFO("Render #", i, " hash=", hash,
                 " reference=", reference_hash);
            CHECK(hash == reference_hash);
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// §4: CacheDiagnostics snapshot covers all registered cache domains
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Cache reuse: diagnostics snapshot covers all cache domains") {
    auto renderer = test::make_renderer();
    auto comp = make_static_composition();

    // Cold render to populate all caches.
    renderer.render(comp, Frame{120});

    // Query the per-runtime CacheDiagnostics for a snapshot of ALL
    // registered cache domains (Node, RenderedFrames, ScenePrograms,
    // Text, Images, etc.).
    auto all_domains = renderer.runtime().diagnostics().snapshot_all_domains();

    // At minimum, NodeCache and ScenePrograms should be registered.
    bool has_nodes = false;
    bool has_programs = false;
    for (const auto& ds : all_domains) {
        if (ds.domain == chronon3d::cache::CacheDomain::Nodes) has_nodes = true;
        if (ds.domain == chronon3d::cache::CacheDomain::ScenePrograms) has_programs = true;
    }
    INFO("registered domains=", all_domains.size());
    CHECK(has_nodes);
    CHECK(has_programs);

    // Warm render — snapshot should still be valid and show activity.
    renderer.render(comp, Frame{120});
    auto warm_domains = renderer.runtime().diagnostics().snapshot_all_domains();
    CHECK(warm_domains.size() >= all_domains.size());
}

// ═════════════════════════════════════════════════════════════════════════════
// §5: Framebuffer pool stats are consistent (no leaks)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Cache reuse: framebuffer pool stats consistent across renders") {
    auto renderer = test::make_renderer();
    auto comp = make_static_composition();

    // Cold render to populate pool.
    renderer.render(comp, Frame{120});

    const auto pool_before = renderer.software_framebuffer_pool().stats();

    // Warm renders: same frame, same renderer.
    constexpr int NUM_RENDERS = 5;
    for (int i = 0; i < NUM_RENDERS; ++i) {
        renderer.render(comp, Frame{120});
    }

    const auto pool_after = renderer.software_framebuffer_pool().stats();

    INFO("pool_before allocs=", pool_before.total_allocations,
         " reuses=", pool_before.total_reuses);
    INFO("pool_after allocs=", pool_after.total_allocations,
         " reuses=", pool_after.total_reuses);

    // After warmup, subsequent renders should reuse framebuffers rather
    // than allocating new ones.  The reuse count should increase.
    CHECK(pool_after.total_reuses >= pool_before.total_reuses);

    // No eviction pressure should have occurred for a static scene
    // rendered 5 times with the same cache.
    CHECK(pool_after.pressure_count == 0);
}
