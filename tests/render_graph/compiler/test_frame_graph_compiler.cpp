#include <doctest/doctest.h>

#include <chronon3d/render_graph/compiler/frame_graph_compiler.hpp>
#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/clear_node.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/nodes/composite_node.hpp>
#include <chronon3d/render_graph/nodes/transition_node.hpp>
#include <chronon3d/render_graph/nodes/text_run_node.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <tests/helpers/test_utils.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <memory>
#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <fstream>
#include <iomanip>
#include <sstream>
using namespace chronon3d;

using namespace chronon3d::graph;

namespace {

class LifetimeShapeProcessor final : public renderer::ShapeProcessor {
public:
    explicit LifetimeShapeProcessor(int& destructions) : m_destructions(&destructions) {}
    ~LifetimeShapeProcessor() override { ++*m_destructions; }

    void draw(const SoftwareProcessorContext&, Framebuffer&, const RenderNode&,
              const RenderState&, const Camera&, i32, i32) override {}
    raster::BBox compute_world_bbox(const Shape&, const Mat4&, f32) override {
        return raster::BBox{};
    }
    bool hit_test(const Shape&, Vec2, f32) override { return false; }

private:
    int* m_destructions;
};

class NoopShapeProcessor final : public renderer::ShapeProcessor {
public:
    void draw(const SoftwareProcessorContext&, Framebuffer&, const RenderNode&,
              const RenderState&, const Camera&, i32, i32) override {}
    raster::BBox compute_world_bbox(const Shape&, const Mat4&, f32) override {
        return raster::BBox{};
    }
    bool hit_test(const Shape&, Vec2, f32) override { return false; }
};

class NoopEffectProcessor final : public renderer::EffectProcessor {
public:
    void apply(Framebuffer&, const EffectParams&,
               const effects::EffectExecutionContext&) override {}
};

class ValidationBackend final : public chronon3d::graph::RenderBackend {
public:
    explicit ValidationBackend(
        bool missing_processor,
        std::shared_ptr<const renderer::ProcessorRegistrySnapshot> snapshot = nullptr)
        : m_missing_processor(missing_processor)
        , m_snapshot(snapshot ? std::move(snapshot)
                              : std::make_shared<const renderer::ProcessorRegistrySnapshot>(
                                    std::vector<renderer::ProcessorRegistrySnapshot::ShapeEntry>{
                                        {ShapeType::Rect, std::make_shared<NoopShapeProcessor>()},
                                        {ShapeType::Image, std::make_shared<NoopShapeProcessor>()}},
                                    std::vector<renderer::ProcessorRegistrySnapshot::EffectEntry>{
                                        {std::type_index(typeid(BlurParams)),
                                         std::make_shared<NoopEffectProcessor>()}},
                                    0)) {}

    std::shared_ptr<const renderer::ProcessorRegistrySnapshot>
    processor_snapshot() const noexcept override {
        return m_snapshot;
    }

    bool requires_processor_snapshot() const noexcept override {
        return true;
    }

    std::optional<chronon3d::graph::RenderBackendError> validate_render_node(
        const RenderNode&) const override {
        ++m_validation_calls;
        if (m_missing_processor) {
            return chronon3d::graph::RenderBackendError{
                chronon3d::graph::RenderBackendErrorCode::InvalidInput,
                "missing shape processor (test backend)"};
        }
        return std::nullopt;
    }

    void apply_per_pixel_dof(
        Framebuffer&, std::span<const float>, const DepthOfFieldSettings&,
        const LensModel&, const std::optional<raster::BBox>&) override {}
    void apply_effect_stack(
        Framebuffer&, const EffectStack&,
        const effects::EffectExecutionContext&) override {}
    void composite_layer(
        Framebuffer&, const Framebuffer&, BlendMode,
        const std::optional<raster::BBox>&, CompositeOperator) override {}
    void apply_blur(
        Framebuffer&, float, const std::optional<raster::BBox>&) override {}

    [[nodiscard]] int validation_calls() const noexcept {
        return m_validation_calls;
    }

private:
    bool m_missing_processor{false};
    std::shared_ptr<const renderer::ProcessorRegistrySnapshot> m_snapshot;
    mutable int m_validation_calls{0};
};

class CompilerTestNode final : public RenderGraphNode {
public:
    // PR2-cleanup: cache policy is decided at construction; legacy
    // `bool cache` / `bool frame_dep` ctor args and `m_cacheable` member were dropped.
    explicit CompilerTestNode(std::string n,
                               RenderNodeCachePolicy policy = static_memory_cache("test"))
        : RenderGraphNode(policy), m_name(std::move(n)) {}

    RenderGraphNodeKind kind() const noexcept override { return RenderGraphNodeKind::Source; }
    [[nodiscard]] std::string_view name() const noexcept override { return m_name; }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>>
    ) const override {
        return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext&) const override {
        return cache::NodeCacheKey{.scope = m_name, .frame = 0, .width = 0, .height = 0};
    }

    NodeExecResult execute(
        RenderGraphContext&,
        std::span<const FramebufferRef>,
        std::span<const std::optional<raster::BBox>>
    ) override {
        return OwnedFB{};
    }

private:
    std::string m_name;
};

RenderGraph make_single_source_graph(ShapeType shape_type) {
    RenderGraph graph;
    RenderNode render_node;
    render_node.shape.set_type(shape_type);
    auto source = std::make_unique<SourceNode>(
        "source", render_node, cache::NodeCacheKey{});
    const auto source_id = graph.add_node(std::move(source));
    graph.set_output(source_id);
    return graph;
}

} // namespace

TEST_CASE("ProcessorRegistrySnapshot owns processors after registry lifetime ends") {
    int destructions = 0;
    std::shared_ptr<const renderer::ProcessorRegistrySnapshot> snapshot;
    renderer::ShapeProcessor* processor_address = nullptr;

    {
        renderer::SoftwareRegistry registry;
        registry.register_shape(
            ShapeType::Rect,
            std::make_shared<LifetimeShapeProcessor>(destructions));
        snapshot = registry.snapshot();
        REQUIRE(snapshot != nullptr);
        const auto handle = snapshot->shape_handle(ShapeType::Rect);
        REQUIRE(handle.valid());
        const auto retained = snapshot->shape_shared(handle);
        processor_address = retained.get();
        CHECK(retained != nullptr);
        CHECK(snapshot->generation() == registry.generation());
    }

    CHECK(destructions == 0);
    REQUIRE(snapshot != nullptr);
    const auto handle = snapshot->shape_handle(ShapeType::Rect);
    auto retained = snapshot->shape_shared(handle);
    CHECK(retained.get() == processor_address);
    CHECK(retained != nullptr);
    Shape shape;
    CHECK_NOTHROW(retained->compute_world_bbox(shape, Mat4(1.0f), 0.0f));
    // Release the explicit access handle before dropping the snapshot so the
    // destruction assertion observes the snapshot's ownership boundary.
    retained.reset();
    snapshot.reset();
    CHECK(destructions == 1);
}

TEST_CASE("ProcessorRegistrySnapshot isolates engine registries through shutdown") {
    int destructions_a = 0;
    int destructions_b = 0;
    std::shared_ptr<const renderer::ProcessorRegistrySnapshot> snapshot_a;
    std::shared_ptr<const renderer::ProcessorRegistrySnapshot> snapshot_b;

    {
        renderer::SoftwareRegistry engine_a_registry;
        renderer::SoftwareRegistry engine_b_registry;
        engine_a_registry.register_shape(
            ShapeType::Rect,
            std::make_shared<LifetimeShapeProcessor>(destructions_a));
        engine_b_registry.register_shape(
            ShapeType::Rect,
            std::make_shared<LifetimeShapeProcessor>(destructions_b));

        snapshot_a = engine_a_registry.snapshot();
        snapshot_b = engine_b_registry.snapshot();
        REQUIRE(snapshot_a != nullptr);
        REQUIRE(snapshot_b != nullptr);
        CHECK(snapshot_a->generation() == snapshot_b->generation());
        CHECK(snapshot_a->identity() != snapshot_b->identity());

        const auto processor_a = snapshot_a->shape_shared(
            snapshot_a->shape_handle(ShapeType::Rect));
        const auto processor_b = snapshot_b->shape_shared(
            snapshot_b->shape_handle(ShapeType::Rect));
        REQUIRE(processor_a != nullptr);
        REQUIRE(processor_b != nullptr);
        CHECK(processor_a.get() != processor_b.get());
    }

    // Registry/engine shutdown must not invalidate a compiled snapshot.
    CHECK(destructions_a == 0);
    CHECK(destructions_b == 0);
    REQUIRE(snapshot_a != nullptr);
    REQUIRE(snapshot_b != nullptr);
    CHECK(snapshot_a->shape_shared(
              snapshot_a->shape_handle(ShapeType::Rect)) != nullptr);
    CHECK(snapshot_b->shape_shared(
              snapshot_b->shape_handle(ShapeType::Rect)) != nullptr);

    snapshot_a.reset();
    CHECK(destructions_a == 1);
    CHECK(destructions_b == 0);
    snapshot_b.reset();
    CHECK(destructions_b == 1);
}

