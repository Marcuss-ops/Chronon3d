#include "fake_box3d_renderer.hpp"
#include "../rasterizers/line_rasterizer.hpp"
#include "../rasterizers/scanline_rasterizer.hpp"
#include <chronon3d/backends/software/projector_2_5d.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d {
namespace renderer {

// Key light: upper-left-front direction. Consistent with extruded text renderer.
static const Vec3 k_box_light = glm::normalize(Vec3(-0.4f, 1.2f, -0.6f));
static constexpr float k_box_ambient = 0.20f;
static constexpr float k_box_diffuse = 0.80f;

static float box_ndotl(const Vec3& normal) {
    return k_box_ambient + k_box_diffuse * std::max(0.0f, glm::dot(normal, k_box_light));
}

void draw_fake_box3d(Framebuffer& fb, const RenderNode& node, const RenderState& state,
                     const FakeBox3DShape& s, const FakeBox3DRenderState& rt) {
    if (!rt.projection.ready) return;
    const Projector2_5D& projector = rt.projection;

    const Vec3& wp = s.world_pos;
    const f32 hw = s.size.x * 0.5f;
    const f32 hh = s.size.y * 0.5f;

    // Build corners in local space, then transform to world via world_matrix
    const Vec3 local[8] = {
        {wp.x-hw, wp.y+hh, wp.z},
        {wp.x+hw, wp.y+hh, wp.z},
        {wp.x+hw, wp.y-hh, wp.z},
        {wp.x-hw, wp.y-hh, wp.z},
        {wp.x-hw, wp.y+hh, wp.z+s.depth},
        {wp.x+hw, wp.y+hh, wp.z+s.depth},
        {wp.x+hw, wp.y-hh, wp.z+s.depth},
        {wp.x-hw, wp.y-hh, wp.z+s.depth},
    };
    Vec3 c[8];
    for (int i = 0; i < 8; ++i) {
        Vec4 w = rt.world_matrix * Vec4(local[i], 1.0f);
        c[i] = {w.x, w.y, w.z};
    }

    const Mat4 inv_view = glm::inverse(projector.view);
    const Vec3 cam_world = Vec3(inv_view[3]);
    // Use world-space box center for to_cam direction
    const Vec4 ctr_w = rt.world_matrix * Vec4(wp, 1.0f);
    const Vec3 box_center{ctr_w.x, ctr_w.y, ctr_w.z};
    const Vec3 to_cam = glm::normalize(cam_world - box_center);

    const Color base = s.color.to_linear();
    const f32 op = state.opacity;

    struct FaceDef { int idx[4]; Vec3 normal; };
    constexpr int NFACES = 6;
    const FaceDef faces[NFACES] = {
        {{0,1,2,3}, { 0, 0,-1}},  // Front
        {{5,4,7,6}, { 0, 0,+1}},  // Back
        {{4,5,1,0}, { 0,+1, 0}},  // Top
        {{3,2,6,7}, { 0,-1, 0}},  // Bottom
        {{1,5,6,2}, {+1, 0, 0}},  // Right
        {{4,0,3,7}, {-1, 0, 0}},  // Left
    };

    const int draw_order[NFACES] = {1, 3, 5, 4, 2, 0};

    for (int oi = 0; oi < NFACES; ++oi) {
        const int fi = draw_order[oi];
        // Transform face normal to world space for backface culling
        Vec3 wn = Vec3(rt.world_matrix * Vec4(faces[fi].normal, 0.0f));
        if (glm::dot(to_cam, wn) <= -0.1f) continue;

        const auto& f = faces[fi];
        Vec3 face_world[4] = {
            c[f.idx[0]],
            c[f.idx[1]],
            c[f.idx[2]],
            c[f.idx[3]],
        };
        const auto projected = projector.project_quad(face_world);
        if (!projected.visible) continue;

        Vec2 quad[4];
        for (int ci = 0; ci < 4; ++ci) quad[ci] = projected.corners[ci];

        float light = box_ndotl(faces[fi].normal);

        if (fi == 2) {
            // Top face: gradient (back edge slightly darker, front edge brighter)
            float light_back  = light * 0.92f;
            float light_front = std::min(1.0f, light * 1.06f);
            Color c_back  = Color{base.r*light_back,  base.g*light_back,  base.b*light_back,  base.a*op};
            Color c_front = Color{std::min(1.0f,base.r*light_front), std::min(1.0f,base.g*light_front), std::min(1.0f,base.b*light_front), base.a*op};
            Color gc[4] = {c_back, c_back, c_front, c_front};
            fill_gradient_quad(fb, quad, gc);
        } else if (fi == 0) {
            // Front face: subtle top-to-bottom gradient
            float light_top = std::min(1.0f, light * 1.04f);
            float light_bot = light * 0.94f;
            Color c_top = Color{std::min(1.0f,base.r*light_top), std::min(1.0f,base.g*light_top), std::min(1.0f,base.b*light_top), base.a*op};
            Color c_bot = Color{base.r*light_bot, base.g*light_bot, base.b*light_bot, base.a*op};
            Color gc[4] = {c_top, c_top, c_bot, c_bot};
            fill_gradient_quad(fb, quad, gc);
        } else {
            Color face_c = Color{base.r*light, base.g*light, base.b*light, base.a*op};
            fill_convex_quad(fb, quad, face_c);
        }
    }
}

} // namespace renderer
} // namespace chronon3d
