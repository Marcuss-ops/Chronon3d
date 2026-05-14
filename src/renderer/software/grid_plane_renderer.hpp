#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/scene/render_node.hpp>
#include <chronon3d/scene/shape.hpp>

namespace chronon3d {
namespace renderer {

void draw_grid_plane(Framebuffer& fb, const RenderNode& node, const RenderState& state, const GridPlaneShape& shape);

} // namespace renderer
} // namespace chronon3d