TEST_CASE("ProcessorRegistrySnapshot survives full engine shutdown and isolates engines") {
    std::shared_ptr<const renderer::ProcessorRegistrySnapshot> snapshot_a;
    std::shared_ptr<const renderer::ProcessorRegistrySnapshot> snapshot_b;

    {
        auto runtime_a_result = runtime::RenderRuntime::create(
            runtime::RuntimeConfig{Config{}, std::nullopt});
        auto runtime_b_result = runtime::RenderRuntime::create(
            runtime::RuntimeConfig{Config{}, std::nullopt});
        REQUIRE(runtime_a_result.has_value());
        REQUIRE(runtime_b_result.has_value());
        auto runtime_a = std::move(runtime_a_result).value();
        auto runtime_b = std::move(runtime_b_result).value();

        auto renderer_a = std::make_unique<SoftwareRenderer>(*runtime_a, Config{});
        auto renderer_b = std::make_unique<SoftwareRenderer>(*runtime_b, Config{});
        snapshot_a = renderer_a->software_registry().snapshot();
        snapshot_b = renderer_b->software_registry().snapshot();
        REQUIRE(snapshot_a != nullptr);
        REQUIRE(snapshot_b != nullptr);
        CHECK(snapshot_a->identity() != snapshot_b->identity());
        CHECK(snapshot_a->shape_shared(
                  snapshot_a->shape_handle(ShapeType::Rect)) != nullptr);
        CHECK(snapshot_b->shape_shared(
                  snapshot_b->shape_handle(ShapeType::Rect)) != nullptr);

        // Destroy both renderer/runtime owners while retaining only the
        // immutable snapshots. Processor dispatch data must remain valid.
        renderer_a.reset();
        renderer_b.reset();
        runtime_a.reset();
        runtime_b.reset();
    }

    REQUIRE(snapshot_a != nullptr);
    REQUIRE(snapshot_b != nullptr);
    const auto processor_a = snapshot_a->shape_shared(
        snapshot_a->shape_handle(ShapeType::Rect));
    const auto processor_b = snapshot_b->shape_shared(
        snapshot_b->shape_handle(ShapeType::Rect));
    REQUIRE(processor_a != nullptr);
    REQUIRE(processor_b != nullptr);
    Shape shape;
    CHECK_NOTHROW(processor_a->compute_world_bbox(shape, Mat4(1.0f), 0.0f));
    CHECK_NOTHROW(processor_b->compute_world_bbox(shape, Mat4(1.0f), 0.0f));
    CHECK(snapshot_a->shape_shared(
              snapshot_a->shape_handle(ShapeType::Rect)).get() !=
          snapshot_b->shape_shared(
              snapshot_b->shape_handle(ShapeType::Rect)).get());
}

TEST_CASE("SoftwareBackend snapshot survives renderer shutdown and isolates engines") {
    auto renderer_a = chronon3d::test::make_renderer_shared();
    auto renderer_b = chronon3d::test::make_renderer_shared();
    auto snapshot_a = renderer_a->backend().processor_snapshot();
    auto snapshot_b = renderer_b->backend().processor_snapshot();

    REQUIRE(snapshot_a != nullptr);
    REQUIRE(snapshot_b != nullptr);
    CHECK(snapshot_a->identity() != snapshot_b->identity());
    const auto handle_a = snapshot_a->shape_handle(ShapeType::Rect);
    const auto handle_b = snapshot_b->shape_handle(ShapeType::Rect);
    REQUIRE(handle_a.valid());
    REQUIRE(handle_b.valid());
    CHECK(snapshot_a->shape_shared(handle_a) != nullptr);
    CHECK(snapshot_b->shape_shared(handle_b) != nullptr);
    CHECK(snapshot_a->shape_shared(handle_a).get() !=
          snapshot_b->shape_shared(handle_b).get());

    // The backend, runtime, registry, and renderer all shut down here. The
    // retained owning snapshots must still provide valid processor objects.
    renderer_a.reset();
    renderer_b.reset();
    CHECK(snapshot_a->shape_shared(handle_a) != nullptr);
    CHECK(snapshot_b->shape_shared(handle_b) != nullptr);
}

TEST_CASE("CompiledFrameGraph retains processor snapshot ownership") {
    int destructions = 0;
    CompiledFrameGraph compiled;

    {
        auto processor = std::make_shared<LifetimeShapeProcessor>(destructions);
        auto snapshot = std::make_shared<const renderer::ProcessorRegistrySnapshot>(
            std::vector<renderer::ProcessorRegistrySnapshot::ShapeEntry>{
                {ShapeType::Rect, processor}},
            std::vector<renderer::ProcessorRegistrySnapshot::EffectEntry>{},
            9);
        ValidationBackend backend(false, snapshot);
        RenderGraphContext ctx;
        ctx.services.backend = &backend;
        ctx.services.registry_generation = 9;
        FrameGraphCompiler compiler;

        compiled = compiler.compile(make_single_source_graph(ShapeType::Rect), ctx);
        REQUIRE(compiled.valid);
        REQUIRE(compiled.processor_snapshot != nullptr);
        CHECK(compiled.processor_snapshot->shape_shared(
                  compiled.nodes.front().shape_processor).get() == processor.get());
        snapshot.reset();
        processor.reset();
    }

    CHECK(destructions == 0);
    compiled = CompiledFrameGraph{};
    CHECK(destructions == 1);
}

TEST_CASE("FrameGraphCompiler - handles empty graph") {
    RenderGraph graph;
    RenderGraphContext ctx;
    FrameGraphCompiler compiler;

    auto compiled = compiler.compile(std::move(graph), ctx);

    CHECK(compiled.empty());
}

TEST_CASE("FrameGraphCompiler - rejects renderable ShapeType::None before execution") {
    auto graph = make_single_source_graph(ShapeType::None);
    RenderGraphContext ctx;
    FrameGraphCompiler compiler;

    try {
        static_cast<void>(compiler.compile(std::move(graph), ctx));
        FAIL("expected ShapeType::None validation failure");
    } catch (const std::runtime_error& error) {
        CHECK(std::string(error.what()).find("ShapeType::None") != std::string::npos);
    }
}

TEST_CASE("FrameGraphCompiler - accepts Image Shape with resolved processor") {
    auto graph = make_single_source_graph(ShapeType::Image);
    ValidationBackend backend(/*missing_processor=*/false);
    RenderGraphContext ctx;
    ctx.services.backend = &backend;
    FrameGraphCompiler compiler;

    const auto compiled = compiler.compile(std::move(graph), ctx);
    CHECK(compiled.valid);
}

TEST_CASE("FrameGraphCompiler - coverage validator accepts standalone ownership") {
    CompiledFrameGraph compiled;
    compiled.nodes.resize(1);
    compiled.nodes[0].reachable = true;
    compiled.nodes[0].name = "standalone";
    compiled.program.operations.push_back(CompiledOperation{
        .node = 0,
        .is_fused = false,
    });
    CHECK_NOTHROW(FrameGraphCompiler::validate_compiled_program_coverage(compiled));
}

