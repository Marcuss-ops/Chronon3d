// tests/runtime/test_gpu_layer_batch.cpp
// ════════════════════════════════════════════════════════════════════════════
// Fase F (TICKET-VIDEO-COMPILER-ARCH-V1) — GpuLayerBatch unit tests.
// TIER=UNIT, no GPU/backend dependency.  Tests the IR surface.
// ════════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>
#include <chronon3d/runtime/gpu_layer_batch.hpp>

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
                                             0,0,1,1, 10,10,200,30, BlendMode::Normal});
    CHECK(batch.size() == 2);
    CHECK(batch.instances[1].kind == PrimitiveKind::Text);
    CHECK(batch.instances[1].dst_x0 == 10.0f);
    CHECK(batch.instances[1].dst_y1 == 30.0f);
}

TEST_CASE("GpuLayerBatch: make_gpu_batch from CompiledLayerBatch") {
    using namespace chronon3d::graph;
    CompiledLayerBatch clb;
    clb.output_physical_slot = 7;
    clb.is_gpu_fused = true;
    clb.member_nodes = {42, 43};

    auto gpu = make_gpu_batch(clb);
    CHECK(gpu.output_physical_slot == 7);
    CHECK(gpu.is_gpu_fused);
    CHECK(gpu.empty());  // no instances — only metadata lifted
}