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
    batch.instances.reserve(compiled_batch.instances.size());
    for (const auto& compiled : compiled_batch.instances) {
        LayerInstance instance;
        instance.resource_index = compiled.resource_index;
        instance.transform_index = compiled.transform_index;
        instance.paint_index = compiled.paint_index;
        instance.opacity = compiled.opacity;
        instance.src_x0 = 0.0f;
        instance.src_y0 = 0.0f;
        instance.src_x1 = 1.0f;
        instance.src_y1 = 1.0f;
        instance.dst_x0 = static_cast<float>(compiled.dst_bounds.x0);
        instance.dst_y0 = static_cast<float>(compiled.dst_bounds.y0);
        instance.dst_x1 = static_cast<float>(compiled.dst_bounds.x1);
        instance.dst_y1 = static_cast<float>(compiled.dst_bounds.y1);
        batch.instances.push_back(instance);
    }
    return batch;
}

} // namespace chronon3d::runtime
