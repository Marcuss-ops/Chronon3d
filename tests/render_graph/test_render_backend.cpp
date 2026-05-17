#include <doctest/doctest.h>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/graph_executor.hpp>
#include <chronon3d/render_graph/graph_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/cache/node_cache.hpp>

using namespace chronon3d;
using namespace chronon3d::graph;

class FakeBackend : public RenderBackend {
public:
    int draw_node_called{0};
    int apply_effect_stack_called{0};
    int composite_layer_called{0};
    int apply_blur_called{0};

    void draw_node(Framebuffer&, const RenderNode&, const RenderState&,
                   const Camera&, int, int) override {
        draw_node_called++;
    }

    void apply_effect_stack(Framebuffer&, const EffectStack&) override {
        apply_effect_stack_called++;
    }

    void composite_layer(Framebuffer&, const Framebuffer&, BlendMode) override {
        composite_layer_called++;
    }

    void apply_blur(Framebuffer&, float) override {
        apply_blur_called++;
    }
};

TEST_CASE("RenderBackend - RenderGraphContext accepts and executes RenderBackend") {
    FakeBackend backend;
    cache::NodeCache cache;
    RenderGraphContext ctx{
        .backend = &backend,
        .node_cache = &cache
    };
    CHECK(ctx.backend == &backend);
}

TEST_CASE("RenderBackend - SourceNode execution calls draw_node on backend") {
    FakeBackend backend;
    cache::NodeCache cache;
    
    // Create simple rect layer scene using modern API
    SceneBuilder builder;
    builder.rect("test_rect", {.size = {50.0f, 50.0f}, .color = Color::red()});
    Scene scene = builder.build();

    RenderGraphContext ctx{
        .width = 64,
        .height = 64,
        .backend = &backend,
        .node_cache = &cache
    };
    
    RenderGraph graph = GraphBuilder::build(scene, ctx);
    
    GraphExecutor executor;
    auto out = executor.execute(graph, ctx);
    
    REQUIRE(out != nullptr);
    CHECK(backend.draw_node_called == 1);
}

TEST_CASE("RenderBackend - EffectStackNode execution calls apply_effect_stack on backend") {
    FakeBackend backend;
    cache::NodeCache cache;
    
    // Create layer scene with a blur effect using modern lambda API
    SceneBuilder builder;
    builder.layer("effect_layer", [](LayerBuilder& lb) {
        lb.rect("rect", {.size = {50.0f, 50.0f}, .color = Color::red()});
        lb.blur(10.0f);
    });
    Scene scene = builder.build();

    RenderGraphContext ctx{
        .width = 64,
        .height = 64,
        .backend = &backend,
        .node_cache = &cache
    };
    
    RenderGraph graph = GraphBuilder::build(scene, ctx);
    
    GraphExecutor executor;
    auto out = executor.execute(graph, ctx);
    
    REQUIRE(out != nullptr);
    CHECK(backend.apply_effect_stack_called >= 1);
}

TEST_CASE("RenderBackend - CompositeNode execution calls composite_layer on backend") {
    FakeBackend backend;
    cache::NodeCache cache;
    
    // Create multiple layer scene using modern API
    SceneBuilder builder;
    builder.rect("rect_1", {.size = {50.0f, 50.0f}, .color = Color::red()});
    builder.rect("rect_2", {.size = {50.0f, 50.0f}, .color = Color::blue()});
    Scene scene = builder.build();

    RenderGraphContext ctx{
        .width = 64,
        .height = 64,
        .backend = &backend,
        .node_cache = &cache
    };
    
    RenderGraph graph = GraphBuilder::build(scene, ctx);
    
    GraphExecutor executor;
    auto out = executor.execute(graph, ctx);
    
    REQUIRE(out != nullptr);
    // There are 2 layer composite nodes
    CHECK(backend.composite_layer_called >= 1);
}
