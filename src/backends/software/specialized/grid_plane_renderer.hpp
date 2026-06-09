#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/scene/model/render/render_runtime.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>

namespace chronon3d {
namespace renderer {

void draw_grid_plane(Framebuffer& fb, const RenderNode& node, const RenderState& state, const GridPlaneShape& shape);
void draw_grid_plane(Framebuffer& fb, const RenderNode& node, const RenderState& state,
                     const GridPlaneShape& shape, const GridPlaneRenderState& runtime);

} // namespace renderer
} // namespace chronon3d
