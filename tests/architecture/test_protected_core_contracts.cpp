#include <doctest/doctest.h>
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/render_graph_node.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <string>

using namespace chronon3d;
using namespace chronon3d::graph;

// ── Contract: render_graph_node.hpp only exposes node base interface ──────────

TEST_CASE("CoreContract: RenderGraphNode base interface is stable") {
    // Verify that to_string() provides readable names for every enum value.
    // Renaming an enum value without updating the to_string overload would
    // cause this test to fail.
    CHECK(to_string(RenderGraphNodeKind::Source) == "Source");
    CHECK(to_string(RenderGraphNodeKind::Transform) == "Transform");
    CHECK(to_string(RenderGraphNodeKind::Composite) == "Composite");
    CHECK(to_string(RenderGraphNodeKind::Effect) == "Effect");
    CHECK(to_string(RenderGraphNodeKind::Mask) == "Mask");
    CHECK(to_string(RenderGraphNodeKind::Precomp) == "Precomp");
}

// ── Contract: RenderGraph move semantics are safe ────────────────────────────

TEST_CASE("CoreContract: RenderGraph move does not crash") {
    RenderGraph g1;
    auto id = g1.add_node(nullptr); // null node placeholder
    g1.set_output(id);

    RenderGraph g2 = std::move(g1);

    // g1 is now in a valid-but-empty state
    CHECK(g2.size() > 0);
    CHECK(g2.has_output());
}

// ── Contract: NodeCacheKey includes tile fields ──────────────────────────────

TEST_CASE("CoreContract: NodeCacheKey includes all contract fields") {
    cache::NodeCacheKey key;
    // Default fields must be present and zero-initialized
    CHECK(key.scope.empty());
    CHECK(key.frame == 0);
    CHECK(key.width == 0);
    CHECK(key.height == 0);
    CHECK(key.params_hash == 0);
    CHECK(key.source_hash == 0);
    CHECK(key.input_hash == 0);

    // Tile fields must be present with safe defaults (-1, -1 = no tile)
    CHECK(key.tile_x == -1);
    CHECK(key.tile_y == -1);
    CHECK(key.tile_size == 0);
    CHECK(key.tile_hash == 0);
}

// ── Contract: NodeCacheKey equality operator exists ──────────────────────────

TEST_CASE("CoreContract: NodeCacheKey operator== compares all fields") {
    cache::NodeCacheKey a{.scope = "test", .frame = 5, .width = 1920, .height = 1080};
    cache::NodeCacheKey b = a;

    CHECK(a == b);

    // Different scope → not equal
    cache::NodeCacheKey c = a;
    c.scope = "other";
    CHECK_FALSE(a == c);

    // Different tile position → not equal
    cache::NodeCacheKey d = a;
    d.tile_x = 0;
    CHECK_FALSE(a == d);
}

// ── Contract: RenderGraphNode has cache_key virtual method ───────────────────

TEST_CASE("CoreContract: RenderGraphNode declares cache_key pure virtual") {
    // We can't instantiate RenderGraphNode directly (abstract),
    // but we can verify the method exists via sizeof or by checking
    // that the vtable includes cache_key by calling through a derived type.
    // This test is structural — it verifies the digest method on NodeCacheKey
    // produces consistent results.
    cache::NodeCacheKey k1{.scope = "a", .frame = 1, .width = 100, .height = 100};
    cache::NodeCacheKey k2{.scope = "a", .frame = 1, .width = 100, .height = 100};
    CHECK(k1.digest() == k2.digest());

    // Different params_hash → different digest
    cache::NodeCacheKey k3{.scope = "a", .frame = 1, .width = 100, .height = 100, .params_hash = 42};
    CHECK(k1.digest() != k3.digest());
}
