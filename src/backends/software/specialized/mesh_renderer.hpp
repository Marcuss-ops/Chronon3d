#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/rendering/lighting_rig.hpp>
#include <optional>
#include <span>

namespace chronon3d {
namespace assets { struct MeshImage; }
namespace renderer {

/// Closed-volume contract for the software rasterizer. A strongly typed enum
/// keeps a float alpha-cutoff argument from silently binding to a bool flag
/// through the implicit float -> bool conversion.
enum class BackfaceCulling { Enabled, Disabled };

void render_mesh_wireframe(Framebuffer& fb, const Mesh& mesh, const Mat4& model,
                           const Mat4& view, const Mat4& proj, const Color& color);

// `culling` applies the closed-volume contract: a triangle whose winding
// faces away from the camera is skipped. It defaults to enabled so translucent
// solids do not composite their front and back surfaces into a denser colour
// (0.5 + 0.5 * 0.5 = 0.75 for a 50% box). `double_sided` materials opt out at
// the call site.
void render_mesh_filled(Framebuffer& fb, const Mesh& mesh, const Mat4& model,
                        const Mat4& view, const Mat4& proj, const Color& color,
                        std::span<float> depth_buffer,
                        f32 near_plane = 0.1f,
                        f32 far_plane = 1000.0f,
                        BackfaceCulling culling = BackfaceCulling::Enabled);

void render_mesh_textured(Framebuffer& fb, const Mesh& mesh, const Mat4& model,
                         const Mat4& view, const Mat4& proj, const Color& tint,
                         std::span<float> depth_buffer, const Framebuffer& texture,
                         f32 near_plane = 0.1f, f32 far_plane = 1000.0f,
                         BackfaceCulling culling = BackfaceCulling::Enabled);

void render_mesh_textured(Framebuffer& fb, const Mesh& mesh, const Mat4& model,
                         const Mat4& view, const Mat4& proj, const Color& tint,
                         std::span<float> depth_buffer, const Framebuffer& texture,
                         f32 near_plane, f32 far_plane,
                         std::optional<f32> alpha_cutoff,
                         BackfaceCulling culling = BackfaceCulling::Enabled);

void render_mesh_filled_lit(Framebuffer& fb, const Mesh& mesh, const Mat4& model,
                            const Mat4& view, const Mat4& proj, const Color& color,
                            std::span<float> depth_buffer,
                            const rendering::LightContext& light,
                            const rendering::RimLight& rim,
                            const RenderNode::InlineMeshMaterial& material,
                            f32 near_plane = 0.1f,
                            f32 far_plane = 1000.0f,
                            f32 roughness_factor = -1.0f,
                            f32 metallic_factor = 0.0f,
                            const assets::MeshImage* metallic_roughness = nullptr,
                            const Framebuffer* base_color_texture = nullptr);

/// Compatibility overload that owns a temporary depth buffer for one mesh.
void render_mesh_filled(Framebuffer& fb, const Mesh& mesh, const Mat4& model,
                        const Mat4& view, const Mat4& proj, const Color& color,
                        f32 near_plane = 0.1f,
                        f32 far_plane = 1000.0f,
                        BackfaceCulling culling = BackfaceCulling::Enabled);

} // namespace renderer
} // namespace chronon3d