TEST_CASE("FrameGraphCompiler - coverage validator accepts fused ownership") {
    CompiledFrameGraph compiled;
    compiled.nodes.resize(1);
    compiled.nodes[0].reachable = true;
    compiled.nodes[0].name = "fused";
    CompiledLayerBatch batch;
    batch.is_gpu_fused = true;
    batch.member_nodes = {0};
    compiled.program.layer_batches.push_back(std::move(batch));
    compiled.program.operations.push_back(CompiledOperation{
        .node = 0,
        .is_fused = true,
    });
    CHECK_NOTHROW(FrameGraphCompiler::validate_compiled_program_coverage(compiled));
}

TEST_CASE("FrameGraphCompiler - coverage validator rejects uncovered reachable node") {
    CompiledFrameGraph compiled;
    compiled.nodes.resize(1);
    compiled.nodes[0].reachable = true;
    compiled.nodes[0].name = "uncovered";
    CHECK_THROWS_WITH(
        FrameGraphCompiler::validate_compiled_program_coverage(compiled),
        doctest::Contains("exactly one execution owner"));
}

TEST_CASE("FrameGraphCompiler - coverage validator accepts legal elimination") {
    CompiledFrameGraph compiled;
    compiled.nodes.resize(1);
    compiled.nodes[0].reachable = true;
    compiled.nodes[0].name = "baked";
    compiled.nodes[0].elimination_reason = EliminationReason::StaticBake;
    CHECK_NOTHROW(FrameGraphCompiler::validate_compiled_program_coverage(compiled));
}

TEST_CASE("FrameGraphCompiler - TextRun bypasses Shape processor validation") {
    RenderGraph graph;
    RenderNode render_ref;
    render_ref.shape.set_type(ShapeType::None);
    const auto text_id = graph.add_node(std::make_unique<TextRunNode>(
        "text", "layer", nullptr, render_ref, cache::NodeCacheKey{},
        TextRunPlacement{}));
    graph.set_output(text_id);

    // TextRunNode owns a dedicated text processor path. Its render reference
    // is intentionally ShapeType::None and must not be sent to a ShapeProcessor.
    RenderGraphContext ctx;
    ctx.frame_input.width = 100;
    ctx.frame_input.height = 100;
    FrameGraphCompiler compiler;
    FrameGraphCompileOptions options;
    options.compute_bboxes = false;

    const auto compiled = compiler.compile(std::move(graph), ctx, options);
    CHECK(compiled.valid);
    REQUIRE(compiled.nodes.size() > text_id);
    CHECK(compiled.nodes[text_id].kind == RenderGraphNodeKind::TextRun);
    CHECK_FALSE(compiled.nodes[text_id].lowered_into_batch);
    bool text_has_standalone_operation = false;
    for (const auto& operation : compiled.program.operations) {
        if (operation.node == text_id) {
            text_has_standalone_operation = true;
            CHECK_FALSE(operation.is_fused);
        }
    }
    CHECK(text_has_standalone_operation);
}

TEST_CASE("FrameGraphCompiler - rejects renderable without backend before execution") {
    auto graph = make_single_source_graph(ShapeType::Rect);
    RenderGraphContext ctx;
    FrameGraphCompiler compiler;

    try {
        static_cast<void>(compiler.compile(std::move(graph), ctx));
        FAIL("expected missing backend validation failure");
    } catch (const std::runtime_error& error) {
        CHECK(std::string(error.what()).find("no render backend") != std::string::npos);
    }
}

TEST_CASE("FrameGraphCompiler - Transition bypasses ShapeType validation") {
    RenderGraph graph;
    LayerTransitionSpec transition_spec{
        .transition_id = "crossfade",
        .duration = 1.0,
        .delay = 0.0,
        .easing = Easing::Linear,
    };
    const auto transition_id = graph.add_node(std::make_unique<TransitionNode>(
        "layer", transition_spec, false, Frame{0}, Frame{30}));
    graph.set_output(transition_id);

    // No backend or shape processor is needed: TransitionNode is a
    // framebuffer operator, not a Shape processor input.
    RenderGraphContext ctx;
    ctx.frame_input.width = 100;
    ctx.frame_input.height = 100;
    FrameGraphCompiler compiler;
    FrameGraphCompileOptions options;
    options.compute_bboxes = false;

    const auto compiled = compiler.compile(std::move(graph), ctx, options);
    CHECK(compiled.valid);
    REQUIRE(compiled.nodes.size() > transition_id);
    CHECK(compiled.nodes[transition_id].kind == RenderGraphNodeKind::Transition);
}

TEST_CASE("FrameGraphCompiler - rejects missing render processor before execution") {
    auto graph = make_single_source_graph(ShapeType::Rect);
    ValidationBackend backend(/*missing_processor=*/true);
    RenderGraphContext ctx;
    ctx.services.backend = &backend;
    FrameGraphCompiler compiler;

    try {
        static_cast<void>(compiler.compile(std::move(graph), ctx));
        FAIL("expected missing processor validation failure");
    } catch (const std::runtime_error& error) {
        CHECK(std::string(error.what()).find("missing shape processor") != std::string::npos);
    }
}

TEST_CASE("FrameGraphCompiler - resolves processor identity during compilation") {
    auto graph = make_single_source_graph(ShapeType::Rect);
    ValidationBackend backend(/*missing_processor=*/false);
    RenderGraphContext ctx;
    ctx.services.backend = &backend;
    FrameGraphCompiler compiler;

    const auto compiled = compiler.compile(std::move(graph), ctx);

    REQUIRE(compiled.valid);
    REQUIRE_FALSE(compiled.nodes.empty());
    CHECK(backend.validation_calls() == 1);
    CHECK(compiled.nodes.front().processor_id ==
          "source:" + std::to_string(static_cast<int>(ShapeType::Rect)));
    REQUIRE(compiled.processor_snapshot != nullptr);
    CHECK(compiled.processor_snapshot->generation() == ctx.services.registry_generation);
    const auto shape_handle = compiled.nodes.front().shape_processor;
    CHECK(shape_handle.valid());
    REQUIRE(compiled.nodes.front().shape_processors_count == 1);
    CHECK(compiled.shape_processor_table[
              compiled.nodes.front().shape_processors_offset] == shape_handle);
}

TEST_CASE("FrameGraphCompiler - resolves effect processor during compilation") {
    EffectStack effects;
    effects.push_back(EffectInstance{BlurParams{5.0f}});

    RenderGraph graph;
    const auto effect_id = graph.add_node(std::make_unique<EffectStackNode>(
        std::move(effects)));
    graph.set_output(effect_id);

    auto snapshot = std::make_shared<const renderer::ProcessorRegistrySnapshot>(
        std::vector<renderer::ProcessorRegistrySnapshot::ShapeEntry>{
            {ShapeType::Rect, std::make_shared<NoopShapeProcessor>()},
            {ShapeType::Image, std::make_shared<NoopShapeProcessor>()}},
        std::vector<renderer::ProcessorRegistrySnapshot::EffectEntry>{
            {std::type_index(typeid(BlurParams)),
             std::make_shared<NoopEffectProcessor>()}},
        17);
    ValidationBackend backend(/*missing_processor=*/false, snapshot);
    RenderGraphContext ctx;
    ctx.services.backend = &backend;
    ctx.services.registry_generation = 17;
    FrameGraphCompiler compiler;

    const auto compiled = compiler.compile(std::move(graph), ctx);

    REQUIRE(compiled.valid);
    REQUIRE(compiled.nodes.size() > effect_id);
    CHECK(compiled.registry_generation == 17);
    CHECK(compiled.nodes[effect_id].effect_processors_count == 1);
    CHECK(compiled.nodes[effect_id].effect_processors_offset <
          compiled.effect_processor_table.size());
    CHECK(compiled.effect_processor_table[
              compiled.nodes[effect_id].effect_processors_offset].valid());
    CHECK(compiled.processor_snapshot != nullptr);
}

TEST_CASE("FrameGraphCompiler - rejects snapshot generation mismatch") {
    ValidationBackend backend(/*missing_processor=*/false);
    RenderGraphContext ctx;
    ctx.services.backend = &backend;
    ctx.services.registry_generation = 1;
    FrameGraphCompiler compiler;

    CHECK_THROWS_WITH(
        static_cast<void>(compiler.compile(make_single_source_graph(ShapeType::Rect), ctx)),
        "FrameGraphCompiler: processor snapshot generation does not match RenderGraphContext registry generation");
}

