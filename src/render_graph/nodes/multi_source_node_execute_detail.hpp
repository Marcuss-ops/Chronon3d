#pragma once

#include <chronon3d/render_graph/nodes/multi_source_node.hpp>

namespace chronon3d::graph::detail {

[[nodiscard]] NodeExecResult execute_multi_source_items(
    RenderGraphContext& ctx,
    const std::string& node_name,
    const std::vector<MultiSourceItem>& items,
    OwnedFB framebuffer,
    bool native_group_surfaces);

} // namespace chronon3d::graph::detail
