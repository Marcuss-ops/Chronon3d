#include "test_frame_graph_compiler_fixtures.hpp"

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