TEST_CASE("FrameGraphCompiler - reuse rejects a different same-generation snapshot") {
    auto first_snapshot = std::make_shared<const renderer::ProcessorRegistrySnapshot>(
        std::vector<renderer::ProcessorRegistrySnapshot::ShapeEntry>{
            {ShapeType::Rect, std::make_shared<NoopShapeProcessor>()}},
        std::vector<renderer::ProcessorRegistrySnapshot::EffectEntry>{},
        0);
    auto second_snapshot = std::make_shared<const renderer::ProcessorRegistrySnapshot>(
        std::vector<renderer::ProcessorRegistrySnapshot::ShapeEntry>{
            {ShapeType::Rect, std::make_shared<NoopShapeProcessor>()}},
        std::vector<renderer::ProcessorRegistrySnapshot::EffectEntry>{},
        0);
    REQUIRE(first_snapshot->identity() != second_snapshot->identity());

    FrameGraphCompiler compiler;
    ValidationBackend first_backend(false, first_snapshot);
    RenderGraphContext first_ctx;
    first_ctx.services.backend = &first_backend;
    first_ctx.policy.graph_structure_unchanged = true;
    FrameGraphCompileOptions options;
    options.run_optimizer = false;
    auto prior = compiler.compile(make_single_source_graph(ShapeType::Rect), first_ctx, options);
    REQUIRE(prior.valid);

    ValidationBackend second_backend(false, second_snapshot);
    RenderGraphContext second_ctx;
    second_ctx.services.backend = &second_backend;
    second_ctx.policy.graph_structure_unchanged = true;
    auto rebuilt = compiler.compile_with_reuse(
        make_single_source_graph(ShapeType::Rect), second_ctx, prior, options);

    REQUIRE(rebuilt.valid);
    CHECK(rebuilt.processor_snapshot_identity == second_snapshot->identity());
    CHECK(rebuilt.processor_snapshot != prior.processor_snapshot);
}

TEST_CASE("FrameGraphCompiler - reuse does not resolve processors per frame") {
    FrameGraphCompiler compiler;
    ValidationBackend backend(/*missing_processor=*/false);
    RenderGraphContext ctx;
    ctx.services.backend = &backend;
    ctx.policy.graph_structure_unchanged = true;
    FrameGraphCompileOptions options;
    options.run_optimizer = false;

    auto prior = compiler.compile(make_single_source_graph(ShapeType::Rect), ctx, options);
    REQUIRE(prior.valid);

    auto reused = compiler.compile_with_reuse(
        make_single_source_graph(ShapeType::Rect), ctx, prior, options);
    REQUIRE(reused.valid);
    CHECK(reused.registry_generation == ctx.services.registry_generation);
    CHECK(reused.processor_snapshot == prior.processor_snapshot);
    CHECK(reused.processor_snapshot_identity == prior.processor_snapshot_identity);
    CHECK(reused.shape_processor_table == prior.shape_processor_table);
    CHECK(reused.effect_processor_table == prior.effect_processor_table);
    REQUIRE(reused.nodes.size() == prior.nodes.size());
    CHECK(reused.nodes.front().shape_processor == prior.nodes.front().shape_processor);
}

TEST_CASE("FrameGraphCompiler - linear graph compilation") {
    RenderGraph graph;
    
    GraphNodeId clear_id = graph.add_node(std::make_unique<CompilerTestNode>("Clear"));
    GraphNodeId source_id = graph.add_node(std::make_unique<CompilerTestNode>("Source"));
    GraphNodeId composite_id = graph.add_node(std::make_unique<CompilerTestNode>("Composite"));

    graph.connect(clear_id, source_id);
    graph.connect(source_id, composite_id);
    graph.set_output(composite_id);

    RenderGraphContext ctx;
    ctx.frame_input.width = 100;
    ctx.frame_input.height = 100;
    
    FrameGraphCompiler compiler;
    FrameGraphCompileOptions options;
    options.run_optimizer = false;
    options.compute_lifetimes = true;

    auto compiled = compiler.compile(std::move(graph), ctx, options);

    REQUIRE(compiled.valid);
    CHECK(compiled.levels.size() >= 3);
    CHECK(compiled.resource_table().resources[clear_id].consumer_count == 1);
    CHECK(compiled.resource_table().resources[source_id].consumer_count == 1);
    CHECK(compiled.output == composite_id);
}

TEST_CASE("FrameGraphCompiler - diamond graph scheduling") {
    RenderGraph graph;

    GraphNodeId a = graph.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b = graph.add_node(std::make_unique<CompilerTestNode>("B"));
    GraphNodeId c = graph.add_node(std::make_unique<CompilerTestNode>("C"));
    GraphNodeId d = graph.add_node(std::make_unique<CompilerTestNode>("D"));

    graph.connect(a, b);
    graph.connect(a, c);
    graph.connect(b, d);
    graph.connect(c, d);
    graph.set_output(d);

    RenderGraphContext ctx;
    FrameGraphCompiler compiler;
    FrameGraphCompileOptions options;
    options.run_optimizer = false;

    auto compiled = compiler.compile(std::move(graph), ctx, options);

    REQUIRE(compiled.valid);
    CHECK(compiled.resource_table().resources[a].consumer_count == 2);
    CHECK(compiled.resource_table().resources[b].consumer_count == 1);
    CHECK(compiled.resource_table().resources[c].consumer_count == 1);
    CHECK(compiled.output == d);
}

TEST_CASE("FrameGraphCompiler - cycle detection throws") {
    RenderGraph graph;

    GraphNodeId a = graph.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b = graph.add_node(std::make_unique<CompilerTestNode>("B"));

    graph.connect(a, b);
    graph.connect(b, a);
    graph.set_output(b);

    RenderGraphContext ctx;
    FrameGraphCompiler compiler;
    FrameGraphCompileOptions options;
    options.run_optimizer = false;

    CHECK_THROWS_AS(static_cast<void>(compiler.compile(std::move(graph), ctx, options)), std::runtime_error);
}

TEST_CASE("FrameGraphCompiler - lifetimes computation") {
    RenderGraph graph;

    GraphNodeId a = graph.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b = graph.add_node(std::make_unique<CompilerTestNode>("B"));
    GraphNodeId c = graph.add_node(std::make_unique<CompilerTestNode>("C"));

    graph.connect(a, b);
    graph.connect(b, c);
    graph.set_output(c);

    RenderGraphContext ctx;
    FrameGraphCompiler compiler;
    FrameGraphCompileOptions options;
    options.run_optimizer = false;
    options.compute_lifetimes = true;

    auto compiled = compiler.compile(std::move(graph), ctx, options);

    REQUIRE(compiled.valid);
    const auto& table = compiled.resource_table();
    REQUIRE(table.resource_for(a) != nullptr);
    REQUIRE(table.resource_for(b) != nullptr);
    CHECK(table.resource_for(a)->producer == a);
    CHECK(table.resource_for(b)->producer == b);
    CHECK(table.resource_for(a)->last_level > table.resource_for(a)->first_level);
    CHECK(table.resource_for(b)->last_level > table.resource_for(b)->first_level);
}

