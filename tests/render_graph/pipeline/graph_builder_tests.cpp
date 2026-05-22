#include <doctest/doctest.h>
#include <chronon3d/render_graph/graph_builder.hpp>
#include <chronon3d/render_graph/nodes/basic_nodes.hpp>
#include <chronon3d/render_graph/nodes/precomp_node.hpp>
#include <chronon3d/render_graph/nodes/video_node.hpp>
#include <chronon3d/render_graph/nodes/track_matte_node.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

using namespace chronon3d;
using namespace chronon3d::graph;

TEST_CASE("Test 6.1 — Scena vuota genera ClearNode come output") {
    Scene scene;
    RenderGraphContext ctx{.width = 100, .height = 100};

    RenderGraph graph = GraphBuilder::build(scene, ctx);
    REQUIRE(graph.has_output());
    
    const auto& output_node = graph.node(graph.output());
    CHECK(output_node.kind() == RenderGraphNodeKind::Output);
    CHECK(output_node.name() == "Clear");
}

TEST_CASE("Test 6.2 — Root node genera Source + Composite") {
    auto scene = SceneBuilder{}
        .rect("root-rect", {.size={100, 100}, .color=Color::red(), .pos={0, 0, 0}})
        .build();

    RenderGraphContext ctx{.width = 100, .height = 100};
    RenderGraph graph = GraphBuilder::build(scene, ctx);

    bool has_source = false;
    bool has_composite = false;
    for (size_t i = 0; i < graph.size(); ++i) {
        const auto& n = graph.node(static_cast<GraphNodeId>(i));
        if (n.kind() == RenderGraphNodeKind::Source) has_source = true;
        if (n.kind() == RenderGraphNodeKind::Composite) has_composite = true;
    }

    CHECK(has_source);
    CHECK(has_composite);
}

TEST_CASE("Test 6.3 — Layer normale genera pipeline source transform composite") {
    auto scene = SceneBuilder{}
        .layer("my-layer", [](LayerBuilder& l) {
            l.rect("layer-rect", {.size = {50, 50}});
        })
        .build();

    RenderGraphContext ctx{.width = 100, .height = 100};
    RenderGraph graph = GraphBuilder::build(scene, ctx);

    bool has_source = false;
    bool has_composite = false;
    for (size_t i = 0; i < graph.size(); ++i) {
        const auto& n = graph.node(static_cast<GraphNodeId>(i));
        if (n.kind() == RenderGraphNodeKind::Source) has_source = true;
        if (n.kind() == RenderGraphNodeKind::Composite) has_composite = true;
    }

    CHECK(has_source);
    CHECK(has_composite);
}

TEST_CASE("Test 6.4 — Layer con mask genera MaskNode") {
    auto scene = SceneBuilder{}
        .layer("masked-layer", [](LayerBuilder& l) {
            l.mask_rounded_rect({.size = {80, 80}, .radius = 5.0f, .pos = {10, 10, 0}});
            l.rect("masked-rect", {.size = {50, 50}});
        })
        .build();

    RenderGraphContext ctx{.width = 100, .height = 100};
    RenderGraph graph = GraphBuilder::build(scene, ctx);

    bool has_mask = false;
    for (size_t i = 0; i < graph.size(); ++i) {
        const auto& n = graph.node(static_cast<GraphNodeId>(i));
        if (n.kind() == RenderGraphNodeKind::Mask) has_mask = true;
    }
    CHECK(has_mask);
}

TEST_CASE("Test 6.5 — Layer con blur genera Effect node") {
    auto scene = SceneBuilder{}
        .layer("effect-layer", [](LayerBuilder& l) {
            l.blur(10.0f);
            l.rect("rect", {.size = {50, 50}});
        })
        .build();

    RenderGraphContext ctx{.width = 100, .height = 100};
    RenderGraph graph = GraphBuilder::build(scene, ctx);

    bool has_effect = false;
    for (size_t i = 0; i < graph.size(); ++i) {
        const auto& n = graph.node(static_cast<GraphNodeId>(i));
        if (n.kind() == RenderGraphNodeKind::Effect) has_effect = true;
    }
    CHECK(has_effect);
}

