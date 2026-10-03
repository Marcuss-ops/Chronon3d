#pragma once

#include <chronon3d/render_graph/nodes/composite_node.hpp>

namespace chronon3d::graph::detail {

[[nodiscard]] NodeExecResult execute_backdrop_software(
    RenderGraphContext& ctx, const Framebuffer& bottom, const Framebuffer& top,
    const BackdropBlurParams& settings, BlendMode mode);

} // namespace chronon3d::graph::detail
