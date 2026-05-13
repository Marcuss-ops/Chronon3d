#pragma once

#include <chronon3d/renderer/framebuffer.hpp>
#include <chronon3d/scene/shape.hpp>
#include <chronon3d/scene/render_node.hpp>
#include <chronon3d/math/mat4.hpp>
#include <chronon3d/math/vec2.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/math/transform.hpp>

namespace chronon3d {
namespace renderer {

void fill_convex_quad(Framebuffer& fb, const Vec2 v[4], const Color& color);
void bline(Framebuffer& fb, Vec2 p0, Vec2 p1, const Color& color);

void draw_fake_box3d(Framebuffer& fb, const RenderNode& node, const RenderState& state);
void draw_grid_plane(Framebuffer& fb, const RenderNode& node, const RenderState& state);

void render_mesh_wireframe(Framebuffer& fb, const Mesh& mesh, const Mat4& model,
                           const Mat4& view, const Mat4& proj, const Color& color);

void draw_transformed_shape(Framebuffer& fb, const Shape& shape, const Mat4& model, const Color& color,
                             f32 spread = 0.0f, const RenderState* state = nullptr);

raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread = 0.0f);
bool hit_test(const Shape& s, Vec2 p, f32 spread = 0.0f);
bool pixel_passes_mask(const RenderState& state, i32 x, i32 y);

Vec2 project_2_5d(const Vec3& wp, const Mat4& view, f32 focal, f32 vp_cx, f32 vp_cy, bool& ok);

bool clip_and_project_line(const Vec3& w0, const Vec3& w1,
                           const Mat4& view, f32 focal, f32 vp_cx, f32 vp_cy,
                           Vec2& p0, Vec2& p1);

} // namespace renderer
} // namespace chronon3d
