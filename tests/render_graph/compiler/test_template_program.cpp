// tests/render_graph/compiler/test_template_program.cpp
// ════════════════════════════════════════════════════════════════════════════
// Fase A (TICKET-VIDEO-COMPILER-ARCH-V1) — unit tests for the
// CompiledTemplateProgram ABI + derivation from a CompiledFrameGraph.
//
// TIER=UNIT, no Blend2D / Text / GPU / FontEngine dependency. The test
// exercises the ABI surface directly with a synthetic CompiledFrameGraph
// (hand-assembled, no real graph build) so the lift logic is verified in
// isolation; the end-to-end compile path on a real composition is covered
// by the integration suites (frame-graph compiler) and forward-pointed to
// WBH macchina-verifica.
// ════════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/render_graph/compiler/compiled_template_program.hpp>
#include <chronon3d/render_graph/core/node_identity.hpp>
#include <chronon3d/render_graph/pipeline/frame_parameter_table.hpp>

#include <cstdint>
#include <memory>
#include <vector>

using namespace chronon3d::graph;

namespace {

/// Hand-assembled CompiledFrameGraph exercising every lift surface:
/// two operations with parameter blocks, two layer batches, one static
/// bake, one active binding (text), and a prepared parameter table.
CompiledFrameGraph build_synthetic_compiled_graph() {
    CompiledFrameGraph g;
    g.valid = true;
    g.structure_hash = 0xDEADBEEF12345678ULL;
    g.output = 1;

    g.nodes.resize(3);
    g.nodes[0].id = 0;
    g.nodes[0].kind = RenderGraphNodeKind::Source;
    g.nodes[0].layer_id = "layer_image";
    g.nodes[0].shape_type = 7;  // ShapeType::Image
    g.nodes[0].binding_meta.active = true;

    g.nodes[1].id = 1;
    g.nodes[1].kind = RenderGraphNodeKind::TextRun;
    g.nodes[1].layer_id = "layer_text";
    g.nodes[1].binding_meta.active = true;

    g.nodes[2].id = 2;
    g.nodes[2].kind = RenderGraphNodeKind::Transform;
    g.nodes[2].layer_id = "layer_xform";
    g.nodes[2].binding_meta.active = false;

    // ── Program: levels + operations + batches + static bakes ─────────
    g.levels = {{0, 1}, {2}};
    g.program.levels = g.levels;
    g.program.fully_recorded = true;

    g.program.operations.push_back(CompiledOperation{
        .node = 0, .stable_node = StableNodeId{10}, .inputs = {},
        .output_physical_slot = 0, .parameter_offset = 0, .parameter_size = 16,
    });
    g.program.operations.push_back(CompiledOperation{
        .node = 1, .stable_node = StableNodeId{20}, .inputs = {},
        .output_physical_slot = 1, .parameter_offset = 16, .parameter_size = 32,
    });
    g.program.operations.push_back(CompiledOperation{
        .node = 2, .stable_node = StableNodeId{30}, .inputs = {0, 1},
        .output_physical_slot = 2, .parameter_offset = 0, .parameter_size = 0,  // no params
    });

    g.program.layer_batches.push_back(CompiledLayerBatch{
        .member_nodes = {0}, .output_physical_slot = 0, .is_gpu_fused = true,
    });
    g.program.layer_batches.push_back(CompiledLayerBatch{
        .member_nodes = {1, 2}, .output_physical_slot = 2, .is_gpu_fused = false,
    });

    g.program.static_bakes.push_back(StaticSubgraphBakePass{
        .root_node = 0, .static_fingerprint = 0xABCD, .is_baked = true,
        .persistent_surface_handle = 17,
    });

    // ── Prepared parameters ────────────────────────────────────────────
    auto table = std::make_shared<const FrameParameterTable>();
    const_cast<FrameParameterTable&>(*table).warm_up(3, 64);
    g.prepared_parameters = table;

    // ── Physical plan ──────────────────────────────────────────────────
    g.physical_framebuffer_plan.slots.push_back(FramebufferSlot{0, 1920, 1080});
    g.physical_framebuffer_plan.physical_slot_count = 1;
    g.physical_framebuffer_plan.logical_resource_count = 3;
    g.physical_framebuffer_plan.resources.resize(3);

    return g;
}

} // namespace

TEST_CASE("CompiledTemplateProgram: ABI surface default-constructs to empty") {
    CompiledTemplateProgram prog;
    CHECK(prog.empty());
    CHECK(prog.compiled == nullptr);
    CHECK_FALSE(prog.fingerprint.valid());
    CHECK(prog.parameters.empty());
    CHECK(prog.resources.empty());
    CHECK(prog.static_regions.empty());
    CHECK(prog.batches.empty());
    CHECK(prog.boundaries.empty());
    CHECK(prog.resource_plan() == nullptr);
    CHECK(prog.execution() == nullptr);
    CHECK_FALSE(prog.valid);
}

