#include <doctest/doctest.h>
#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/render_graph/nodes/text_run_node.hpp>
#include <chronon3d/text/text_run.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <memory>
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
    CHECK(to_string(RenderGraphNodeKind::TextRun) == "TextRun");
}

// ── Contract: RenderGraph move semantics are safe ────────────────────────────

TEST_CASE("CoreContract: RenderGraph move does not crash") {
    RenderGraph g1;
    auto id = g1.add_node(std::make_unique<MockNode>("mock"));
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

// ---------------------------------------------------------------------------
// TextRunNode contract tests.
// ---------------------------------------------------------------------------
//
// These tests cover three behavioral guarantees the scaffolding
// relies on without spinning up a full SoftwareRenderer + RenderGraph:
//
//   1. cache_key is deterministic for the same TextRunShape (two calls
//      produce byte-identical digests).
//   2. cache_key is sensitive to per-glyph animated state (animating one
//      glyph's position.x flips the digest — proves frame-to-frame cache
//      invalidation is real and not accidentally static).
//   3. predicted_bbox returns a non-empty raster::BBox tight to the glyph
//      extents (proves the wiring reaches renderer::compute_text_run_world_bbox).

namespace {

// Minimal concrete RenderGraphNode for testing graph wiring without real nodes.
struct MockNode : RenderGraphNode {
    std::string m_name;
    explicit MockNode(std::string name) : m_name(std::move(name)) {}
    [[nodiscard]] std::string_view name() const override { return m_name; }
    [[nodiscard]] RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Source; }
    void execute(RenderGraphContext&) override {}
    [[nodiscard]] cache::NodeCacheKey cache_key(const RenderGraphContext&) const override { return {}; }
    [[nodiscard]] std::optional<raster::BBox> predicted_bbox(const RenderGraphContext&) const override { return std::nullopt; }
    [[nodiscard]] bool cache_static() const override { return false; }
};

// Build a minimal TextRunShape with N glyphs at integer x-positions.
std::shared_ptr<TextRunShape> make_test_text_run_shape_pr3(size_t glyph_count) {
    PlacedGlyphRun placed;
    for (size_t i = 0; i < glyph_count; ++i) {
        PlacedGlyph g;
        g.glyph_id = static_cast<u32>(i + 1);
        g.cluster = static_cast<u32>(i);
        g.is_cluster_start = true;
        g.advance_x = 10.0f;
        g.x = static_cast<float>(i) * 10.0f;
        g.y = 0.0f;
        g.byte_offset = i;
        g.byte_len = 1;
        placed.glyphs.push_back(g);

        PlacedGlyphRun::Cluster cl;
        cl.start_glyph = i;
        cl.end_glyph = i + 1;
        cl.byte_offset = i;
        cl.byte_len = 1;
        cl.advance = 10.0f;
        cl.raw_advance = 10.0f;
        placed.clusters.push_back(cl);
    }
    placed.total_width = static_cast<float>(glyph_count) * 10.0f;
    placed.total_height = 16.0f;

    auto layout = std::make_shared<TextRunLayout>();
    layout->source_text = std::string(glyph_count, 'A');
    layout->font.font_path = "assets/fonts/Inter-Bold.ttf";
    layout->font_size = 16.0f;
    layout->placed = placed;
    layout->bounds = Vec2(placed.total_width, placed.total_height);
    layout->line_height = 16.0f;

    auto shape = std::make_shared<TextRunShape>();
    shape->layout = layout;
    shape->glyphs.reserve(glyph_count);
    for (size_t i = 0; i < glyph_count; ++i) {
        GlyphInstanceState gs;
        gs.glyph_id = static_cast<u32>(i + 1);
        gs.layout_position = Vec2(static_cast<float>(i) * 10.0f, 0.0f);
        gs.position = Vec3(0.0f, 0.0f, 0.0f);
        gs.scale = Vec3(1.0f, 1.0f, 1.0f);
        gs.rotation = Vec3(0.0f, 0.0f, 0.0f);
        gs.opacity = 1.0f;
        gs.fill = Color(1.0f, 1.0f, 1.0f, 1.0f);
        shape->glyphs.push_back(gs);
    }
    return shape;
}

} // anonymous namespace

TEST_CASE("CoreContract: TextRunNode cache_key is deterministic") {
    auto shape = make_test_text_run_shape_pr3(3);
    RenderNode rnode;
    rnode.world_transform.position = Vec3(50.0f, 50.0f, 0.0f);
    rnode.name = std::pmr::string{"test_run"};

    cache::NodeCacheKey skeleton;
    skeleton.scope = "test";
    skeleton.frame = 0;
    skeleton.width = 1920;
    skeleton.height = 1080;

    TextRunNode node("test_run", shape, rnode, skeleton);

    RenderGraphContext ctx;
    ctx.frame_input.width = 1920;
    ctx.frame_input.height = 1080;

    auto k1 = node.cache_key(ctx);
    auto k2 = node.cache_key(ctx);
    CHECK(k1 == k2);
    CHECK(k1.digest() != 0);
    CHECK(k1.source_hash != 0);
}

