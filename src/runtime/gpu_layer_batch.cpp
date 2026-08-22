#include <chronon3d/runtime/gpu_layer_batch.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>

namespace chronon3d::runtime {

std::optional<LayerInstance> lower_node_to_layer_instance(
    const graph::CompiledNodeInfo& node,
    std::uint32_t resource_index,
    std::uint32_t transform_index,
    std::uint32_t paint_index,
    float opacity,
    BlendMode blend) {
    LayerInstance inst;
    inst.resource_index = resource_index;
    inst.transform_index = transform_index;
    inst.paint_index = paint_index;
    inst.opacity = opacity;
    inst.blend = blend;
    switch (node.kind) {
        case graph::RenderGraphNodeKind::Source:
            if (node.shape_type == 7) {  // Image
                inst.kind = PrimitiveKind::Image;
                return inst;
            }
            inst.kind = PrimitiveKind::Rect;
            return inst;
        case graph::RenderGraphNodeKind::TextRun:
            inst.kind = PrimitiveKind::Text;
            return inst;
        case graph::RenderGraphNodeKind::Video:
            inst.kind = PrimitiveKind::Image;
            return inst;
        default:
            break;
    }
    return std::nullopt;
}

GpuLayerBatch make_gpu_batch(
    const graph::CompiledLayerBatch& compiled_batch) {
    GpuLayerBatch batch;
    batch.output_physical_slot = compiled_batch.output_physical_slot;
    batch.is_gpu_fused         = compiled_batch.is_gpu_fused;
    return batch;
}

} // namespace chronon3d::runtime