TEST_CASE("Test 6.6 — Adjustment layer non crea source proprio") {
    auto scene = SceneBuilder{}
        .adjustment_layer("adj-layer", [](LayerBuilder& l) {
            l.blur(5.0f);
        })
        .build();

    RenderGraphContext ctx{.width = 100, .height = 100};
    RenderGraph graph = GraphBuilder::build(scene, ctx);

    bool has_adjustment = false;
    bool has_source = false;
    for (size_t i = 0; i < graph.size(); ++i) {
        const auto& n = graph.node(static_cast<GraphNodeId>(i));
        if (n.kind() == RenderGraphNodeKind::Adjustment) has_adjustment = true;
        if (n.kind() == RenderGraphNodeKind::Source && n.name() == "Source:adj-layer") has_source = true;
    }
    CHECK(has_adjustment);
    CHECK_FALSE(has_source);
}

TEST_CASE("Test 6.7 — Null layer non renderizza") {
    auto scene = SceneBuilder{}
        .layer("null-layer", [](LayerBuilder& l) {
            l.kind(LayerKind::Null);
        })
        .build();

    RenderGraphContext ctx{.width = 100, .height = 100};
    RenderGraph graph = GraphBuilder::build(scene, ctx);

    bool has_source = false;
    for (size_t i = 0; i < graph.size(); ++i) {
        const auto& n = graph.node(static_cast<GraphNodeId>(i));
        if (n.kind() == RenderGraphNodeKind::Source) has_source = true;
    }
    CHECK_FALSE(has_source);
}

TEST_CASE("Test 6.8 — Precomp layer genera PrecompNode") {
    auto scene = SceneBuilder{}
        .precomp_layer("precomp-layer", "nested_comp", [](LayerBuilder&) {})
        .build();

    RenderGraphContext ctx{.width = 100, .height = 100};
    RenderGraph graph = GraphBuilder::build(scene, ctx);

    bool has_precomp = false;
    for (size_t i = 0; i < graph.size(); ++i) {
        const auto& n = graph.node(static_cast<GraphNodeId>(i));
        if (n.kind() == RenderGraphNodeKind::Precomp) has_precomp = true;
    }
    CHECK(has_precomp);
}

TEST_CASE("Test 6.9 — Video layer genera VideoNode") {
    auto scene = SceneBuilder{}
        .video_layer("video-layer", video::VideoSource{.path = "video.mp4"}, [](LayerBuilder&) {})
        .build();

    RenderGraphContext ctx{.width = 100, .height = 100};
    RenderGraph graph = GraphBuilder::build(scene, ctx);

    bool has_video = false;
    for (size_t i = 0; i < graph.size(); ++i) {
        const auto& n = graph.node(static_cast<GraphNodeId>(i));
        if (n.kind() == RenderGraphNodeKind::Video) has_video = true;
    }
    CHECK(has_video);
}

TEST_CASE("Test 6.10 — Track matte genera TrackMatteNode") {
    auto scene = SceneBuilder{}
        .layer("matte-source", [](LayerBuilder& l) {
            l.rect("matte-rect", {.size = {50, 50}});
        })
        .layer("target", [](LayerBuilder& l) {
            l.track_matte_alpha("matte-source");
            l.rect("target-rect", {.size = {50, 50}});
        })
        .build();

    RenderGraphContext ctx{.width = 100, .height = 100};
    RenderGraph graph = GraphBuilder::build(scene, ctx);

    bool has_track_matte = false;
    for (size_t i = 0; i < graph.size(); ++i) {
        const auto& n = graph.node(static_cast<GraphNodeId>(i));
        if (n.kind() == RenderGraphNodeKind::TrackMatte) has_track_matte = true;
    }
    CHECK(has_track_matte);
}
