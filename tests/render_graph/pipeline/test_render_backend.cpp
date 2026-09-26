#include <doctest/doctest.h>
#include <cstring>
#include <type_traits>
#include <string>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/compositor/composite_operator.hpp>
#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <chronon3d/render_graph/builder/graph_builder.hpp>
#include <chronon3d/compiler/compiler_pass_pipeline.hpp>
#include "../compiler/graph_compile_test_compat.hpp"
#include <chronon3d/render_graph/compiler/frame_graph_compile_options.hpp>
#include <chronon3d/runtime/job/job_session.hpp>
#include <chronon3d/core/scope/execution_scope.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/effects/effect_execution_context.hpp>
using namespace chronon3d;

using namespace chronon3d::graph;

class FakeBackend : public graph::RenderBackend {
public:
    int draw_node_called{0};
    int apply_effect_stack_called{0};
    int composite_layer_called{0};
    int apply_blur_called{0};
    std::string temporal_history_key;
    int temporal_width{0};
    int temporal_height{0};

    RenderOpResult draw_node(Framebuffer&, const RenderNode&, const RenderState&,
                             const Camera&, int, int) override {
        draw_node_called++;
        return RenderOpResult{RenderOpOutcome{1}};
    }

    void apply_effect_stack(Framebuffer& fb, const EffectStack&,
                            const effects::EffectExecutionContext& context) override {
        apply_effect_stack_called++;
        temporal_history_key = std::string(context.temporal_history_key);
        temporal_width = fb.width();
        temporal_height = fb.height();
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

class FailingBackend final : public FakeBackend {
public:
    RenderOpResult draw_node(Framebuffer&, const RenderNode&, const RenderState&,
                             const Camera&, int, int) override {
        return RenderOpResult{make_render_backend_error(
            RenderBackendErrorCode::ExecutionFailure,
            "forced draw failure")};
    }
};





TEST_CASE("RenderBackend - RenderGraphContext accepts and executes RenderBackend") {
    FakeBackend backend;
    cache::NodeCache cache;
    RenderGraphContext ctx{
        .backend = &backend, .node_cache = &cache
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
        .frame_input = {.width = 64, .height = 64},
        .backend = &backend, .node_cache = &cache
    };
    
    compiler::CompilerPassPipeline graph_builder;
    graph_builder.add_default_passes();
    RenderGraph graph = graph_builder.build(scene, ctx);
    // PR-2 rewire — compile through FrameGraphCompiler (sole topical-plan producer).
    auto compiled = FrameGraphCompiler{}.compile(std::move(graph), ctx,
        FrameGraphCompileOptions{ .run_optimizer = false });

    GraphExecutor executor;
    runtime::JobSession session;
    ExecutionScheduler scheduler{SchedulerMode::Sequential, 1, false};
    auto root_scope = ExecutionScope::make_root(session, session.root_arena(), compiled.graph_instance_id);
    auto out = executor.execute(compiled, ctx, root_scope, scheduler);

    REQUIRE(out.has_framebuffer());
    CHECK(backend.draw_node_called == 1);
}

TEST_CASE("RenderGraph wires displacement map source as a second effect input") {
    SceneBuilder builder;
    builder.layer("subject", [](LayerBuilder& layer) {
        layer.rect("subject-shape", {.size={24.0f,24.0f},.color=Color::white()});
        layer.effect(DisplacementMapParams{.map_layer="map"});
    });
    builder.layer("map", [](LayerBuilder& layer) {
        layer.rect("map-shape", {.size={24.0f,24.0f},.color=Color{0.5f,0.5f,0.5f,1.0f}});
    });
    auto scene=builder.build();
    RenderGraphContext context{.frame_input={.width=64,.height=64}};
    compiler::CompilerPassPipeline passes;
    passes.add_default_passes();
    const auto graph=passes.build(scene,context);
    bool found_two_input_effect=false;
    for(GraphNodeId id=0;id<graph.size();++id) {
        if(dynamic_cast<const EffectStackNode*>(&graph.node(id)) && graph.inputs(id).size()>1)
            found_two_input_effect=true;
    }
    CHECK(found_two_input_effect);

    SceneBuilder cyclic_builder;
    cyclic_builder.layer("a", [](LayerBuilder& layer) {
        layer.rect("a-shape", {.size={8.0f,8.0f},.color=Color::red()});
        layer.effect(DisplacementMapParams{.map_layer="b"});
    });
    cyclic_builder.layer("b", [](LayerBuilder& layer) {
        layer.rect("b-shape", {.size={8.0f,8.0f},.color=Color::blue()});
        layer.effect(DisplacementMapParams{.map_layer="a"});
    });
    auto cyclic_scene=cyclic_builder.build();
    CHECK_THROWS((void)passes.build(cyclic_scene,context));
}

TEST_CASE("RenderBackend - failing draw_node reaches GraphExecutor as frame failure") {
    FailingBackend backend;
    cache::NodeCache cache;
    SceneBuilder builder;
    builder.rect("failing_rect", {.size = {50.0f, 50.0f}, .color = Color::red()});
    Scene scene = builder.build();
    RenderGraphContext ctx{
        .frame_input = {.width = 64, .height = 64},
        .backend = &backend, .node_cache = &cache
    };
    compiler::CompilerPassPipeline graph_builder;
    graph_builder.add_default_passes();
    auto graph = graph_builder.build(scene, ctx);
    auto compiled = FrameGraphCompiler{}.compile(std::move(graph), ctx,
        FrameGraphCompileOptions{ .run_optimizer = false });
    GraphExecutor executor;
    runtime::JobSession session;
    ExecutionScheduler scheduler{SchedulerMode::Sequential, 1, false};
    auto scope = ExecutionScope::make_root(session, session.root_arena(), compiled.graph_instance_id);
    auto output = executor.execute(compiled, ctx, scope, scheduler);
    CHECK_FALSE(output.ok());
    REQUIRE(session.last_frame_error());
    CHECK(session.last_frame_error()->backend_code == RenderBackendErrorCode::ExecutionFailure);
    CHECK(session.last_frame_error()->message == "forced draw failure");
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
        .frame_input = {.width = 64, .height = 64},
        .backend = &backend, .node_cache = &cache
    };
    
    compiler::CompilerPassPipeline graph_builder;
    graph_builder.add_default_passes();
    RenderGraph graph = graph_builder.build(scene, ctx);
    auto compiled = FrameGraphCompiler{}.compile(std::move(graph), ctx,
        FrameGraphCompileOptions{ .run_optimizer = false });

    GraphExecutor executor;
    runtime::JobSession session;
    ExecutionScheduler scheduler2{SchedulerMode::Sequential, 1, false};
    auto root_scope2 = ExecutionScope::make_root(session, session.root_arena(), compiled.graph_instance_id);
    auto out = executor.execute(compiled, ctx, root_scope2, scheduler2);

    REQUIRE(out.has_framebuffer());
    CHECK(backend.apply_effect_stack_called >= 1);
}

TEST_CASE("RenderBackend - Echo reaches software dispatch with stable key and full canvas") {
    EffectStack echo_params;
    echo_params.emplace_back(EchoParams{.echoes = 2, .decay = 0.5f, .time_offset_frames = 1});
    EffectStackNode echo_a(echo_params, Frame{-1}, frame_variant_cache("echo"));
    EffectStackNode echo_b(echo_params, Frame{-1}, frame_variant_cache("echo"));
    echo_a.set_temporal_history_key("trail-a/echo/0");
    echo_b.set_temporal_history_key("trail-b/echo/0");
    RenderGraphContext cache_context{.frame_input = {.width = 64, .height = 48}};
    CHECK(echo_a.cache_key(cache_context).params_hash != echo_b.cache_key(cache_context).params_hash);

    FakeBackend backend;
    cache::NodeCache cache;

    SceneBuilder builder;
    builder.layer("trail", [](LayerBuilder& layer) {
        layer.rect("rect", {.size = {20.0f, 20.0f}, .color = Color::white()});
        layer.effect(EchoParams{.echoes = 2, .decay = 0.5f, .time_offset_frames = 1});
    });
    Scene scene = builder.build();

    RenderGraphContext ctx{
        .frame_input = {.width = 64, .height = 48},
        .backend = &backend, .node_cache = &cache
    };
    compiler::CompilerPassPipeline graph_builder;
    graph_builder.add_default_passes();
    RenderGraph graph = graph_builder.build(scene, ctx);
    auto compiled = FrameGraphCompiler{}.compile(std::move(graph), ctx,
        FrameGraphCompileOptions{ .run_optimizer = false });

    GraphExecutor executor;
    runtime::JobSession session;
    ExecutionScheduler scheduler{SchedulerMode::Sequential, 1, false};
    auto scope = ExecutionScope::make_root(session, session.root_arena(), compiled.graph_instance_id);
    auto out = executor.execute(compiled, ctx, scope, scheduler);

    REQUIRE(out.has_framebuffer());
    REQUIRE(backend.apply_effect_stack_called == 1);
    CHECK(backend.temporal_history_key == "trail/echo/0");
    CHECK(backend.temporal_width == 64);
    CHECK(backend.temporal_height == 48);
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
        .frame_input = {.width = 64, .height = 64},
        .backend = &backend, .node_cache = &cache
    };
    
    compiler::CompilerPassPipeline graph_builder;
    graph_builder.add_default_passes();
    RenderGraph graph = graph_builder.build(scene, ctx);
    auto compiled = FrameGraphCompiler{}.compile(std::move(graph), ctx,
        FrameGraphCompileOptions{ .run_optimizer = false });

    GraphExecutor executor;
    runtime::JobSession session;
    ExecutionScheduler scheduler3{SchedulerMode::Sequential, 1, false};
    auto root_scope3 = ExecutionScope::make_root(session, session.root_arena(), compiled.graph_instance_id);
    auto out = executor.execute(compiled, ctx, root_scope3, scheduler3);

    REQUIRE(out.has_framebuffer());
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
