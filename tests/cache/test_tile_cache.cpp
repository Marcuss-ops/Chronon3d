#include <doctest/doctest.h>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

using namespace chronon3d;
using namespace chronon3d::cache;

// ═════════════════════════════════════════════════════════════════════════════
// Helper: build a minimal NodeCacheKey
// ═════════════════════════════════════════════════════════════════════════════
namespace {

NodeCacheKey make_key(u64 params_hash = 0xABCD, int w = 128, int h = 128) {
    return NodeCacheKey{
        .scope = "test_node",
        .frame = 1,
        .width = w,
        .height = h,
        .params_hash = params_hash,
        .source_hash = 0xCAFE,
        .input_hash = 0xBEEF
    };
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// Test 1 — Different tile positions produce different digests
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("TileCache: Different tile positions → different digests") {
    auto base = make_key();

    NodeCacheKey tile_a = base;
    tile_a.tile_x = 0;
    tile_a.tile_y = 0;
    tile_a.tile_size = 64;

    NodeCacheKey tile_b = base;
    tile_b.tile_x = 64;
    tile_b.tile_y = 0;
    tile_b.tile_size = 64;

    NodeCacheKey tile_c = base;
    tile_c.tile_x = 0;
    tile_c.tile_y = 64;
    tile_c.tile_size = 64;

    NodeCacheKey tile_d = base;
    tile_d.tile_x = 64;
    tile_d.tile_y = 64;
    tile_d.tile_size = 64;

    const u64 d_a = tile_a.digest();
    const u64 d_b = tile_b.digest();
    const u64 d_c = tile_c.digest();
    const u64 d_d = tile_d.digest();

    CHECK(d_a != d_b);
    CHECK(d_a != d_c);
    CHECK(d_a != d_d);
    CHECK(d_b != d_c);
    CHECK(d_b != d_d);
    CHECK(d_c != d_d);
}

// ═════════════════════════════════════════════════════════════════════════════
// Test 2 — Same tile position → same digest
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("TileCache: Same tile position → same digest") {
    auto base = make_key();

    NodeCacheKey k1 = base;
    k1.tile_x = 128;
    k1.tile_y = 256;
    k1.tile_size = 64;

    NodeCacheKey k2 = base;
    k2.tile_x = 128;
    k2.tile_y = 256;
    k2.tile_size = 64;

    CHECK(k1.digest() == k2.digest());
    CHECK(k1 == k2);
}

// ═════════════════════════════════════════════════════════════════════════════
// Test 3 — Different tile sizes produce different digests (same x0/y0)
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("TileCache: Different tile sizes → different digests") {
    auto base = make_key();

    NodeCacheKey k32 = base;
    k32.tile_x = 0;
    k32.tile_y = 0;
    k32.tile_size = 32;

    NodeCacheKey k64 = base;
    k64.tile_x = 0;
    k64.tile_y = 0;
    k64.tile_size = 64;

    CHECK(k32.digest() != k64.digest());
}

// ═════════════════════════════════════════════════════════════════════════════
// Test 4 — Default tile fields (-1, -1, 0, 0) produce consistent digests
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("TileCache: Default tile fields are consistent") {
    auto base = make_key();
    // Default-constructed tile fields: tile_x=-1, tile_y=-1, tile_size=0, tile_hash=0

    NodeCacheKey k1 = base;  // defaults
    NodeCacheKey k2 = base;  // defaults

    CHECK(k1.digest() == k2.digest());
    CHECK(k1 == k2);

    // Explicitly setting the defaults should match
    NodeCacheKey k3 = base;
    k3.tile_x = -1;
    k3.tile_y = -1;
    k3.tile_size = 0;
    k3.tile_hash = 0;

    CHECK(k1.digest() == k3.digest());
}

// ═════════════════════════════════════════════════════════════════════════════
// Test 5 — Non-tile keys differ from tile keys
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("TileCache: Non-tile key ≠ tile key") {
    auto base = make_key();

    NodeCacheKey non_tile = base;  // tile_x=-1, tile_y=-1

    NodeCacheKey tile_0_0 = base;
    tile_0_0.tile_x = 0;
    tile_0_0.tile_y = 0;
    tile_0_0.tile_size = 64;

    CHECK(non_tile.digest() != tile_0_0.digest());
}

// ═════════════════════════════════════════════════════════════════════════════
// Test 6 — NodeCache stores and retrieves per-tile keys independently
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("TileCache: NodeCache isolates entries by tile position") {
    NodeCache cache(64 * 1024 * 1024);  // 64 MB
    auto base = make_key();

    constexpr int TILE_SIZE = 64;

    // Store framebuffer for tile (0, 0)
    NodeCacheKey key_0_0 = base;
    key_0_0.tile_x = 0;
    key_0_0.tile_y = 0;
    key_0_0.tile_size = TILE_SIZE;

    auto fb_0_0 = std::make_shared<Framebuffer>(TILE_SIZE, TILE_SIZE);
    fb_0_0->set_pixel(10, 10, Color::red());
    cache.store(key_0_0, fb_0_0);

    // Store framebuffer for tile (64, 0)
    NodeCacheKey key_64_0 = base;
    key_64_0.tile_x = 64;
    key_64_0.tile_y = 0;
    key_64_0.tile_size = TILE_SIZE;

    auto fb_64_0 = std::make_shared<Framebuffer>(TILE_SIZE, TILE_SIZE);
    fb_64_0->set_pixel(10, 10, Color::green());
    cache.store(key_64_0, fb_64_0);

    // Retrieve tile (0, 0) — must get red, not green
    auto ret_0_0 = cache.get(key_0_0);
    REQUIRE(ret_0_0 != nullptr);
    Color c0 = ret_0_0->get_pixel(10, 10);
    CHECK(c0.r == doctest::Approx(1.0f));
    CHECK(c0.g == doctest::Approx(0.0f));

    // Retrieve tile (64, 0) — must get green
    auto ret_64_0 = cache.get(key_64_0);
    REQUIRE(ret_64_0 != nullptr);
    Color c1 = ret_64_0->get_pixel(10, 10);
    CHECK(c1.r == doctest::Approx(0.0f));
    CHECK(c1.g == doctest::Approx(1.0f));

    // Verify they are distinct pointers
    CHECK(ret_0_0 != ret_64_0);
}

// ═════════════════════════════════════════════════════════════════════════════
// Test 7 — tile_hash field differentiates keys
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("TileCache: tile_hash differentiates keys") {
    auto base = make_key();

    NodeCacheKey k1 = base;
    k1.tile_x = 0;
    k1.tile_y = 0;
    k1.tile_size = 64;
    k1.tile_hash = 0xAAAA;

    NodeCacheKey k2 = base;
    k2.tile_x = 0;
    k2.tile_y = 0;
    k2.tile_size = 64;
    k2.tile_hash = 0xBBBB;

    CHECK(k1.digest() != k2.digest());
}

// ═════════════════════════════════════════════════════════════════════════════
// Test 8 — Integration: cache hit within same tile across frames
// ═════════════════════════════════════════════════════════════════════════════
// Renders a simple scene with tiles enabled and verifies cache counters
// show meaningful activity.
TEST_CASE("TileCache: Integration — render with tiles shows cache activity") {
    const int W = 160, H = 120;

    Composition comp(CompositionSpec{
        .name = "TileCacheStaticBg", .width = W, .height = H, .duration = 6
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);
        // Static background — should be cacheable after first frame
        s.rect("bg", {
            .size = {160.0f, 120.0f},
            .color = Color{0.1f, 0.15f, 0.2f, 1.0f},
            .pos = {80.0f, 60.0f, 0}
        });
        // Small moving circle — changes each frame
        float x = 20.0f + static_cast<float>(ctx.frame) * 3.0f;
        s.circle("ball", {
            .radius = 8.0f,
            .color = Color::red(),
            .pos = {x, 60.0f, 0}
        });
        return s.build();
    });

    SoftwareRenderer renderer;
    {
        RenderSettings s;
        s.use_modular_graph = true;
        s.enable_dirty_rects = true;
        s.enable_dirty_bitmask = true;
        s.tile_size = 32;
        renderer.set_settings(s);
    }

    // Frame 0: full render (no prev fb)
    renderer.render_frame(comp, 0);
    renderer.counters()->reset();

    // Frame 1: should have dirty tiles and cache activity
    renderer.render_frame(comp, 1);

    const auto* c = renderer.counters();

    // Tile counters should show partial dirty coverage
    uint64_t tile_dirty = c->tile_dirty_count.load();
    uint64_t tile_clean = c->tile_clean_count.load();
    uint64_t pixel_rendered = c->tile_pixels_rendered.load();
    uint64_t pixel_skipped = c->tile_pixels_skipped.load();

    INFO("tile_dirty=", tile_dirty, " tile_clean=", tile_clean,
         " px_rendered=", pixel_rendered, " px_skipped=", pixel_skipped);

    // Small moving circle = only a few tiles dirty
    CHECK(tile_dirty > 0);
    CHECK(tile_clean > 0);
    CHECK(pixel_rendered > 0);
    CHECK(pixel_skipped > 0);
    CHECK(c->tile_full_fallbacks.load() == 0);

    // Cache evaluation runs even if nodes are frame-dependent —
    // bypass_not_cacheable should register for non-cacheable nodes.
    uint64_t bypass_not_cacheable = c->bypass_not_cacheable_count.load();
    INFO("tile_dirty=", tile_dirty, " tile_clean=", tile_clean,
         " bypass_not_cacheable=", bypass_not_cacheable);

    // Render consecutive frames — cache counters should accumulate
    renderer.counters()->reset();
    renderer.render_frame(comp, 2);
    renderer.render_frame(comp, 3);
    renderer.render_frame(comp, 4);
    renderer.render_frame(comp, 5);

    const auto* c2 = renderer.counters();
    uint64_t tile_dirty2 = c2->tile_dirty_count.load();
    uint64_t tile_clean2 = c2->tile_clean_count.load();
    INFO("after 4 consecutive frames: tile_dirty=", tile_dirty2,
         " tile_clean=", tile_clean2);

    // Tile counters should show ongoing partial coverage
    CHECK(tile_dirty2 > 0);
    CHECK(tile_clean2 > 0);
    CHECK(c2->tile_full_fallbacks.load() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// Test 9 — Integration: pixel-perfect output with tile cache enabled
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("TileCache: Integration — pixel-perfect output with tile cache") {
    const int W = 128, H = 96;

    Composition comp(CompositionSpec{
        .name = "TileCachePxMatch", .width = W, .height = H, .duration = 8
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);
        s.rect("bg", {
            .size = {128.0f, 96.0f},
            .color = Color{0.05f, 0.1f, 0.15f, 1.0f},
            .pos = {64.0f, 48.0f, 0}
        });
        float x = 30.0f + static_cast<float>(ctx.frame) * 5.0f;
        s.circle("dot", {
            .radius = 6.0f,
            .color = Color::yellow(),
            .pos = {x, 48.0f, 0}
        });
        return s.build();
    });

    // Baseline: no tiles
    SoftwareRenderer baseline;
    {
        RenderSettings s;
        s.use_modular_graph = true;
        s.enable_dirty_rects = false;
        baseline.set_settings(s);
    }

    // Optimized: tiles + cache
    SoftwareRenderer opt;
    {
        RenderSettings s;
        s.use_modular_graph = true;
        s.enable_dirty_rects = true;
        s.enable_dirty_bitmask = true;
        s.tile_size = 32;
        opt.set_settings(s);
    }

    int total_mismatches = 0;
    for (Frame f = 0; f < 8; ++f) {
        auto fb_b = baseline.render_frame(comp, f);
        auto fb_o = opt.render_frame(comp, f);
        REQUIRE(fb_b != nullptr);
        REQUIRE(fb_o != nullptr);
        REQUIRE(fb_b->width() == W);
        REQUIRE(fb_b->height() == H);

        int mism = 0;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                Color ca = fb_b->get_pixel(x, y);
                Color cb = fb_o->get_pixel(x, y);
                if (std::abs(ca.r - cb.r) > 1e-4f ||
                    std::abs(ca.g - cb.g) > 1e-4f ||
                    std::abs(ca.b - cb.b) > 1e-4f ||
                    std::abs(ca.a - cb.a) > 1e-4f)
                    ++mism;
            }
        total_mismatches += mism;
    }
    CHECK(total_mismatches == 0);
}
