// =============================================================================
// test_node_identity.cpp — Work Package 4 / PR 4.5
//   Stable node identity: types, deterministic hashing, type-safety factory.
//
// Lock-down tests for the identity contract in
// include/chronon3d/render_graph/core/node_identity.hpp and the
// PR-4 wiring exposed through `scene_program_store.hpp::make_precomp_key`:
//
//   • GraphInstanceId / StableNodeId are content-derived uint64_t
//     with `kInvalid...Id == 0` sentinels.
//   • hash_stable_node_inputs(layer_id_hash, kind_and_name_hash) is
//     deterministic, returns 1..max for all non-zero input pairs
//     (null-guard), and survives the (0, 0) edge case.
//   • NodeIdentity{graph, node} has working operator<=>,
//     operator==, and std::hash specialization that avoids trivial
//     collisions when exactly one half changes.
//   • PrecompInstanceKey is still an AGGREGATE — designated-init
//     call sites keep compiling — and `make_precomp_key()` is a
//     type-safe alias for the equivalent literal.
//   • FrameGraphCompiler populates `CompiledFrameGraph::graph_instance_id`
//     and per-node `CompiledNodeInfo::stable_node_id`; structurally
//     identical graphs re-compile to the same `graph_instance_id`.
//
// Test categories:
//   §1 hash_stable_node_inputs — determinism + null-guard
//   §2 NodeIdentity — validity, comparison, hash
//   §3 PrecompInstanceKey / make_precomp_key — aggregate-ness,
//     type-safety, std::hash
//   §4 FrameGraphCompiler wiring — produced ids are stable across
//     re-compilation of structurally identical graphs.
// =============================================================================

#include <doctest/doctest.h>

#include <chronon3d/render_graph/core/node_identity.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/internal/render_graph/cache/scene_program_store.hpp>
#include <chronon3d/render_graph/compiler/frame_graph_compiler.hpp>
#include <chronon3d/render_graph/compiler/frame_graph_compile_options.hpp>
#include <chronon3d/cache/node_cache.hpp>

#include <memory>
#include <set>
#include <unordered_set>

using namespace chronon3d;
using namespace chronon3d::graph;

// ── Test node used by §4 (mirrors tests/render_graph/compiler/test_frame_graph_compiler.cpp) ──

namespace {

class CompilerTestNode final : public RenderGraphNode {
public:
    explicit CompilerTestNode(std::string n)
        : RenderGraphNode(static_memory_cache("test")), m_name(std::move(n)) {}

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
        return NodeExecResult{};
    }

private:
    std::string m_name;
};

} // namespace


// ═════════════════════════════════════════════════════════════════════════════
// §1: hash_stable_node_inputs — determinism, null-guard, sensitivity
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("node_identity: hash_stable_node_inputs is deterministic for the same pair") {
    const auto h1 = hash_stable_node_inputs(0xdeadbeefULL, 0x12345678ULL);
    const auto h2 = hash_stable_node_inputs(0xdeadbeefULL, 0x12345678ULL);
    const auto h3 = hash_stable_node_inputs(0xdeadbeefULL, 0x12345678ULL);
    CHECK(h1 == h2);
    CHECK(h2 == h3);
    CHECK(h1 != kInvalidStableNodeId);  // null-guard: never zero
}

TEST_CASE("node_identity: hash_stable_node_inputs never returns 0 (null-guard)") {
    // Edge case where the mix nominally yields the sentinel.
    const auto h_all_zero = hash_stable_node_inputs(0, 0);
    CHECK(h_all_zero != 0);

    // Sweep a few obvious candidates for "yielded 0" — none should.
    for (std::uint64_t a = 0; a < 16; ++a) {
        for (std::uint64_t b = 0; b < 16; ++b) {
            const auto h = hash_stable_node_inputs(a, b);
            CHECK(h != 0);
        }
    }
}

TEST_CASE("node_identity: hash_stable_node_inputs is sensitive to both lanes") {
    const auto base = hash_stable_node_inputs(7, 11);

    CHECK(base != hash_stable_node_inputs(8, 11));   // lane A changed
    CHECK(base != hash_stable_node_inputs(7, 12));   // lane B changed
    CHECK(base != hash_stable_node_inputs(8, 12));   // both changed
}

TEST_CASE("node_identity: hash_stable_node_inputs has wide output range") {
    // FNV-1a over (a, b) ∈ [1, 64]² has at-most-trivial 4096 distinct inputs.
    // We deliberately don't pin to exact equality (collisions are allowed
    // by the hash family), but we do require that the hash family produces
    // at least ~98% distinct outputs for that grid — a sanity bound that
    // catches a degenerate implementation.
    std::set<std::uint64_t> seen;
    for (std::uint64_t a = 1; a <= 64; ++a) {
        for (std::uint64_t b = 1; b <= 64; ++b) {
            const auto h = hash_stable_node_inputs(a, b);
            CHECK(h != 0);
            seen.insert(h);
        }
    }
    // 64*64 = 4096 inputs → expect strong distinctness for FNV-1a.
    CHECK(seen.size() >= 4096 * 98 / 100);
}

