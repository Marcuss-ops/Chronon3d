#include "fake_box3d_renderer.hpp"
#include "../rasterizers/line_rasterizer.hpp"
#include "../rasterizers/scanline_rasterizer.hpp"
#include "../utils/projection_utils.hpp"
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

void draw_fake_box3d(Framebuffer& fb, const RenderNode& node, const RenderState& state, const FakeBox3DShape& s) {
    if (!s.cam_ready) return;

    const Vec3& wp = s.world_pos;
    const f32 hw = s.size.x * 0.5f;
    const f32 hh = s.size.y * 0.5f;

    Vec3 c[8] = {
        {wp.x-hw, wp.y+hh, wp.z},
        {wp.x+hw, wp.y+hh, wp.z},
        {wp.x+hw, wp.y-hh, wp.z},
        {wp.x-hw, wp.y-hh, wp.z},
        {wp.x-hw, wp.y+hh, wp.z+s.depth},
        {wp.x+hw, wp.y+hh, wp.z+s.depth},
        {wp.x+hw, wp.y-hh, wp.z+s.depth},
        {wp.x-hw, wp.y-hh, wp.z+s.depth},
    };

    Vec2 sc[8];
    bool ok[8];
    for (int i = 0; i < 8; ++i)
        sc[i] = project_2_5d(c[i], s.cam_view, s.cam_focal, s.vp_cx, s.vp_cy, ok[i]);

    const Mat4 inv_view = glm::inverse(s.cam_view);
    const Vec3 cam_world = Vec3(inv_view[3]);
    const Vec3 to_cam = glm::normalize(cam_world - wp);

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
        if (glm::dot(to_cam, faces[fi].normal) <= -0.1f) continue;

        const auto& f = faces[fi];
        bool all_ok = true;
        for (int ci = 0; ci < 4; ++ci) if (!ok[f.idx[ci]]) { all_ok = false; break; }
        if (!all_ok) continue;

        Vec2 quad[4];
        for (int ci = 0; ci < 4; ++ci) quad[ci] = sc[f.idx[ci]];

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