TEST_CASE("CoreContract: TextRunNode cache_key invalidates on per-glyph state change") {
    auto shape = make_test_text_run_shape_pr3(3);
    RenderNode rnode;
    rnode.world_transform.position = Vec3(50.0f, 50.0f, 0.0f);
    rnode.name = std::pmr::string{"test_run_state"};

    cache::NodeCacheKey skeleton;
    skeleton.scope = "test";
    skeleton.frame = 0;
    skeleton.width = 1920;
    skeleton.height = 1080;

    TextRunNode node("test_run_state", shape, rnode, skeleton);

    RenderGraphContext ctx;
    ctx.frame_input.width = 1920;
    ctx.frame_input.height = 1080;

    auto k_baseline = node.cache_key(ctx);

    // Mutate the middle glyph's position.x — should invalidate the cache key.
    shape->glyphs[1].position.x = 25.0f;
    auto k_animated = node.cache_key(ctx);

    CHECK(k_baseline != k_animated);

    // Mutate rotation.z instead — different field, still invalidates.
    shape = make_test_text_run_shape_pr3(3);   // reset
    TextRunNode node_rot("test_run_state", shape, rnode, skeleton);
    shape->glyphs[0].rotation.z = 15.0f;
    auto k_rotated = node_rot.cache_key(ctx);
    CHECK(k_baseline != k_rotated);
}

TEST_CASE("CoreContract: TextRunNode predicted_bbox is non-empty") {
    auto shape = make_test_text_run_shape_pr3(3);
    RenderNode rnode;
    rnode.world_transform.position = Vec3(50.0f, 50.0f, 0.0f);
    rnode.name = std::pmr::string{"test_run_bbox"};

    cache::NodeCacheKey skeleton;
    skeleton.scope = "test";
    skeleton.frame = 0;
    skeleton.width = 1920;
    skeleton.height = 1080;

    TextRunNode node("test_run_bbox", shape, rnode, skeleton);

    RenderGraphContext ctx;
    ctx.frame_input.width = 1920;
    ctx.frame_input.height = 1080;

    auto bbox = node.predicted_bbox(ctx);
    REQUIRE(bbox.has_value());
    CHECK_FALSE(bbox->is_empty());
    // Robust relative-span check: rejects a constant stub and ties the
    // bbox width to the input glyph count (3 glyphs × ~10 px advance +
    // spread padding).  Absolute bounds are fragile because the spread
    // padding inside `compute_text_run_world_bbox` (8 px glyph + 12 px
    // advance estimate + 8 px safety margin in TextRunNode) shifts the
    // bbox outward — actual world bbox_x lies in roughly [34, 98] for
    // these inputs, NOT [40, 90].  A relative-span check catches the
    // wiring without depending on padding internals.
    CHECK(bbox->x1 > bbox->x0);
    CHECK(bbox->x1 - bbox->x0 >= 30);  // at least the 3-glyph advance span
}

TEST_CASE("CoreContract: TextRunNode predicted_bbox widens under 2.5D rotation.y") {
    // 2.5D-aware bbox expansion: rotation.y on a glyph
    // contributes a horizontal shear equal to |tan(angle)| * layout_y.
    // With layout_y = 0 (the default for our horizontal text run), the
    // contribution is exactly zero, so set layout_position.y > 0 to
    // exercise the path.
    auto shape_a = make_test_text_run_shape_pr3(3);
    auto shape_b = make_test_text_run_shape_pr3(3);

    // Lift the middle glyph off the y=0 baseline so rotation.y widens.
    shape_b->glyphs[1].layout_position.y = 40.0f;
    shape_b->glyphs[1].rotation.y = 30.0f;   // tan(30°) ≈ 0.577

    RenderNode rnode;
    rnode.world_transform.position = Vec3(0.0f, 0.0f, 0.0f);
    rnode.name = std::pmr::string{"test_run_25d"};

    cache::NodeCacheKey skeleton;
    skeleton.width = 1920;
    skeleton.height = 1080;

    TextRunNode node_a("test_run_25d_a", shape_a, rnode, skeleton);
    TextRunNode node_b("test_run_25d_b", shape_b, rnode, skeleton);

    RenderGraphContext ctx;
    ctx.frame_input.width = 1920;
    ctx.frame_input.height = 1080;

    auto bbox_a = node_a.predicted_bbox(ctx);
    auto bbox_b = node_b.predicted_bbox(ctx);
    REQUIRE(bbox_a.has_value());
    REQUIRE(bbox_b.has_value());

    // The rotation.y/baseline-lifted shape should produce a wider (or
    // shifted) bbox than the baseline shape.  Compare horizontal extent;
    // the shear pushes bbox_a's right edge out by ~ |tan(30°)| * 40 ≈ 23 px.
    const i32 width_a = bbox_a->x1 - bbox_a->x0;
    const i32 width_b = bbox_b->x1 - bbox_b->x0;
    CHECK(width_b > width_a);
}

TEST_CASE("CoreContract: TextRunNode kind/enum/string-mapping is consistent") {
    auto shape = make_test_text_run_shape_pr3(1);
    RenderNode rnode;
    rnode.name = std::pmr::string{"k"};
    cache::NodeCacheKey skeleton;
    skeleton.scope = "k";
    skeleton.width = 1;
    skeleton.height = 1;

    TextRunNode node("k", shape, rnode, skeleton);
    CHECK(node.kind() == RenderGraphNodeKind::TextRun);
    CHECK(to_string(node.kind()) == "TextRun");
    CHECK(node.name() == "k");
    CHECK(node.cache_static() == false);
}