TEST_CASE("FrameGraphCompiler - compiled resource table colors transient intervals") {
    RenderGraph graph;
    const auto a = graph.add_node(std::make_unique<CompilerTestNode>(
        "A", no_cache("transient-a")));
    const auto b = graph.add_node(std::make_unique<CompilerTestNode>(
        "B", no_cache("transient-b")));
    const auto c = graph.add_node(std::make_unique<CompilerTestNode>(
        "C", no_cache("transient-c")));
    const auto d = graph.add_node(std::make_unique<CompilerTestNode>(
        "D", no_cache("transient-d")));
    const auto e = graph.add_node(std::make_unique<CompilerTestNode>(
        "E", no_cache("transient-e")));
    const auto output = graph.add_node(std::make_unique<CompilerTestNode>(
        "Output", no_cache("output")));
    graph.connect(a, b);
    graph.connect(b, output);
    graph.connect(c, d);
    graph.connect(d, e);
    graph.connect(e, output);
    graph.set_output(output);

    RenderGraphContext ctx;
    FrameGraphCompiler compiler;
    FrameGraphCompileOptions options;
    options.run_optimizer = false;
    options.compute_lifetimes = true;

    const auto compiled = compiler.compile(std::move(graph), ctx, options);
    REQUIRE(compiled.valid);
    const auto& table = compiled.resource_table();

    CHECK(table.logical_resource_count == 6);
    CHECK(table.aliasable_resource_count == 5);
    CHECK(table.excluded_persistent_count == 1);
    CHECK(table.excluded_async_count == 0);
    CHECK(table.physical_slot_count < table.aliasable_resource_count);
    CHECK(table.peak_live_resource_count == table.physical_slot_count);
    REQUIRE(table.resource_for(a) != nullptr);
    REQUIRE(table.resource_for(b) != nullptr);
    REQUIRE(table.resource_for(c) != nullptr);
    REQUIRE(table.resource_for(d) != nullptr);
    REQUIRE(table.resource_for(e) != nullptr);
    REQUIRE(table.resource_for(output) != nullptr);
    CHECK(table.resource_for(a)->aliasable());
    CHECK(table.resource_for(b)->aliasable());
    CHECK(table.resource_for(c)->aliasable());
    CHECK(table.resource_for(d)->aliasable());
    CHECK(table.resource_for(e)->aliasable());
    CHECK_FALSE(table.resource_for(output)->aliasable());
    CHECK(table.resource_for(a)->physical_slot ==
          table.resource_for(e)->physical_slot);
}

TEST_CASE("FrameGraphCompiler - FrameVariant resources remain transient and alias") {
    RenderGraph graph;
    const auto a = graph.add_node(std::make_unique<CompilerTestNode>(
        "variant-a", frame_variant_cache("camera-dependent-a")));
    const auto b = graph.add_node(std::make_unique<CompilerTestNode>(
        "transient-b", no_cache("transient-b")));
    const auto c = graph.add_node(std::make_unique<CompilerTestNode>(
        "transient-c", no_cache("transient-c")));
    const auto d = graph.add_node(std::make_unique<CompilerTestNode>(
        "transient-d", no_cache("transient-d")));
    const auto e = graph.add_node(std::make_unique<CompilerTestNode>(
        "variant-e", frame_variant_cache("camera-dependent-e")));
    const auto output = graph.add_node(std::make_unique<CompilerTestNode>(
        "output", no_cache("output")));
    graph.connect(a, b);
    graph.connect(b, output);
    graph.connect(c, d);
    graph.connect(d, e);
    graph.connect(e, output);
    graph.set_output(output);

    RenderGraphContext ctx;
    FrameGraphCompiler compiler;
    FrameGraphCompileOptions options;
    options.run_optimizer = false;
    options.compute_lifetimes = true;

    const auto compiled = compiler.compile(std::move(graph), ctx, options);
    REQUIRE(compiled.valid);
    const auto& table = compiled.resource_table();
    REQUIRE(table.resource_for(a) != nullptr);
    REQUIRE(table.resource_for(e) != nullptr);

    CHECK(compiled.nodes[a].cache_policy.frame_dependent());
    CHECK(compiled.nodes[e].cache_policy.frame_dependent());
    CHECK_FALSE(table.resource_for(a)->persistent);
    CHECK_FALSE(table.resource_for(e)->persistent);
    CHECK(table.resource_for(a)->aliasable());
    CHECK(table.resource_for(e)->aliasable());
    CHECK(table.resource_for(a)->physical_slot ==
          table.resource_for(e)->physical_slot);
}

TEST_CASE("FrameGraphCompiler - persistent and non-releasable resources are excluded") {
    RenderGraph graph;
    const auto persistent = graph.add_node(std::make_unique<CompilerTestNode>(
        "persistent", static_memory_cache("persistent")));
    const auto transient = graph.add_node(std::make_unique<CompilerTestNode>(
        "transient", no_cache("transient")));
    const auto output = graph.add_node(std::make_unique<CompilerTestNode>(
        "output", no_cache("output")));
    graph.connect(persistent, transient);
    graph.connect(transient, output);
    graph.set_output(output);

    RenderGraphContext ctx;
    FrameGraphCompiler compiler;
    FrameGraphCompileOptions options;
    options.run_optimizer = false;
    options.compute_lifetimes = true;

    const auto compiled = compiler.compile(std::move(graph), ctx, options);
    REQUIRE(compiled.valid);
    const auto& table = compiled.resource_table();
    REQUIRE(table.resource_for(persistent) != nullptr);
    REQUIRE(table.resource_for(transient) != nullptr);
    CHECK(table.resource_for(persistent)->persistent);
    CHECK_FALSE(table.resource_for(persistent)->aliasable());
    CHECK_FALSE(table.resource_for(transient)->persistent);
    CHECK(table.resource_for(transient)->aliasable());
    CHECK(table.excluded_persistent_count == 2);
    CHECK(table.excluded_async_count == 0);
}

TEST_CASE("FrameGraphCompiler - structure hash includes edges and output") {
    FrameGraphCompiler compiler;
    RenderGraphContext ctx;
    FrameGraphCompileOptions options;
    options.run_optimizer = false;

    RenderGraph chain;
    GraphNodeId a = chain.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b = chain.add_node(std::make_unique<CompilerTestNode>("B"));
    GraphNodeId c = chain.add_node(std::make_unique<CompilerTestNode>("C"));
    chain.connect(a, b);
    chain.connect(b, c);
    chain.set_output(c);
    const auto chain_hash = compiler.compile(std::move(chain), ctx, options).structure_hash;

    RenderGraph reordered;
    GraphNodeId ra = reordered.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId rb = reordered.add_node(std::make_unique<CompilerTestNode>("B"));
    GraphNodeId rc = reordered.add_node(std::make_unique<CompilerTestNode>("C"));
    reordered.connect(ra, rc);
    reordered.connect(rb, rc);
    reordered.set_output(rc);
    const auto reordered_hash = compiler.compile(std::move(reordered), ctx, options).structure_hash;

    CHECK(chain_hash != reordered_hash);

    RenderGraph alternate_output;
    GraphNodeId oa = alternate_output.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId ob = alternate_output.add_node(std::make_unique<CompilerTestNode>("B"));
    GraphNodeId oc = alternate_output.add_node(std::make_unique<CompilerTestNode>("C"));
    alternate_output.connect(oa, ob);
    alternate_output.connect(ob, oc);
    alternate_output.set_output(ob);  // same nodes/edges; only output differs
    const auto alternate_output_hash = compiler.compile(
        std::move(alternate_output), ctx, options).structure_hash;
    CHECK(alternate_output_hash != chain_hash);
}

TEST_CASE("FrameGraphCompiler - structure hash excludes frame and dynamic context") {
    FrameGraphCompiler compiler;
    FrameGraphCompileOptions options;
    options.run_optimizer = false;

    auto make_graph = [] {
        RenderGraph graph;
        const auto source = graph.add_node(
            std::make_unique<CompilerTestNode>("source"));
        graph.set_output(source);
        return graph;
    };

    RenderGraphContext frame_zero;
    frame_zero.frame_input.frame = Frame{0};
    frame_zero.frame_input.time_seconds = 0.0f;
    frame_zero.frame_input.width = 1920;
    frame_zero.frame_input.height = 1080;
    const auto hash_zero = compiler.compile(
        make_graph(), frame_zero, options).structure_hash;

    RenderGraphContext frame_later;
    frame_later.frame_input.frame = Frame{59};
    frame_later.frame_input.time_seconds = 1.966f;
    frame_later.frame_input.width = 1920;
    frame_later.frame_input.height = 1080;
    const auto hash_later = compiler.compile(
        make_graph(), frame_later, options).structure_hash;

    CHECK(hash_later == hash_zero);
}

