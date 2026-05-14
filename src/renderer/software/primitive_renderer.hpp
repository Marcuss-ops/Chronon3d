#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/scene/shape.hpp>
#include <chronon3d/scene/render_node.hpp>
#include <chronon3d/math/mat4.hpp>
#include <chronon3d/math/vec2.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/scene/camera.hpp>

namespace chronon3d {
namespace renderer {

void fill_convex_quad(Framebuffer& fb, const Vec2 v[4], const Color& color);
// Gouraud-shaded quad: colors[i] maps to v[i], bilinear interpolated across the quad.
void fill_gradient_quad(Framebuffer& fb, const Vec2 v[4], const Color colors[4]);

void fill_triangle(Framebuffer& fb, const Vec2 v[3], const Color& color);
void fill_gradient_triangle(Framebuffer& fb, const Vec2 v[3], const Color colors[3]);

void bline(Framebuffer& fb, Vec2 p0, Vec2 p1, const Color& color);

void draw_fake_box3d(Framebuffer& fb, const RenderNode& node, const RenderState& state, const FakeBox3DShape& shape);
void draw_grid_plane(Framebuffer& fb, const RenderNode& node, const RenderState& state, const GridPlaneShape& shape);

void render_mesh_wireframe(Framebuffer& fb, const Mesh& mesh, const Mat4& model,
                           const Mat4& view, const Mat4& proj, const Color& color);

void render_mesh_filled(Framebuffer& fb, const Mesh& mesh, const Mat4& model,
                        const Mat4& view, const Mat4& proj, const Color& color);

void draw_transformed_shape(Framebuffer& fb, const Shape& shape, const Mat4& model, const Color& color,
                             f32 spread = 0.0f, const RenderState* state = nullptr);

void draw_glass_panel(Framebuffer& fb, const Framebuffer& src, const Shape& shape, const Mat4& model, f32 opacity, const RenderState* state);

std::unique_ptr<Framebuffer> downsample_fb(const Framebuffer& src, i32 dst_w, i32 dst_h);


raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread = 0.0f);
bool hit_test(const Shape& s, Vec2 p, f32 spread = 0.0f);
bool pixel_passes_mask(const RenderState& state, i32 x, i32 y);

Vec2 project_2_5d(const Vec3& wp, const Mat4& view, f32 focal, f32 vp_cx, f32 vp_cy, bool& ok);

bool clip_and_project_line(const Vec3& w0, const Vec3& w1,
                           const Mat4& view, f32 focal, f32 vp_cx, f32 vp_cy,
                           Vec2& p0, Vec2& p1);

} // namespace renderer
} // namespace chronon3d
