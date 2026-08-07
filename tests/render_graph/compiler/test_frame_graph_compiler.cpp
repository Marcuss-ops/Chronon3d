#include <doctest/doctest.h>

#include <chronon3d/render_graph/compiler/frame_graph_compiler.hpp>
#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/transition_node.hpp>
#include <chronon3d/render_graph/nodes/text_run_node.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <memory>
#include <stdexcept>
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

class ValidationBackend final : public RenderBackend {
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

    renderer::ShapeProcessor* resolve_shape_processor(
        const RenderNode&) const noexcept override {
        ++m_shape_resolve_calls;
        return reinterpret_cast<renderer::ShapeProcessor*>(1);
    }

    renderer::EffectProcessor* resolve_effect_processor(
        std::type_index) const noexcept override {
        ++m_effect_resolve_calls;
        return reinterpret_cast<renderer::EffectProcessor*>(1);
    }

    std::optional<RenderBackendError> validate_render_node(
        const RenderNode&) const override {
        ++m_validation_calls;
        if (m_missing_processor) {
            return RenderBackendError{
                RenderBackendErrorCode::InvalidInput,
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
    [[nodiscard]] int shape_resolve_calls() const noexcept {
        return m_shape_resolve_calls;
    }
    [[nodiscard]] int effect_resolve_calls() const noexcept {
        return m_effect_resolve_calls;
    }

private:
    bool m_missing_processor{false};
    std::shared_ptr<const renderer::ProcessorRegistrySnapshot> m_snapshot;
    mutable int m_validation_calls{0};
    mutable int m_shape_resolve_calls{0};
    mutable int m_effect_resolve_calls{0};
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
        processor_address = snapshot->shape(handle);
        CHECK(processor_address != nullptr);
        CHECK(snapshot->generation() == registry.generation());
    }

    CHECK(destructions == 0);
    REQUIRE(snapshot != nullptr);
    const auto handle = snapshot->shape_handle(ShapeType::Rect);
    CHECK(snapshot->shape(handle) == processor_address);
    snapshot.reset();
    CHECK(destructions == 1);
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
        CHECK(compiled.processor_snapshot->shape(
                  compiled.nodes.front().shape_processor) == processor.get());
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
    CHECK(backend.effect_resolve_calls() == 0);
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
    CHECK(second_backend.shape_resolve_calls() == 0);
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
    CHECK(backend.shape_resolve_calls() == 0);

    auto reused = compiler.compile_with_reuse(
        make_single_source_graph(ShapeType::Rect), ctx, prior, options);
    REQUIRE(reused.valid);
    CHECK(backend.shape_resolve_calls() == 0);
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
    CHECK(compiled.consumer_counts[clear_id] == 1);
    CHECK(compiled.consumer_counts[source_id] == 1);
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
    CHECK(compiled.consumer_counts[a] == 2);
    CHECK(compiled.consumer_counts[b] == 1);
    CHECK(compiled.consumer_counts[c] == 1);
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
    
    // Lifetimes checks
    CHECK(compiled.lifetimes[a].producer == a);
    CHECK(compiled.lifetimes[b].producer == b);
    CHECK(compiled.lifetimes[a].last_level > compiled.lifetimes[a].first_level);
    CHECK(compiled.lifetimes[b].last_level > compiled.lifetimes[b].first_level);
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
    CHECK(compiled.consumer_counts == prior.consumer_counts);
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
