#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <span>

namespace chronon3d {
namespace renderer {

void render_mesh_wireframe(Framebuffer& fb, const Mesh& mesh, const Mat4& model,
                           const Mat4& view, const Mat4& proj, const Color& color);

void render_mesh_filled(Framebuffer& fb, const Mesh& mesh, const Mat4& model,
                        const Mat4& view, const Mat4& proj, const Color& color,
                        std::span<float> depth_buffer);

/// Compatibility overload that owns a temporary depth buffer for one mesh.
void render_mesh_filled(Framebuffer& fb, const Mesh& mesh, const Mat4& model,
                        const Mat4& view, const Mat4& proj, const Color& color);

} // namespace renderer
} // namespace chronon3d
