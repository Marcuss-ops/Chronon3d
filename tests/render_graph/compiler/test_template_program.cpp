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
#include <chronon3d/render_graph/compiler/parameter_ring.hpp>
#include <chronon3d/render_graph/compiler/command_replay.hpp>
#include <chronon3d/render_graph/pipeline/frame_parameter_table.hpp>

#include <cstdint>
#include <memory>
#include <string_view>
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
    g.nodes[2].inputs = {0, 1};  // ← topological edge for propagation
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
    g.resource_table().slots.push_back(runtime::PhysicalResourceSlot{});
    g.resource_table().physical_slot_count = 1;
    g.resource_table().logical_resource_count = 3;
    g.resource_table().resources.resize(3);

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

TEST_CASE("CompiledTemplateProgram: static regions (maximal island bake) + GPU batches lift") {
    auto g = build_synthetic_compiled_graph();
    auto prog = compile_template_program(std::move(g));

    // Fase B: the synthetic graph has exactly one static node (node 0 =
    // Source Image with default FrameVariant policy → kind-derived static).
    // Node 1 (TextRun) is ContentDynamic, node 2 (Transform with a
    // ContentDynamic input) propagates to ContentDynamic.
    REQUIRE(prog.static_regions.size() == 1);
    CHECK(prog.static_regions[0].root == 0);
    CHECK(prog.static_regions[0].members == std::vector<GraphNodeId>{0});
    CHECK(prog.static_regions[0].is_baked);
    CHECK(prog.static_regions[0].fingerprint != 0);
    // bake_id is the region index (BakedResourceId resolved at runtime).
    CHECK(prog.static_regions[0].bake_id == 0);

    REQUIRE(prog.batches.size() == 2);
    CHECK(prog.batches[0].is_gpu_fused);
    CHECK(prog.batches[0].member_nodes == std::vector<GraphNodeId>{0});
    CHECK_FALSE(prog.batches[1].is_gpu_fused);
    CHECK(prog.batches[1].member_nodes == std::vector<GraphNodeId>{1, 2});

    // Boundaries are empty in Fase A (Phase G populates).
    CHECK(prog.boundaries.empty());
}

// ── Fase B: temporal classification ───────────────────────────────────────
TEST_CASE("Fase B: classify_temporal propagates worst-class through inputs") {
    auto g = build_synthetic_compiled_graph();
    auto prog = compile_template_program(std::move(g));

    REQUIRE(prog.temporal.per_node.size() == 3);
    CHECK(prog.temporal.classification(0) == TemporalClass::Static);
    CHECK(prog.temporal.classification(1) == TemporalClass::ContentDynamic);
    // node 2 (Transform) has own TransformDynamic but input node 1 is
    // ContentDynamic → propagated worst class wins.
    CHECK(prog.temporal.classification(2) == TemporalClass::ContentDynamic);
    CHECK(prog.temporal.static_count == 1);
    CHECK(prog.temporal.total_count == 3);
}

TEST_CASE("Fase B: is_static / is_dynamic / to_string helpers") {
    CHECK(is_static(TemporalClass::Pure));
    CHECK(is_static(TemporalClass::Static));
    CHECK_FALSE(is_static(TemporalClass::TransformDynamic));
    CHECK_FALSE(is_static(TemporalClass::ExternalDynamic));
    CHECK(is_dynamic(TemporalClass::ParameterDynamic));
    CHECK_FALSE(is_dynamic(TemporalClass::Pure));

    CHECK(std::string_view(to_string(TemporalClass::Static)) == "Static");
    CHECK(std::string_view(to_string(TemporalClass::ExternalDynamic)) == "ExternalDynamic");
    CHECK(std::string_view(to_string(TemporalClass::Pure)) == "Pure");
}

// ── Fase B: static island splitting at dynamic boundaries ─────────────────
TEST_CASE("Fase B: bake_maximal_static_islands splits at dynamic separators") {
    // Two independent static source nodes feeding a dynamic composite.
    CompiledFrameGraph g;
    g.valid = true;
    g.structure_hash = 0x1111;
    g.output = 2;
    g.levels = {{0, 1}, {2}};
    g.nodes.resize(3);
    g.nodes[0].id = 0;
    g.nodes[0].kind = RenderGraphNodeKind::Source;
    g.nodes[0].shape_type = 7;  // Image → static
    g.nodes[1].id = 1;
    g.nodes[1].kind = RenderGraphNodeKind::Source;
    g.nodes[1].shape_type = 7;  // Image → static
    g.nodes[2].id = 2;
    g.nodes[2].kind = RenderGraphNodeKind::Effect;
    g.nodes[2].inputs = {0, 1};  // dynamic composite
    g.program.levels = g.levels;

    auto temporal = classify_temporal(g);
    CHECK(temporal.classification(0) == TemporalClass::Static);
    CHECK(temporal.classification(1) == TemporalClass::Static);
    CHECK(temporal.classification(2) == TemporalClass::ParameterDynamic);

    auto regions = bake_maximal_static_islands(g, temporal);
    // Two disconnected static islands, one per source.
    REQUIRE(regions.size() == 2);
    CHECK(regions[0].members == std::vector<GraphNodeId>{0});
    CHECK(regions[1].members == std::vector<GraphNodeId>{1});
    CHECK(regions[0].bake_id == 0);
    CHECK(regions[1].bake_id == 1);
    CHECK(regions[0].fingerprint != regions[1].fingerprint);
}

