#pragma once

#include <chronon3d/render_graph/nodes/composite_node.hpp>

namespace chronon3d::graph::detail {

[[nodiscard]] NodeExecResult execute_backdrop_software(
    RenderGraphContext& ctx, const Framebuffer& bottom, const Framebuffer& top,
    const BackdropBlurParams& settings, BlendMode mode);
void publish_dof_provenance(
    RenderGraphContext& ctx, const Framebuffer& surface, u64 composite_id,
    float camera_depth, float source_radius,
    const std::optional<raster::BBox>& clip,
    i32 framebuffer_width, i32 framebuffer_height);
[[nodiscard]] float dof_source_radius(
    RenderGraphContext& ctx, float camera_depth);

} // namespace chronon3d::graph::detail