TEST_CASE("CompiledTemplateProgram: ProgramFingerprint equality + hash") {
    ProgramFingerprint a{0x1, kRenderAbiV1, kQualityProfileDefault};
    ProgramFingerprint b{0x1, kRenderAbiV1, kQualityProfileDefault};
    ProgramFingerprint c{0x2, kRenderAbiV1, kQualityProfileDefault};

    CHECK(a == b);
    CHECK(a != c);
    CHECK(a.valid());
    CHECK_FALSE(ProgramFingerprint{}.valid());

    CHECK(std::hash<ProgramFingerprint>{}(a) == std::hash<ProgramFingerprint>{}(b));
    CHECK(std::hash<ProgramFingerprint>{}(a) != std::hash<ProgramFingerprint>{}(c));
}

TEST_CASE("CompiledTemplateProgram: lift populates fingerprint from structure_hash") {
    auto g = build_synthetic_compiled_graph();
    auto prog = compile_template_program(std::move(g));

    CHECK(prog.valid);
    CHECK_FALSE(prog.empty());
    CHECK(prog.compiled != nullptr);
    CHECK(prog.fingerprint.topology_hash == 0xDEADBEEF12345678ULL);
    CHECK(prog.fingerprint.renderer_abi == kRenderAbiV1);
    CHECK(prog.fingerprint.valid());

    // Accessors delegate to the owned compiled graph — no copies.
    REQUIRE(prog.resource_plan() != nullptr);
    CHECK(prog.resource_plan()->physical_slot_count == 1);
    REQUIRE(prog.execution() != nullptr);
    CHECK(prog.execution()->operations.size() == 3);
    CHECK(prog.execution()->fully_recorded);
}

TEST_CASE("CompiledTemplateProgram: parameter schema lifts offsets/sizes") {
    auto g = build_synthetic_compiled_graph();
    auto prog = compile_template_program(std::move(g));

    REQUIRE(prog.parameters.entries.size() == 2);  // node 2 has size 0 → skipped
    CHECK(prog.parameters.entries[0].node == 0);
    CHECK(prog.parameters.entries[0].parameter_offset == 0);
    CHECK(prog.parameters.entries[0].parameter_size == 16);
    CHECK(prog.parameters.entries[1].node == 1);
    CHECK(prog.parameters.entries[1].parameter_offset == 16);
    CHECK(prog.parameters.entries[1].parameter_size == 32);
    CHECK(prog.parameters.total_bytes == 48);
    CHECK(prog.parameters.has_prepared_parameters);
    CHECK(prog.parameters.frame_count == 3);
    CHECK(prog.parameters.fully_recorded);
}

TEST_CASE("CompiledTemplateProgram: resource manifest classifies kinds") {
    auto g = build_synthetic_compiled_graph();
    auto prog = compile_template_program(std::move(g));

    REQUIRE(prog.resources.entries.size() == 2);  // only active bindings
    CHECK(prog.resources.entries[0].kind == ResourceKind::Image);
    CHECK(prog.resources.entries[0].binding_id == "layer_image");
    CHECK(prog.resources.entries[1].kind == ResourceKind::Text);
    CHECK(prog.resources.entries[1].binding_id == "layer_text");
}

TEST_CASE("CompiledTemplateProgram: static regions + GPU batches lift") {
    auto g = build_synthetic_compiled_graph();
    auto prog = compile_template_program(std::move(g));

    REQUIRE(prog.static_regions.size() == 1);
    CHECK(prog.static_regions[0].bake_id == 17);
    CHECK(prog.static_regions[0].root == 0);
    CHECK(prog.static_regions[0].fingerprint == 0xABCD);
    CHECK(prog.static_regions[0].is_baked);

    REQUIRE(prog.batches.size() == 2);
    CHECK(prog.batches[0].is_gpu_fused);
    CHECK(prog.batches[0].member_nodes == std::vector<GraphNodeId>{0});
    CHECK_FALSE(prog.batches[1].is_gpu_fused);
    CHECK(prog.batches[1].member_nodes == std::vector<GraphNodeId>{1, 2});

    // Boundaries are empty in Fase A (Phase G populates).
    CHECK(prog.boundaries.empty());
}

TEST_CASE("CompiledTemplateProgram: consuming the source graph is a move") {
    auto g = build_synthetic_compiled_graph();
    auto* original_address = &g;
    auto prog = compile_template_program(std::move(g));

    // The lift moved `g` into the template; the shared_ptr owns it.
    CHECK(prog.compiled.get() != nullptr);
    CHECK(prog.compiled.get() != original_address);
    CHECK(prog.compiled->output == 1);
    CHECK(prog.compiled->valid);
}