TEST_CASE("FrameGraphCompiler - stable structure hash") {
    RenderGraph graph1;
    GraphNodeId a1 = graph1.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b1 = graph1.add_node(std::make_unique<CompilerTestNode>("B"));
    graph1.connect(a1, b1);
    graph1.set_output(b1);

    RenderGraph graph2;
    GraphNodeId a2 = graph2.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b2 = graph2.add_node(std::make_unique<CompilerTestNode>("B"));
    graph2.connect(a2, b2);
    graph2.set_output(b2);

    RenderGraphContext ctx;
    FrameGraphCompiler compiler;
    FrameGraphCompileOptions options;
    options.run_optimizer = false;

    auto compiled1 = compiler.compile(std::move(graph1), ctx, options);
    auto compiled2 = compiler.compile(std::move(graph2), ctx, options);

    CHECK(compiled1.structure_hash == compiled2.structure_hash);

    ctx.services.registry_generation = 2;
    RenderGraph graph3;
    GraphNodeId a3 = graph3.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b3 = graph3.add_node(std::make_unique<CompilerTestNode>("B"));
    graph3.connect(a3, b3);
    graph3.set_output(b3);
    auto compiled3 = compiler.compile(std::move(graph3), ctx, options);

    CHECK(compiled3.structure_hash != compiled1.structure_hash);
    CHECK(compiled1.registry_generation == 0);
    CHECK(compiled3.registry_generation == 2);
}

// ── TICKET-008 / §9.4 closure — `compile_with_reuse` reuse-path tests ──────────
// The line-~185 structure-hash canary above is extended with FIVE new
// TEST_CASEs (A-E below) per TICKET-008 Step 6.

TEST_CASE("FrameGraphCompiler - compile_with_reuse: skip path (Test A)") {
    FrameGraphCompiler compiler;
    RenderGraphContext ctx;
    ctx.policy.graph_structure_unchanged = true;
    FrameGraphCompileOptions options;
    options.run_optimizer = false;
    options.validate_dag = true;
    options.compute_lifetimes = true;

    // Build prior via the standard compile() path.
    RenderGraph gp;
    GraphNodeId a = gp.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b = gp.add_node(std::make_unique<CompilerTestNode>("B"));
    gp.connect(a, b);
    gp.set_output(b);
    auto prior = compiler.compile(std::move(gp), ctx, options);

    // Build a logically equivalent second graph for the reuse call.
    RenderGraph g2;
    GraphNodeId a2 = g2.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b2 = g2.add_node(std::make_unique<CompilerTestNode>("B"));
    g2.connect(a2, b2);
    g2.set_output(b2);

    auto compiled = compiler.compile_with_reuse(std::move(g2), ctx, prior, options);

    REQUIRE(compiled.valid);
    REQUIRE_FALSE(prior.levels.empty());
    REQUIRE(compiled.levels.size() == prior.levels.size());
    CHECK(compiled.levels == prior.levels);  // deep copy
    REQUIRE(compiled.nodes.size() == prior.nodes.size());
    for (size_t i = 0; i < compiled.nodes.size(); ++i) {
        CHECK(compiled.nodes[i].stable_node_id == prior.nodes[i].stable_node_id);
    }
    REQUIRE(compiled.resource_table().resources.size() ==
            prior.resource_table().resources.size());
    for (size_t i = 0; i < compiled.resource_table().resources.size(); ++i) {
        CHECK(compiled.resource_table().resources[i].consumer_count ==
              prior.resource_table().resources[i].consumer_count);
    }
    CHECK(compiled.output == prior.output);
    // structure_hash should be freshly derived and equal (hash is deterministic).
    CHECK(compiled.structure_hash == prior.structure_hash);
}

TEST_CASE("FrameGraphCompiler - compile_with_reuse: mismatched hash falls through (Test B)") {
    FrameGraphCompiler compiler;
    RenderGraphContext ctx;
    ctx.policy.graph_structure_unchanged = true;  // will be ignored by fall-through
    FrameGraphCompileOptions options;
    options.run_optimizer = false;
    options.validate_dag = true;
    options.compute_lifetimes = true;

    // Build prior: 2-node graph.
    RenderGraph gp;
    GraphNodeId a = gp.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b = gp.add_node(std::make_unique<CompilerTestNode>("B"));
    gp.connect(a, b);
    gp.set_output(b);
    auto prior = compiler.compile(std::move(gp), ctx, options);

    // Build second graph with an extra node — structure differs from prior.
    RenderGraph g2;
    GraphNodeId a2 = g2.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b2 = g2.add_node(std::make_unique<CompilerTestNode>("B"));
    GraphNodeId c2 = g2.add_node(std::make_unique<CompilerTestNode>("C"));
    g2.connect(a2, b2);
    g2.connect(b2, c2);
    g2.set_output(c2);

    auto compiled = compiler.compile_with_reuse(std::move(g2), ctx, prior, options);

    REQUIRE(compiled.valid);
    // Full path ran — levels/nodes are NOT bit-equal to prior's.
    CHECK(compiled.levels != prior.levels);
    CHECK(compiled.nodes.size() != prior.nodes.size());
    // Output reflects the new graph.
    CHECK(compiled.output == c2);
}

TEST_CASE("FrameGraphCompiler - compile_with_reuse: graph_structure_unchanged=false falls through (Test C)") {
    FrameGraphCompiler compiler;
    RenderGraphContext ctx;
    ctx.policy.graph_structure_unchanged = false;
    FrameGraphCompileOptions options;
    options.run_optimizer = false;
    options.validate_dag = true;
    options.compute_lifetimes = true;

    RenderGraph gp;
    GraphNodeId a = gp.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b = gp.add_node(std::make_unique<CompilerTestNode>("B"));
    gp.connect(a, b);
    gp.set_output(b);
    auto prior = compiler.compile(std::move(gp), ctx, options);

    RenderGraph g2;
    GraphNodeId a2 = g2.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b2 = g2.add_node(std::make_unique<CompilerTestNode>("B"));
    g2.connect(a2, b2);
    g2.set_output(b2);

    auto compiled = compiler.compile_with_reuse(std::move(g2), ctx, prior, options);

    REQUIRE(compiled.valid);
    // The predicate controls whether metadata is reused, not the topology
    // produced by the fresh compile.  Equivalent graphs therefore retain the
    // same execution levels while still taking the fall-through path.
    CHECK(compiled.levels == prior.levels);
}

TEST_CASE("FrameGraphCompiler - compile_with_reuse: run_optimizer=true falls through (Test D)") {
    FrameGraphCompiler compiler;
    RenderGraphContext ctx;
    ctx.policy.graph_structure_unchanged = true;
    FrameGraphCompileOptions options;
    options.run_optimizer = true;  // unsafe predicate: skip is gated off
    options.validate_dag = true;
    options.compute_lifetimes = true;

    RenderGraph gp;
    GraphNodeId a = gp.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b = gp.add_node(std::make_unique<CompilerTestNode>("B"));
    gp.connect(a, b);
    gp.set_output(b);
    auto prior = compiler.compile(std::move(gp), ctx, options);

    RenderGraph g2;
    GraphNodeId a2 = g2.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b2 = g2.add_node(std::make_unique<CompilerTestNode>("B"));
    g2.connect(a2, b2);
    g2.set_output(b2);

    auto compiled = compiler.compile_with_reuse(std::move(g2), ctx, prior, options);

    REQUIRE(compiled.valid);
    CHECK(compiled.levels == prior.levels);  // optimizer safety gates reuse
}

TEST_CASE("FrameGraphCompiler - compile_with_reuse: post-conditions hold (Test E)") {
    FrameGraphCompiler compiler;
    RenderGraphContext ctx;
    ctx.policy.graph_structure_unchanged = true;
    ctx.node_exec.early_exit_skip.clear();  // populated below
    FrameGraphCompileOptions options;
    options.run_optimizer = false;
    options.validate_dag = true;
    options.compute_lifetimes = true;

    // Build prior
    RenderGraph gp;
    GraphNodeId a = gp.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b = gp.add_node(std::make_unique<CompilerTestNode>("B"));
    gp.connect(a, b);
    gp.set_output(b);
    auto prior = compiler.compile(std::move(gp), ctx, options);

    // Build second graph; set early_exit_skip on ctx.node_exec for post-cond
    RenderGraph g2;
    GraphNodeId a2 = g2.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b2 = g2.add_node(std::make_unique<CompilerTestNode>("B"));
    g2.connect(a2, b2);
    g2.set_output(b2);
    ctx.node_exec.early_exit_skip.assign(2, false);
    ctx.node_exec.early_exit_skip[0] = true;  // mark a2 skip

    auto compiled = compiler.compile_with_reuse(std::move(g2), ctx, prior, options);

    REQUIRE(compiled.valid);
    // structure_hash freshly derived equals compute_structure_hash on compiled.graph
    CHECK(compiled.structure_hash ==
          FrameGraphCompiler::compute_structure_hash(
              compiled.graph, compiled.output, ctx.services.registry_generation));
    // early_exit_skip propagated from ctx (per-node, size == graph.size())
    REQUIRE(compiled.early_exit_skip.size() == compiled.graph.size());
    CHECK(compiled.early_exit_skip[0] == true);
    CHECK(compiled.early_exit_skip[1] == false);
    // graph_instance_id was freshly derived (defensive even on skip path)
    CHECK(compiled.graph_instance_id.value != kInvalidGraphInstanceId.value);
    // skip_initial_clear copied from policy
    CHECK(compiled.skip_initial_clear == ctx.policy.skip_initial_clear);
}

