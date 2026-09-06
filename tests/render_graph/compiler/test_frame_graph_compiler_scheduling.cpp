// test_frame_graph_compiler_scheduling.cpp — Split from
// test_frame_graph_compiler_fixtures.cpp: linear/diamond/cycle scheduling,
// lifetimes, transient alias coloring and structure-hash determinism group.
#include "test_frame_graph_compiler_fixtures.hpp"

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
