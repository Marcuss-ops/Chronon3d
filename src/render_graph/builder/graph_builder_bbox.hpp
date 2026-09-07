#pragma once

#include <chronon3d/scene/model/layer/layer.hpp>
#include "graph_builder_coordinates.hpp"

namespace chronon3d {
class SoftwareRenderer;
namespace renderer {
class ProcessorRegistrySnapshot;
}
} // namespace chronon3d

namespace chronon3d::graph::detail {

bool is_native_3d_layer(const Layer& layer);

raster::BBox compute_layer_bbox(const LayerGraphItem& item, const RenderGraphContext& ctx, SoftwareRenderer* renderer);

/// Compute a layer bbox reusing an already-captured processor-registry
/// snapshot.  Per-frame dirty analysis calls this once per active layer with
/// a snapshot captured once for the whole frame, instead of rebuilding the
/// (sorted, shared-owned) snapshot for every visible node of every layer.
/// The caller must guarantee the snapshot outlives the call and reflects the
/// same registry generation the frame is being analyzed against.
raster::BBox compute_layer_bbox(
    const LayerGraphItem& item,
    const RenderGraphContext& ctx,
    const renderer::ProcessorRegistrySnapshot& processor_snapshot);

} // namespace chronon3d::graph::detail
