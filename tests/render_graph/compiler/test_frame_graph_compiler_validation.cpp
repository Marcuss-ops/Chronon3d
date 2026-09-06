// test_frame_graph_compiler_validation.cpp — Split from
// test_frame_graph_compiler_fixtures.cpp: compiler validation, coverage,
// processor resolution and snapshot-generation/reuse identity gate group.
#include "test_frame_graph_compiler_fixtures.hpp"

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