// ═════════════════════════════════════════════════════════════════════════════
// §2: NodeIdentity — validity, comparison, hashing
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("node_identity: default NodeIdentity is invalid") {
    NodeIdentity id{};
    CHECK_FALSE(id.valid());

    // kInvalid sentinels are explicit too.
    CHECK(kInvalidGraphInstanceId == 0);
    CHECK(kInvalidStableNodeId    == 0);
}

TEST_CASE("node_identity: NodeIdentity.valid() requires BOTH halves") {
    SUBCASE("both valid → valid") {
        NodeIdentity id{GraphInstanceId{42}, StableNodeId{42}};
        CHECK(id.valid());
    }
    SUBCASE("graph invalid → invalid") {
        NodeIdentity id{kInvalidGraphInstanceId, StableNodeId{42}};
        CHECK_FALSE(id.valid());
    }
    SUBCASE("node invalid → invalid") {
        NodeIdentity id{GraphInstanceId{42}, kInvalidStableNodeId};
        CHECK_FALSE(id.valid());
    }
    SUBCASE("both invalid → invalid (default ctor path)") {
        NodeIdentity id{};
        CHECK_FALSE(id.valid());
    }
}

TEST_CASE("node_identity: NodeIdentity operator<=> gives lexicographic order graph→node") {
    NodeIdentity a{GraphInstanceId{1}, StableNodeId{5}};
    NodeIdentity b{GraphInstanceId{2}, StableNodeId{1}};   // graph larger
    NodeIdentity c{GraphInstanceId{1}, StableNodeId{6}};   // node larger

    CHECK((a <=> b) == std::strong_ordering::less);   // (1,5) < (2,1)
    CHECK((b <=> a) == std::strong_ordering::greater);
    CHECK((a <=> c) == std::strong_ordering::less);   // (1,5) < (1,6)
    CHECK((a <=> NodeIdentity{1, 5}) == std::strong_ordering::equal);
}

TEST_CASE("node_identity: std::hash<NodeIdentity> distinguishes (g1,n1) from (g2,n1) and (g1,n2)") {
    using std::hash;

    NodeIdentity a{GraphInstanceId{1}, StableNodeId{1}};
    NodeIdentity b{GraphInstanceId{2}, StableNodeId{1}};   // different graph
    NodeIdentity c{GraphInstanceId{1}, StableNodeId{2}};   // different node

    std::unordered_set<NodeIdentity> bucket;
    bucket.insert(a);
    bucket.insert(b);
    bucket.insert(c);

    CHECK(bucket.size() == 3);
    CHECK(bucket.count(a) == 1);
    CHECK(bucket.count(b) == 1);
    CHECK(bucket.count(c) == 1);
    CHECK(hash<NodeIdentity>{}(a) != hash<NodeIdentity>{}(b));
    CHECK(hash<NodeIdentity>{}(a) != hash<NodeIdentity>{}(c));
}

TEST_CASE("node_identity: std::hash<NodeIdentity> is deterministic") {
    using std::hash;
    NodeIdentity id{GraphInstanceId{0xfeedfaceULL}, StableNodeId{0xdeadbeefULL}};
    auto h1 = hash<NodeIdentity>{}(id);
    auto h2 = hash<NodeIdentity>{}(id);
    CHECK(h1 == h2);
}

// ═════════════════════════════════════════════════════════════════════════════
// §3: PrecompInstanceKey / make_precomp_key — type-safety + std::hash
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("node_identity: make_precomp_key produces the same numeric key as the literal") {
    PrecompInstanceKey from_factory = make_precomp_key(
        GraphInstanceId{42}, StableNodeId{99});
    PrecompInstanceKey from_literal{42, 99};

    CHECK(from_factory.graph == from_literal.graph);
    CHECK(from_factory.node  == from_literal.node);
    CHECK(from_factory == from_literal);
}

TEST_CASE("node_identity: PrecompInstanceKey supports designated initializers (aggregate)") {
    // This would NOT compile if PrecompInstanceKey were not aggregate; the
    // existence of this test case is part of the contract PR 4.5 enforces.
    PrecompInstanceKey k{.graph = 17, .node = 23};
    CHECK(k.graph == 17);
    CHECK(k.node  == 23);
}

