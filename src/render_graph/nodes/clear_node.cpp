// ClearNode policy dispatch and public node entrypoint.
#include "clear_node_execution.hpp"

namespace chronon3d::graph {

NodeExecResult ClearNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef> inputs,
    std::span<const std::optional<raster::BBox>> bboxes) {
    return detail::execute_clear_node(ctx, inputs, bboxes);
}

} // namespace chronon3d::graph