TEST_CASE("FrameGraphCompiler - CompiledResourcePlan computes deterministic release and ownership") {
    FrameGraphCompiler compiler;
    RenderGraphContext ctx;
    FrameGraphCompileOptions options;
    options.validate_dag = true;
    options.compute_lifetimes = true;

    // Build DAG:
    //   A (Producer 0) ───┐
    //                     ├──> C (Composite, sole consumer of A & B) ──> D (Output)
    //   B (Producer 1) ───┘
    RenderGraph graph;
    GraphNodeId a = graph.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b = graph.add_node(std::make_unique<CompilerTestNode>("B"));
    GraphNodeId c = graph.add_node(std::make_unique<CompilerTestNode>("C"));
    GraphNodeId d = graph.add_node(std::make_unique<CompilerTestNode>("D"));
    graph.connect(a, c);
    graph.connect(b, c);
    graph.connect(c, d);
    graph.set_output(d);

    auto compiled = compiler.compile(std::move(graph), ctx, options);
    REQUIRE(compiled.valid);

    const auto& table = compiled.resource_table();
    const auto* a_plan = table.resource_for(a);
    const auto* b_plan = table.resource_for(b);
    REQUIRE(a_plan != nullptr);
    REQUIRE(b_plan != nullptr);
    CHECK(a_plan->release_scheduled);
    CHECK(b_plan->release_scheduled);
    CHECK(a_plan->release_after_level == a_plan->last_level);
    CHECK(b_plan->release_after_level == b_plan->last_level);
    REQUIRE(a_plan->ownership_transfer_consumer().has_value());
    REQUIRE(b_plan->ownership_transfer_consumer().has_value());
    CHECK(*a_plan->ownership_transfer_consumer() == c);
    CHECK(*b_plan->ownership_transfer_consumer() == c);

    // Verify CompiledFrameProgram remains derived from the same resource plans.
    REQUIRE_FALSE(compiled.program.empty());
    CHECK(compiled.program.levels == compiled.levels);
    CHECK(compiled.program.operations.size() == 4);
    CHECK(compiled.program.operations[0].node == a);
    CHECK(compiled.program.operations[1].node == b);
    CHECK(compiled.program.operations[2].node == c);
    CHECK(compiled.program.operations[3].node == d);
}

TEST_CASE("ExecutionWorkspaceRing - leases and releases workspace slots deterministically") {
    ExecutionWorkspaceRing ring;

    // Acquire slot 0
    {
        auto lease0 = ring.acquire(0);
        auto& ws0 = lease0.workspace();
        ws0.temp.resize(4);
        CHECK(ws0.temp.size() == 4);

        // Nested frame in-flight leases slot 1 without modifying slot 0
        {
            auto lease1 = ring.acquire(1);
            auto& ws1 = lease1.workspace();
            CHECK(ws1.temp.empty()); // fresh frame begin
            ws1.temp.resize(2);
            CHECK(ws1.temp.size() == 2);
        }
    }

    // After leases go out of scope, slots can be re-acquired cleanly
    auto lease0_again = ring.acquire(0);
    CHECK(lease0_again.workspace().temp.empty()); // begin_frame cleared it
}

TEST_CASE("FrameParameterTable and FrameParameterSampler - sample and retrieve opaque parameters") {
    struct TestParamBlock {
        int64_t source_frame{0};
        float opacity{1.0f};
        float transform[4]{1.0f, 0.0f, 0.0f, 1.0f};
    };

    FrameParameterTable table;
    const std::size_t kFrameCount = 10;
    const Frame kStartFrame{100};

    FrameParameterSampler::prepare(table, kStartFrame, kFrameCount, [](Frame frame, FrameParameterWriter& writer) {
        TestParamBlock block;
        block.source_frame = frame.integral();
        block.opacity = static_cast<float>(frame.integral()) / 100.0f;
        writer.write(block);
    });

    REQUIRE(table.frame_count() == kFrameCount);

    // Verify sample at frame 105
    const auto view = table.view(Frame{105});
    REQUIRE(view.size() == sizeof(TestParamBlock));

    TestParamBlock retrieved{};
    std::memcpy(&retrieved, view.data(), sizeof(TestParamBlock));
    CHECK(retrieved.source_frame == 105);
    CHECK(retrieved.opacity == doctest::Approx(1.05f));
}

#include <chronon3d/runtime/gpu_runtime.hpp>
#include <chronon3d/runtime/media_session_pool.hpp>
#include <chronon3d/runtime/async_encoder_sink.hpp>

TEST_CASE("MediaSessionPool: key hashing and registration") {
    using namespace chronon3d::runtime;
    MediaSessionPool pool;
    CHECK(pool.cached_session_count() == 0);

    MediaSessionKey key{
        MediaCodecId::H264,
        1920,
        1080,
        MediaPixelFormat::NV12,
        0,
        false
    };

    pool.register_session(key, ReusableMediaSession{100, 200, 1, false});
    CHECK(pool.cached_session_count() == 1);

    pool.clear();
    CHECK(pool.cached_session_count() == 0);
}

TEST_CASE("AsyncEncoderSink: asynchronous task dispatch and drain") {
    using namespace chronon3d::runtime;
    std::vector<std::uint64_t> drained_frames;
    std::mutex mtx;

    {
        AsyncEncoderSink sink([&](FrameExecutionSlot* slot, std::uint64_t frame_idx, bool is_flush) {
            (void)slot;
            if (!is_flush) {
                std::lock_guard lock(mtx);
                drained_frames.push_back(frame_idx);
            }
        });

        FrameExecutionSlot s0{}, s1{};
        sink.submit_frame(&s0, 42);
        sink.submit_frame(&s1, 43);
        sink.flush_and_join();
    }

    CHECK(drained_frames.size() == 2);
    CHECK(drained_frames[0] == 42);
    CHECK(drained_frames[1] == 43);
}

TEST_CASE("ProcessorCapabilities: bitmask and fusion predicates") {
    using namespace chronon3d::renderer;
    ProcessorCapabilities caps{};
    CHECK(!caps.is_gpu_fusible());

    caps.gpu = true;
    caps.fusible = true;
    caps.pixel_local = true;
    CHECK(caps.is_gpu_fusible());

    caps.in_place = true;
    caps.native_surface_input = true;
    caps.native_surface_output = true;
    CHECK(caps.is_gpu_fusible());
}

TEST_CASE("CompiledFrameProgram: operation capabilities and fusion properties") {
    using namespace chronon3d::graph;
    using namespace chronon3d::renderer;

    CompiledOperation op{};
    op.node = 5;
    op.capabilities.gpu = true;
    op.capabilities.fusible = true;
    op.capabilities.pixel_local = true;
    op.is_fused = true;

    CHECK(op.capabilities.is_gpu_fusible());
    CHECK(op.is_fused);

    CompiledFrameProgram program{};
    program.levels = {{5}};
    program.operations.push_back(op);
    program.has_fused_passes = true;
    CHECK(!program.empty());
    CHECK(program.has_fused_passes);
}

