#include <doctest/doctest.h>
#include <chronon3d/scene/scene.hpp>
#include <chronon3d/scene/shape.hpp>
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/graph_builder.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>

using namespace chronon3d;
using namespace chronon3d::graph;

// ── Graph Snapshot Contract ──────────────────────────────────────────────────
//
// The same scene MUST always produce the same render graph structure
// (same nodes, same connections, same output).  This ensures deterministic
// rendering and cache stability.

namespace {

/// Build a minimal scene with a background rect.
Scene make_rect_scene() {
    Scene scene;

    Layer bg;
    bg.name = "bg";
    bg.duration = 100;
    bg.visible = true;

    RenderNode rect_node;
    rect_node.name = "bg_rect";
    rect_node.shape = Shape{
        .type = ShapeType::Rect,
        .rect = {.size = {100.0f, 80.0f}},
    };
    rect_node.color = Color{0.1f, 0.2f, 0.3f, 1.0f};

    bg.nodes.push_back(std::move(rect_node));
    scene.add_layer(std::move(bg));

    return scene;
}

RenderGraphContext make_ctx(int w = 640, int h = 360) {
    RenderGraphContext ctx;
    ctx.width = w;
    ctx.height = h;
    ctx.frame = 0;
    ctx.time_seconds = 0.0f;
    return ctx;
}

} // namespace

TEST_CASE("GraphSnapshot: same scene produces same graph size") {
    Scene scene = make_rect_scene();
    auto ctx = make_ctx();

    auto graph1 = GraphBuilder::build(scene, ctx);
    auto graph2 = GraphBuilder::build(scene, ctx);

    CHECK(graph1.size() > 0);
    CHECK(graph1.size() == graph2.size());
    CHECK(graph1.has_output());
    CHECK(graph2.has_output());
}

TEST_CASE("GraphSnapshot: same scene produces same live node count") {
    Scene scene = make_rect_scene();
    auto ctx = make_ctx();

    auto graph1 = GraphBuilder::build(scene, ctx);
    auto graph2 = GraphBuilder::build(scene, ctx);

    CHECK(graph1.live_count() == graph2.live_count());
}

TEST_CASE("GraphSnapshot: different scenes produce different graphs") {
    Scene scene1 = make_rect_scene();

    Scene scene2;
    Layer fg;
    fg.name = "circle";
    fg.duration = 100;
    fg.visible = true;
    RenderNode circle_node;
    circle_node.name = "circle_shape";
    circle_node.shape = Shape{
        .type = ShapeType::Circle,
        .circle = {.radius = 30.0f},
    };
    circle_node.color = Color::red();
    fg.nodes.push_back(std::move(circle_node));
    scene2.add_layer(std::move(fg));

    auto ctx = make_ctx();

    auto graph1 = GraphBuilder::build(scene1, ctx);
    auto graph2 = GraphBuilder::build(scene2, ctx);

    // Different scenes should produce different graph structures
    bool same_size = (graph1.size() == graph2.size());
    bool same_live = (graph1.live_count() == graph2.live_count());
    bool same_output = (graph1.output() == graph2.output());
    // If size, live count and output are all the same, graphs might still differ internally
    bool all_same = same_size && same_live && same_output;
    CHECK_FALSE(all_same);
}

TEST_CASE("GraphSnapshot: same scene at different frames produces same structure") {
    Scene scene = make_rect_scene();
    auto ctx1 = make_ctx();
    auto ctx2 = make_ctx();
    ctx2.frame = 5; // different frame

    auto graph1 = GraphBuilder::build(scene, ctx1);
    auto graph2 = GraphBuilder::build(scene, ctx2);

    CHECK(graph1.size() == graph2.size());
    CHECK(graph1.live_count() == graph2.live_count());
}

TEST_CASE("GraphSnapshot: empty scene produces valid graph") {
    Scene empty_scene;
    auto ctx = make_ctx(100, 100);
    auto graph = GraphBuilder::build(empty_scene, ctx);

    // Even an empty scene should produce a graph
    // (at minimum a ClearNode to fill the framebuffer)
    CHECK(graph.has_output());
    CHECK(graph.live_count() >= 1);
}

TEST_CASE("GraphSnapshot: layer order affects graph structure") {
    auto ctx = make_ctx();

    // Scene with bg first, then fg
    Scene scene_a;
    {
        Layer bg; bg.name = "bg"; bg.duration = 100;
        RenderNode n; n.name = "bg_rect";
        n.shape = Shape{.type = ShapeType::Rect, .rect = {.size = {100, 80}}};
        n.color = Color::blue();
        bg.nodes.push_back(std::move(n));
        scene_a.add_layer(std::move(bg));
    }
    {
        Layer fg; fg.name = "fg"; fg.duration = 100;
        RenderNode n; n.name = "fg_circle";
        n.shape = Shape{.type = ShapeType::Circle, .circle = {.radius = 30}};
        n.color = Color::red();
        fg.nodes.push_back(std::move(n));
        scene_a.add_layer(std::move(fg));
    }

    // Scene with fg first, then bg
    Scene scene_b;
    {
        Layer fg; fg.name = "fg"; fg.duration = 100;
        RenderNode n; n.name = "fg_circle";
        n.shape = Shape{.type = ShapeType::Circle, .circle = {.radius = 30}};
        n.color = Color::red();
        fg.nodes.push_back(std::move(n));
        scene_b.add_layer(std::move(fg));
    }
    {
        Layer bg; bg.name = "bg"; bg.duration = 100;
        RenderNode n; n.name = "bg_rect";
        n.shape = Shape{.type = ShapeType::Rect, .rect = {.size = {100, 80}}};
        n.color = Color::blue();
        bg.nodes.push_back(std::move(n));
        scene_b.add_layer(std::move(bg));
    }

    auto graph_a = GraphBuilder::build(scene_a, ctx);
    auto graph_b = GraphBuilder::build(scene_b, ctx);

    // Different layer order should produce different graph structure
    bool same_structure = (graph_a.live_count() == graph_b.live_count())
                       && (graph_a.output() == graph_b.output());
    CHECK_FALSE(same_structure);
}
