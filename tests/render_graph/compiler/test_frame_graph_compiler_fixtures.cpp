#include "test_frame_graph_compiler_fixtures.hpp"

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