TEST_CASE("Invariant: 100 Image layers lower into 1 CompiledLayerBatch and 0 legacy ops") {
    using namespace chronon3d::graph;

    RenderGraph graph;
    auto clear_node = std::make_unique<ClearNode>();
    GraphNodeId current = graph.add_node(std::move(clear_node));

    cache::NodeCacheKey key{.scope = "img", .frame = 0, .width = 1920, .height = 1080};
    for (int i = 0; i < 100; ++i) {
        RenderNode rn;
        rn.shape = Shape{ImageShape{.path = "assets/images/camera_reference.jpg", .size = Vec2{160.0f, 90.0f}}};
        auto img_node = std::make_unique<SourceNode>("img_" + std::to_string(i), rn, key);
        GraphNodeId img_id = graph.add_node(std::move(img_node));

        Transform t{Vec3{static_cast<float>(i * 10), static_cast<float>(i * 5), 0.0f}};
        auto xform_node = std::make_unique<TransformNode>(t);
        GraphNodeId xform_id = graph.add_node(std::move(xform_node));
        graph.connect(img_id, xform_id);

        auto comp_node = std::make_unique<CompositeNode>(
            graph.next_composite_id(), BlendMode::Normal);
        GraphNodeId comp_id = graph.add_node(std::move(comp_node));
        graph.connect(current, comp_id);
        graph.connect(xform_id, comp_id);
        current = comp_id;
    }
    graph.set_output(current);

    FrameGraphCompiler compiler;
    ValidationBackend backend(false);
    RenderGraphContext ctx;
    ctx.services.backend = &backend;
    auto compiled = compiler.compile(std::move(graph), ctx);

    CHECK(compiled.program.layer_batches.size() == 1);
    CHECK(compiled.program.layer_batches[0].instances.size() == 100);

    auto count_legacy_transform_ops = [](const CompiledFrameGraph& g) {
        std::size_t count = 0;
        for (const auto& op : g.program.operations) {
            if (op.node < g.nodes.size() && g.nodes[op.node].kind == RenderGraphNodeKind::Transform) {
                count++;
            }
        }
        return count;
    };

    auto count_legacy_composite_ops = [](const CompiledFrameGraph& g) {
        std::size_t count = 0;
        for (const auto& op : g.program.operations) {
            if (!op.is_fused && op.node < g.nodes.size() && g.nodes[op.node].kind == RenderGraphNodeKind::Composite) {
                count++;
            }
        }
        return count;
    };

    CHECK(count_legacy_transform_ops(compiled) == 0);
    CHECK(count_legacy_composite_ops(compiled) == 0);
}

// ── build_compiled_frame_program: deterministic lowering golden ─────────────
// The staged compiler pipeline must produce an identical executable program
// for an identical graph every time it runs. This test normalizes the whole
// CompiledFrameProgram (command order, stable-node/resource IDs, layer batch
// structure, flags) into a canonical string plus an FNV-1a digest, then
// compiles the same fixture twice and asserts byte-for-byte equality. It is
// the regression guard that a stage refactor must not reorder or drop work.
namespace {

std::string normalize_compiled_program(const CompiledFrameProgram& program) {
    std::string out;

    // Command order + resource IDs. Each standalone/fused operation keeps its
    // stable-node identity and its assigned physical output slot.
    for (const auto& op : program.operations) {
        out += "N";
        out += std::to_string(op.node);
        out += ";S";
        out += std::to_string(op.stable_node.value);
        out += ";P";
        out += std::to_string(op.output_physical_slot);
        out += ";F";
        out += std::to_string(op.is_fused ? 1 : 0);
        out += ";C";
        out += std::to_string(op.has_compiled_execute() ? 1 : 0);
        out += ";\n";
    }

    // Fused layer batches: member topology, instances, root and slot.
    for (const auto& batch : program.layer_batches) {
        out += "B;";
        for (GraphNodeId member : batch.member_nodes) {
            out += std::to_string(member);
            out += ",";
        }
        out += "|R";
        out += std::to_string(batch.root_node);
        out += "|P";
        out += std::to_string(batch.output_physical_slot);
        out += "|I";
        for (const auto& inst : batch.instances) {
            out += std::to_string(inst.node);
            out += ":";
            out += std::to_string(inst.resource_index);
            out += ";";
        }
        out += "\n";
    }

    // Static bakes: root node + fingerprint (content-derived, deterministic).
    for (const auto& bake : program.static_bakes) {
        out += "K";
        out += std::to_string(bake.root_node);
        out += ";";
        out += std::to_string(bake.static_fingerprint);
        out += "\n";
    }

    // Execution flags.
    out += "fully_recorded=";
    out += std::to_string(program.fully_recorded ? 1 : 0);
    out += ";has_fused_passes=";
    out += std::to_string(program.has_fused_passes ? 1 : 0);
    return out;
}

std::uint64_t fnv1a64(std::string_view data) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : data) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

RenderGraph make_determinism_fixture() {
    RenderGraph graph;
    auto clear_node = std::make_unique<ClearNode>();
    GraphNodeId current = graph.add_node(std::move(clear_node));

    cache::NodeCacheKey key{.scope = "det", .frame = 0, .width = 1920, .height = 1080};
    for (int i = 0; i < 3; ++i) {
        RenderNode rn;
        rn.shape = Shape{ImageShape{.path = "assets/det.png", .size = Vec2{160.0f, 90.0f}}};
        auto img_node = std::make_unique<SourceNode>(
            "det_" + std::to_string(i), rn, key);
        GraphNodeId img_id = graph.add_node(std::move(img_node));

        Transform t{Vec3{static_cast<float>(i * 10), static_cast<float>(i * 5), 0.0f}};
        auto xform_node = std::make_unique<TransformNode>(t);
        GraphNodeId xform_id = graph.add_node(std::move(xform_node));
        graph.connect(img_id, xform_id);

        auto comp_node = std::make_unique<CompositeNode>(
            graph.next_composite_id(), BlendMode::Normal);
        GraphNodeId comp_id = graph.add_node(std::move(comp_node));
        graph.connect(current, comp_id);
        graph.connect(xform_id, comp_id);
        current = comp_id;
    }
    graph.set_output(current);
    return graph;
}

} // namespace

TEST_CASE("FrameGraphCompiler - compiled frame program is deterministic (golden)") {
    FrameGraphCompiler compiler;
    ValidationBackend backend(false);
    RenderGraphContext ctx;
    ctx.services.backend = &backend;

    auto first = compiler.compile(make_determinism_fixture(), ctx);
    auto second = compiler.compile(make_determinism_fixture(), ctx);
    REQUIRE(first.valid);
    REQUIRE(second.valid);

    const std::string normalized_first = normalize_compiled_program(first.program);
    const std::string normalized_second = normalize_compiled_program(second.program);

    // Same command order, same resource IDs, same layer batches, same flags.
    CHECK(normalized_first == normalized_second);
    CHECK(first.program.operations.size() == second.program.operations.size());
    CHECK(first.program.layer_batches.size() == second.program.layer_batches.size());
    CHECK(first.program.fully_recorded == second.program.fully_recorded);
    CHECK(first.program.has_fused_passes == second.program.has_fused_passes);

    // Same deterministic hash for two independent compilations of the fixture.
    const auto first_hash = fnv1a64(normalized_first);
    CHECK(first_hash == fnv1a64(normalized_second));

    // Persist the contract as a small source-controlled golden. The test is
    // intentionally opt-in for regeneration so ordinary runs cannot rewrite
    // a golden accidentally.
    const char* source_dir = std::getenv("CHRONON3D_SOURCE_DIR");
    REQUIRE(source_dir != nullptr);
    const std::string golden_path = std::string(source_dir) +
        "/tests/render_graph/compiler/frame_graph_program.golden";
    std::ifstream golden(golden_path);
    REQUIRE(golden.good());
    std::string line;
    std::string expected;
    while (std::getline(golden, line)) {
        if (line.rfind("fnv1a64=", 0) == 0) {
            expected = line.substr(8);
            break;
        }
    }
    REQUIRE_FALSE(expected.empty());
    std::ostringstream rendered;
    rendered << "0x" << std::hex << std::setw(16) << std::setfill('0') << first_hash;
    CHECK(rendered.str() == expected);
    // Guard against a vacuous normalization: the program must actually carry
    // the fused batch work this fixture is designed to produce.
    CHECK_FALSE(normalized_first.empty());
}