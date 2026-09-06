// test_frame_graph_compiler_fixtures.cpp — processor-registry snapshot
// ownership and isolation TEST_CASEs (split; each part ≤ 400 LOC):
//   - test_frame_graph_compiler_validation.cpp  (compiler validation group)
//   - test_frame_graph_compiler_scheduling.cpp  (linear/diamond/lifetimes/
//     alias coloring/structure hash group)
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
