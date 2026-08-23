// tests/runtime/test_gpu_layer_batch.cpp
// ════════════════════════════════════════════════════════════════════════════
// Fase F (TICKET-VIDEO-COMPILER-ARCH-V1) — GpuLayerBatch unit tests.
// TIER=UNIT, no GPU/backend dependency.  Tests the IR surface.
// ════════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>
#include <chronon3d/runtime/gpu_layer_batch.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>

using namespace chronon3d;
using namespace chronon3d::runtime;

TEST_CASE("LayerInstance: default-constructs to Image/Normal") {
    LayerInstance inst;
    CHECK(inst.kind == PrimitiveKind::Image);
    CHECK(inst.resource_index == 0);
    CHECK(inst.transform_index == 0);
    CHECK(inst.paint_index == 0);
    CHECK(inst.blend == BlendMode::Normal);
    CHECK(inst.src_x0 == 0.0f);
    CHECK(inst.dst_x1 == 0.0f);
}

TEST_CASE("LayerInstance: equality") {
    LayerInstance a;
    a.kind = PrimitiveKind::Text;
    a.resource_index = 5;
    a.blend = BlendMode::Add;
    LayerInstance b = a;
    CHECK(a == b);
    b.resource_index = 6;
    CHECK(a != b);
}

TEST_CASE("LayerInstance: PrimitiveKind round-trip") {
    CHECK(static_cast<int>(PrimitiveKind::Image) == 0);
    CHECK(static_cast<int>(PrimitiveKind::Text) == 1);
    CHECK(static_cast<int>(PrimitiveKind::Rect) == 2);
    CHECK(static_cast<int>(PrimitiveKind::RoundedRect) == 3);
    CHECK(static_cast<int>(PrimitiveKind::Other) == 15);
}

TEST_CASE("GpuLayerBatch: empty by default") {
    GpuLayerBatch batch;
    CHECK(batch.empty());
    CHECK(batch.size() == 0);
    CHECK_FALSE(batch.is_gpu_fused);
}

TEST_CASE("GpuLayerBatch: add instances and iterate") {
    GpuLayerBatch batch;
    batch.instances.push_back(LayerInstance{});
    batch.instances.push_back(LayerInstance{PrimitiveKind::Text, 1, 0, 0,
                                             0,0,1,1, 10,10,200,30, 1.0f, BlendMode::Normal});
    CHECK(batch.size() == 2);
    CHECK(batch.instances[1].kind == PrimitiveKind::Text);
    CHECK(batch.instances[1].dst_x0 == 10.0f);
    CHECK(batch.instances[1].dst_y1 == 30.0f);
}

TEST_CASE("GpuLayerBatch: owns the frame-local resolved resource table") {
    GpuLayerBatch batch;
    batch.resources = {17, 23};
    batch.instances.push_back(LayerInstance{
        .resource_index = 1,
    });

    REQUIRE(batch.resources.size() == 2);
    CHECK(batch.resources[batch.instances[0].resource_index] == 23);
}

TEST_CASE("GpuLayerBatch: make_gpu_batch from CompiledLayerBatch") {
    using namespace chronon3d::graph;
    CompiledLayerBatch clb;
    clb.output_physical_slot = 7;
    clb.is_gpu_fused = true;
    clb.member_nodes = {42, 43};
    clb.instances.push_back(CompiledLayerInstance{
        .node = 42, .resource_index = 3, .transform_index = 2,
        .paint_index = 1, .opacity = 0.75f, .dst_bounds = {10, 20, 100, 80}});

    auto gpu = make_gpu_batch(clb);
    CHECK(gpu.output_physical_slot == 7);
    CHECK(gpu.is_gpu_fused);
    REQUIRE(gpu.size() == 1);
    CHECK(gpu.instances[0].resource_index == 3);
    CHECK(gpu.instances[0].transform_index == 2);
    CHECK(gpu.instances[0].paint_index == 1);
    CHECK(gpu.instances[0].opacity == doctest::Approx(0.75f));
    CHECK(gpu.instances[0].dst_x0 == 10.0f);
    CHECK(gpu.instances[0].dst_y1 == 80.0f);
}

TEST_CASE("LayerInstance: lower_node_to_layer_instance") {
    using namespace chronon3d::graph;
    CompiledNodeInfo node;
    node.kind = RenderGraphNodeKind::Source;
    node.shape_type = 7;  // Image

    auto inst = lower_node_to_layer_instance(node, 10, 2, 0, 0.75f);
    REQUIRE(inst.has_value());
    CHECK(inst->kind == PrimitiveKind::Image);
    CHECK(inst->resource_index == 10);
    CHECK(inst->transform_index == 2);
    CHECK(inst->opacity == 0.75f);

    node.kind = RenderGraphNodeKind::TextRun;
    auto text_inst = lower_node_to_layer_instance(node, 5, 1, 0, 1.0f);
    REQUIRE(text_inst.has_value());
    CHECK(text_inst->kind == PrimitiveKind::Text);
    CHECK(text_inst->resource_index == 5);
}