TEST_CASE("node_identity: distinct keys produce distinct std::hash results") {
    using std::hash;
    PrecompInstanceKey k1{.graph = 1, .node = 1};
    PrecompInstanceKey k2{.graph = 2, .node = 1};   // graph changed
    PrecompInstanceKey k3{.graph = 1, .node = 2};   // node changed

    std::unordered_set<PrecompInstanceKey> bucket;
    bucket.insert(k1);
    bucket.insert(k2);
    bucket.insert(k3);
    CHECK(bucket.size() == 3);
    CHECK(hash<PrecompInstanceKey>{}(k1) != hash<PrecompInstanceKey>{}(k2));
    CHECK(hash<PrecompInstanceKey>{}(k1) != hash<PrecompInstanceKey>{}(k3));
}

TEST_CASE("node_identity: PrecompInstanceKey defaults are zero (kInvalid-ish)") {
    PrecompInstanceKey k{};
    CHECK(k.graph == 0);
    CHECK(k.node  == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// §4: FrameGraphCompiler wiring — produced ids are stable across
//     re-compilation of structurally identical graphs.
// Verifies the real signature: compile(RenderGraph graph,
// RenderGraphContext& ctx, const FrameGraphCompileOptions& options = {}).
// We use CompilerTestNode (same shape as tests/render_graph/compiler/
// test_frame_graph_compiler.cpp) for end-to-end coverage.
// ═════════════════════════════════════════════════════════════════════════════

namespace {

CompiledFrameGraph compile_linear() {
    RenderGraph graph;
    GraphNodeId a = graph.add_node(std::make_unique<CompilerTestNode>("A"));
    GraphNodeId b = graph.add_node(std::make_unique<CompilerTestNode>("B"));
    graph.connect(a, b);
    graph.set_output(b);

    RenderGraphContext ctx;
    ctx.frame_input.width  = 100;
    ctx.frame_input.height = 100;

    FrameGraphCompiler compiler;
    FrameGraphCompileOptions options;
    options.run_optimizer = false;
    return compiler.compile(std::move(graph), ctx, options);
}

} // namespace

TEST_CASE("node_identity: FrameGraphCompiler populates graph_instance_id and stable_node_id") {
    auto compiled = compile_linear();

    CHECK(compiled.graph_instance_id != kInvalidGraphInstanceId);
    REQUIRE_FALSE(compiled.nodes.empty());
    CHECK(compiled.nodes[0].stable_node_id != kInvalidStableNodeId);

    // accessor must agree with field
    const auto id_0 = compiled.node_identity(GraphNodeId{0});
    CHECK(id_0.graph == compiled.graph_instance_id);
    CHECK(id_0.node  == compiled.nodes[0].stable_node_id);
}

TEST_CASE("node_identity: re-compile of an identical graph yields the SAME graph_instance_id") {
    auto compiled_a = compile_linear();
    auto compiled_b = compile_linear();

    CHECK(compiled_a.graph_instance_id                  == compiled_b.graph_instance_id);
    CHECK(compiled_a.graph_instance_id                  != kInvalidGraphInstanceId);
    CHECK(compiled_a.nodes[0].stable_node_id            == compiled_b.nodes[0].stable_node_id);
    CHECK(compiled_a.nodes[1].stable_node_id            == compiled_b.nodes[1].stable_node_id);
}

TEST_CASE("node_identity: structurally distinct graphs yield distinct graph_instance_id") {
    auto compile_triangle = []() {
        RenderGraph graph;
        GraphNodeId a = graph.add_node(std::make_unique<CompilerTestNode>("Triangle-A"));
        GraphNodeId b = graph.add_node(std::make_unique<CompilerTestNode>("Triangle-B"));
        GraphNodeId c = graph.add_node(std::make_unique<CompilerTestNode>("Triangle-C"));
        graph.connect(a, b);
        graph.connect(b, c);
        graph.set_output(c);

        RenderGraphContext ctx;
        ctx.frame_input.width  = 100;
        ctx.frame_input.height = 100;

        FrameGraphCompiler compiler;
        FrameGraphCompileOptions options;
        options.run_optimizer = false;
        return compiler.compile(std::move(graph), ctx, options);
    };

    auto linear   = compile_linear();
    auto triangle = compile_triangle();

    CHECK(linear.graph_instance_id   != triangle.graph_instance_id);
    CHECK(linear.graph_instance_id   != kInvalidGraphInstanceId);
    CHECK(triangle.graph_instance_id != kInvalidGraphInstanceId);
}

TEST_CASE("node_identity: node_identity(.) returns invalid sentinel for out-of-range ids") {
    auto compiled = compile_linear();

    // Probingly high id → out of range → defaults to invalid identity
    // (graph=kInvalidGraphInstanceId, node=kInvalidStableNodeId).
    const auto bogus = compiled.node_identity(GraphNodeId{1000});
    CHECK_FALSE(bogus.valid());
    CHECK(bogus.graph == kInvalidGraphInstanceId);
    CHECK(bogus.node  == kInvalidStableNodeId);
}
