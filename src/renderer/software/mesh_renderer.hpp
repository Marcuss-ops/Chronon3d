#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/math/mat4.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/scene/render_node.hpp>

namespace chronon3d {
namespace renderer {

void render_mesh_wireframe(Framebuffer& fb, const Mesh& mesh, const Mat4& model,
                           const Mat4& view, const Mat4& proj, const Color& color);

void render_mesh_filled(Framebuffer& fb, const Mesh& mesh, const Mat4& model,
                        const Mat4& view, const Mat4& proj, const Color& color);

} // namespace renderer
} // namespace chronon3d