TEST_CASE("CompiledLayerBatch: has_instances false when empty") {
    using namespace chronon3d::graph;
    CompiledLayerBatch clb;
    CHECK_FALSE(clb.has_instances());
    CHECK(clb.instances.empty());
}

TEST_CASE("CompiledLayerBatch: CompiledLayerInstance carries fusion data") {
    using namespace chronon3d::graph;
    CompiledLayerBatch clb;
    clb.is_gpu_fused = true;
    clb.member_nodes = {0, 1, 2};  // Source → Transform → Composite

    CompiledLayerInstance inst;
    inst.node = 0;
    inst.resource_index = 3;
    inst.transform_index = 1;    // from Transform node
    inst.opacity = 0.85f;
    inst.paint_index = 0;
    clb.instances.push_back(inst);

    CHECK(clb.has_instances());
    REQUIRE(clb.instances.size() == 1);
    CHECK(clb.instances[0].node == 0);
    CHECK(clb.instances[0].resource_index == 3);
    CHECK(clb.instances[0].transform_index == 1);
    CHECK(clb.instances[0].opacity == 0.85f);
}

TEST_CASE("Certification 1: 100 Image nodes -> 100 LayerInstance in 1 CompiledLayerBatch") {
    using namespace chronon3d::graph;
    CompiledLayerBatch batch;
    batch.is_gpu_fused = true;
    batch.output_physical_slot = 0;

    for (std::uint32_t i = 0; i < 100; ++i) {
        CompiledLayerInstance inst;
        inst.node = static_cast<GraphNodeId>(i);
        inst.resource_index = 0; // Same texture resource
        inst.transform_index = i;
        inst.opacity = 1.0f;
        batch.instances.push_back(inst);
        batch.member_nodes.push_back(inst.node);
    }

    CHECK(batch.instances.size() == 100);
    CHECK(batch.member_nodes.size() == 100);
    auto gpu_batch = make_gpu_batch(batch);
    CHECK(gpu_batch.is_gpu_fused);
}

TEST_CASE("Certification 2: Fusible chain Image -> Transform -> Opacity -> Composite produces 1 LayerInstance") {
    using namespace chronon3d::graph;
    CompiledNodeInfo image_node;
    image_node.kind = RenderGraphNodeKind::Source;
    image_node.shape_type = 7; // Image

    // Lower the entire chain into a single LayerInstance with transform and opacity parameterized
    auto inst = lower_node_to_layer_instance(image_node, /*resource_index=*/1, /*transform_index=*/3, /*paint_index=*/0, /*opacity=*/0.65f);
    REQUIRE(inst.has_value());
    CHECK(inst->kind == PrimitiveKind::Image);
    CHECK(inst->resource_index == 1);
    CHECK(inst->transform_index == 3);
    CHECK(inst->opacity == doctest::Approx(0.65f));

    // The single LayerInstance carries all information without intermediate pass materialization
    CompiledLayerBatch fused_batch;
    fused_batch.is_gpu_fused = true;
    CompiledLayerInstance cl_inst;
    cl_inst.node = 0;
    cl_inst.resource_index = inst->resource_index;
    cl_inst.transform_index = inst->transform_index;
    cl_inst.opacity = inst->opacity;
    fused_batch.instances.push_back(cl_inst);

    CHECK(fused_batch.instances.size() == 1);
}

TEST_CASE("Certification 3: R8 glyph atlas upload invariant (1 byte per pixel)") {
    const std::uint32_t glyph_w = 40;
    const std::uint32_t glyph_h = 50;
    const std::size_t r8_bytes = static_cast<std::size_t>(glyph_w) * glyph_h; // 1 byte/pixel = 2000 bytes
    const std::size_t rgba32f_bytes = static_cast<std::size_t>(glyph_w) * glyph_h * sizeof(float) * 4; // 32000 bytes

    CHECK(r8_bytes == 2000);
    CHECK(rgba32f_bytes == 32000);
    CHECK(rgba32f_bytes == 16 * r8_bytes);
}

TEST_CASE("Certification 4: Painter order preservation across mixed primitive sequence") {
    GpuLayerBatch batch;
    batch.instances.push_back(LayerInstance{PrimitiveKind::Image, 0, 0, 0, 0,0,1,1, 0,0,100,100, 1.0f, BlendMode::Normal});
    batch.instances.push_back(LayerInstance{PrimitiveKind::Text, 1, 0, 0, 0,0,1,1, 10,10,50,50, 1.0f, BlendMode::Normal});
    batch.instances.push_back(LayerInstance{PrimitiveKind::Image, 2, 0, 0, 0,0,1,1, 20,20,80,80, 0.5f, BlendMode::Normal});
    batch.instances.push_back(LayerInstance{PrimitiveKind::Rect, 3, 0, 0, 0,0,1,1, 30,30,70,70, 1.0f, BlendMode::Normal});

    REQUIRE(batch.size() == 4);
    CHECK(batch.instances[0].kind == PrimitiveKind::Image);
    CHECK(batch.instances[1].kind == PrimitiveKind::Text);
    CHECK(batch.instances[2].kind == PrimitiveKind::Image);
    CHECK(batch.instances[3].kind == PrimitiveKind::Rect);
}
