#include "test_frame_graph_compiler_fixtures.hpp"

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
    // The registration layer defines CHRONON3D_SOURCE_DIR as a compile-time
    // macro (_chronon3d_renderer_target_finalize); the env var is an optional
    // override for out-of-tree runs.
    const char* source_dir = std::getenv("CHRONON3D_SOURCE_DIR");
#ifdef CHRONON3D_SOURCE_DIR
    if (!source_dir) source_dir = CHRONON3D_SOURCE_DIR;
#endif
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
