#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/scene/shape.hpp>
#include <chronon3d/math/mat4.hpp>
#include <chronon3d/scene/render_node.hpp>

namespace chronon3d {
namespace renderer {

void draw_glass_panel(Framebuffer& fb, const Framebuffer& src, const Shape& shape, const Mat4& model, f32 opacity, const RenderState* state);

} // namespace renderer
} // namespace chronon3d
