#pragma once

#include <chronon3d/render_graph/nodes/clear_node.hpp>

namespace chronon3d::graph::detail {

NodeExecResult execute_clear_node(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef> inputs,
    std::span<const std::optional<raster::BBox>> bboxes);

} // namespace chronon3d::graph::detail

