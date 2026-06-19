#include <doctest/doctest.h>
#include <cstring>
#include <type_traits>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/compositor/composite_operator.hpp>
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/render_graph/builder/graph_builder.hpp>
#include <chronon3d/core/memory/render_session.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/effects/effect_execution_context.hpp>
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

    void apply_effect_stack(Framebuffer&, const EffectStack&,
                            const effects::EffectExecutionContext&) override {
        apply_effect_stack_called++;
    }

    void composite_layer(Framebuffer&, const Framebuffer&, BlendMode,
                         const std::optional<raster::BBox>& = std::nullopt,
                         CompositeOperator = CompositeOperator::SourceOver) override {
        composite_layer_called++;
    }

    void apply_blur(Framebuffer&, float,
                    const std::optional<raster::BBox>& = std::nullopt) override {
        apply_blur_called++;
    }

    void apply_per_pixel_dof(Framebuffer&, std::span<const float>,
                              const DepthOfFieldSettings&, const LensModel&,
                              const std::optional<raster::BBox>&) override {
        // FakeBackend: no-op, just count calls
    }
};

TEST_CASE("RenderBackend - RenderGraphContext accepts and executes RenderBackend") {
    FakeBackend backend;
    cache::NodeCache cache;
    RenderGraphContext ctx{
        .resources = {.backend = &backend, .node_cache = &cache}
    };
    CHECK(ctx.resources.backend == &backend);
}

TEST_CASE("RenderBackend - SourceNode execution calls draw_node on backend") {
    FakeBackend backend;
    cache::NodeCache cache;
    
    // Create simple rect layer scene using modern API
    SceneBuilder builder;
    builder.rect("test_rect", {.size = {50.0f, 50.0f}, .color = Color::red()});
    Scene scene = builder.build();

    RenderGraphContext ctx{
        .frame = {.width = 64, .height = 64},
        .resources = {.backend = &backend, .node_cache = &cache}
    };
    
    RenderGraph graph = GraphBuilder::build(scene, ctx);
    
    GraphExecutor executor;
    RenderSession session;
    auto out = executor.execute(graph, ctx, session);
    
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
        .frame = {.width = 64, .height = 64},
        .resources = {.backend = &backend, .node_cache = &cache}
    };
    
    RenderGraph graph = GraphBuilder::build(scene, ctx);
    
    GraphExecutor executor;
    RenderSession session;
    auto out = executor.execute(graph, ctx, session);
    
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
        .frame = {.width = 64, .height = 64},
        .resources = {.backend = &backend, .node_cache = &cache}
    };
    
    RenderGraph graph = GraphBuilder::build(scene, ctx);
    
    GraphExecutor executor;
    RenderSession session;
    auto out = executor.execute(graph, ctx, session);
    
    REQUIRE(out != nullptr);
    // There are 2 layer composite nodes
    CHECK(backend.composite_layer_called >= 1);
}

// ── PR2 — RenderBackend capabilities contract ──────────────────────

TEST_CASE("RenderBackend - PR2: non-copyable contract (compile-time)") {
    // RenderBackend is an abstract base class (pure virtuals), so type traits
    // evaluated on the base itself return false even when its move ops are
    // explicitly =default'd. Verify the PR2 contract on the concrete
    // FakeBackend subclass — which inherits the contract from RenderBackend
    // and is the canary for any user-implemented backend.
    static_assert(!std::is_copy_constructible_v<FakeBackend>,
                  "PR2: RenderBackend contract — concrete subclasses must be non-copyable");
    static_assert(!std::is_copy_assignable_v<FakeBackend>,
                  "PR2: RenderBackend contract — concrete subclasses must be non-copy-assignable");
    static_assert(std::is_move_constructible_v<FakeBackend>,
                  "PR2: RenderBackend contract — concrete subclasses must remain movable");
    // doctest does not define a SUCCEED macro (that is a Catch2/GTest idiom).
    // Use MESSAGE + CHECK(true) to record a passing assertion with annotation.
    MESSAGE("static_asserts above enforce the PR2 contract");
    CHECK(true);
}

TEST_CASE("RenderBackend - PR2: default capabilities are empty") {
    FakeBackend backend;
    const auto caps = backend.capabilities();
    CHECK_FALSE(caps.text_run);
}

TEST_CASE("RenderBackend - PR2: error_code_name round-trip") {
    using chronon3d::graph::RenderBackendErrorCode;
    using chronon3d::graph::render_backend_error_code_name;
    CHECK(std::strcmp(render_backend_error_code_name(RenderBackendErrorCode::UnsupportedCapability),
                      "UnsupportedCapability") == 0);
    CHECK(std::strcmp(render_backend_error_code_name(RenderBackendErrorCode::InvalidInput),
                      "InvalidInput") == 0);
    CHECK(std::strcmp(render_backend_error_code_name(RenderBackendErrorCode::ExecutionFailure),
                      "ExecutionFailure") == 0);
}
