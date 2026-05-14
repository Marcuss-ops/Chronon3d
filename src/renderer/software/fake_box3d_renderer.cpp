#include "fake_box3d_renderer.hpp"
#include "line_rasterizer.hpp"
#include "scanline_rasterizer.hpp"
#include "projection_utils.hpp"
#include <chronon3d/compositor/blend_mode.hpp>
#include <glm/glm.hpp>
#include <algorithm>

namespace chronon3d {
namespace renderer {

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

    auto brighten = [](Color col, f32 t) -> Color {
        return {std::min(1.0f, col.r+t), std::min(1.0f, col.g+t), std::min(1.0f, col.b+t), col.a};
    };
    auto darken = [](Color col, f32 t) -> Color {
        return {std::max(0.0f, col.r-t), std::max(0.0f, col.g-t), std::max(0.0f, col.b-t), col.a};
    };

    struct FaceDef { int idx[4]; Vec3 normal; };
    constexpr int NFACES = 6;
    const FaceDef faces[NFACES] = {
        {{0,1,2,3}, { 0, 0,-1}},
        {{5,4,7,6}, { 0, 0,+1}},
        {{4,5,1,0}, { 0,+1, 0}},
        {{3,2,6,7}, { 0,-1, 0}},
        {{1,5,6,2}, {+1, 0, 0}},
        {{4,0,3,7}, {-1, 0, 0}},
    };

    const Color face_colors[NFACES] = {
        {base.r, base.g, base.b, base.a * op},           // Front
        darken(base, 0.40f).with_alpha(base.a * op),     // Back
        brighten(base, 0.15f).with_alpha(base.a * op),   // Top
        darken(base, 0.30f).with_alpha(base.a * op),     // Bottom
        darken(base, 0.15f).with_alpha(base.a * op),     // Right
        darken(base, 0.25f).with_alpha(base.a * op),     // Left
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
        fill_convex_quad(fb, quad, face_colors[fi]);
    }

    const Color hl = Color{1, 1, 1, 0.25f * op};
    auto draw_edge = [&](int i0, int i1) {
        if (ok[i0] && ok[i1]) bline(fb, sc[i0], sc[i1], hl);
    };
    draw_edge(0, 1); draw_edge(1, 2); draw_edge(2, 3); draw_edge(3, 0);
    draw_edge(0, 4); draw_edge(1, 5); draw_edge(2, 6); draw_edge(3, 7);
    draw_edge(4, 5); draw_edge(5, 6); draw_edge(6, 7); draw_edge(7, 4);
}

} // namespace renderer
} // namespace chronon3d