// ── Fase D: parameter ring ────────────────────────────────────────────────
TEST_CASE("Fase D: build_parameter_ring from operations with parameter_size > 0") {
    auto g = build_synthetic_compiled_graph();
    auto ring = chronon3d::graph::build_parameter_ring(g, /*slot_count=*/3);

    CHECK(ring.slot_count == 3);
    CHECK(ring.total_bytes == 48);  // 16 (node 0) + 32 (node 1)
    REQUIRE(ring.entries.size() == 2);
    CHECK(ring.entries[0].node == 0);
    CHECK(ring.entries[0].offset == 0);
    CHECK(ring.entries[0].size == 16);
    CHECK(ring.entries[1].node == 1);
    CHECK(ring.entries[1].offset == 16);
    CHECK(ring.entries[1].size == 32);
    CHECK_FALSE(ring.empty());
    CHECK(ring.buffer_bytes() == 3u * 48u);
    CHECK(ring.entry_for(0) != nullptr);
    CHECK(ring.entry_for(99) == nullptr);
}

TEST_CASE("Fase D: ParameterRingWriter writes into correct offset") {
    auto g = build_synthetic_compiled_graph();
    auto ring = chronon3d::graph::build_parameter_ring(g, /*slot_count=*/2);

    std::vector<std::byte> buffer(ring.total_bytes, std::byte{0xFF});
    chronon3d::graph::ParameterRingWriter writer(
        std::span<std::byte>(buffer), ring);

    std::vector<std::byte> payload(16, std::byte{0xAB});
    CHECK(writer.write_entry(0, payload));
    CHECK(buffer[0] == std::byte{0xAB});
    CHECK(buffer[15] == std::byte{0xAB});
    // Node 1's offset starts at 16 — untouched by the node 0 write.
    CHECK(buffer[16] == std::byte{0xFF});

    writer.clear();
    CHECK(buffer[0] == std::byte{0x00});
    CHECK(buffer[16] == std::byte{0x00});
}

// ── Fase C: physical resource analysis ────────────────────────────────────
TEST_CASE("Fase C: analyze_physical_resources mirrors alloc plan") {
    auto g = build_synthetic_compiled_graph();
    const auto plan = chronon3d::graph::analyze_physical_resources(g);
    CHECK(plan.sized_slots.size() >= 1);
    CHECK(plan.slot_count >= 1);
    CHECK(plan.peak_transient_bytes > 0);
}

TEST_CASE("Fase C: build_device_memory_plan computes estimated peak") {
    auto g = build_synthetic_compiled_graph();
    const auto plan = chronon3d::graph::analyze_physical_resources(g);
    auto mem = chronon3d::graph::build_device_memory_plan(plan);
    CHECK(mem.physical_slots > 0);
    CHECK(mem.safety_margin > 0);
    CHECK(mem.estimated_peak > mem.physical_slots);
}

TEST_CASE("Fase C: admit_or_degrade_job rejects over-budget job") {
    auto g = build_synthetic_compiled_graph();
    const auto plan = chronon3d::graph::analyze_physical_resources(g);
    auto mem = chronon3d::graph::build_device_memory_plan(plan);

    auto admitted = chronon3d::graph::admit_or_degrade_job(
        mem, /*available_vram=*/mem.estimated_peak * 2);
    CHECK(admitted.verdict == chronon3d::graph::AdmissionVerdict::Admitted);
    CHECK(admitted.ok());

    auto rejected = chronon3d::graph::admit_or_degrade_job(
        mem, /*available_vram=*/0);
    CHECK(rejected.verdict == chronon3d::graph::AdmissionVerdict::Rejected);
    CHECK_FALSE(rejected.ok());
}

// ── Fase E: command replay bridge ──────────────────────────────────────────
TEST_CASE("Fase E: CommandReplayDescriptor allocates slots") {
    auto g = build_synthetic_compiled_graph();
    auto ring = chronon3d::graph::build_parameter_ring(g, 3);
    auto replay = chronon3d::graph::build_command_replay(ring, 3);

    CHECK(replay.valid());
    CHECK(replay.slot_count == 3);
    CHECK(replay.total_param_bytes == 3u * ring.total_bytes);

    auto slots = replay.allocate_slots();
    CHECK(slots.size() == 3);
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(slots[i].ready());
        CHECK(slots[i].slot_index == i);
        CHECK(slots[i].param_buffer.size() == ring.total_bytes);
    }
}

TEST_CASE("Fase E: write_frame invokes per-op writer") {
    auto g = build_synthetic_compiled_graph();
    auto ring = chronon3d::graph::build_parameter_ring(g, 2);
    auto replay = chronon3d::graph::build_command_replay(ring, 2);
    auto slots = replay.allocate_slots();

    std::size_t write_count = 0;
    CHECK(replay.write_frame(
        std::span<chronon3d::graph::CommandSlot>(slots), 0,
        [&](std::size_t, const chronon3d::graph::ParameterRingDescriptor::Entry&,
            chronon3d::graph::ParameterRingWriter&) {
            ++write_count;
        }));
    CHECK(write_count == 2);  // two param entries
}

TEST_CASE("Fase D: compile_template_program populates param_ring") {
    auto g = build_synthetic_compiled_graph();
    auto prog = compile_template_program(std::move(g));

    CHECK(prog.param_ring.slot_count == 3);
    CHECK(prog.param_ring.total_bytes == 48);
    CHECK(prog.param_ring.entries.size() == 2);
}

TEST_CASE("Fase B: merge_contiguous_static_regions is deterministic no-op in Fase B") {
    auto g = build_synthetic_compiled_graph();
    auto temporal = classify_temporal(g);
    auto regions = bake_maximal_static_islands(g, temporal);
    auto merged = merge_contiguous_static_regions(regions, g, temporal);

    REQUIRE(merged.size() == regions.size());
    for (std::size_t i = 0; i < regions.size(); ++i) {
        CHECK(merged[i].members == regions[i].members);
        CHECK(merged[i].bake_id == regions[i].bake_id);
    }
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
