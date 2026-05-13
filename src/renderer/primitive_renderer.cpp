#include "primitive_renderer.hpp"
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/scene/layer.hpp>
#include <chronon3d/render_graph/render_node.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d {
namespace renderer {

Vec2 project_2_5d(const Vec3& wp, const Mat4& view, f32 focal, f32 vp_cx, f32 vp_cy, bool& ok) {
    Vec4 cam = view * Vec4(wp, 1.0f);
    if (cam.z >= 0.0f) { ok = false; return {}; }
    const f32 ps = focal / (-cam.z);
    ok = true;
    return {cam.x * ps + vp_cx, -cam.y * ps + vp_cy};
}

void fill_convex_quad(Framebuffer& fb, const Vec2 v[4], const Color& color) {
    if (color.a <= 0.0f) return;

    f32 y_min = v[0].y, y_max = v[0].y;
    for (int i = 1; i < 4; ++i) { y_min = std::min(y_min, v[i].y); y_max = std::max(y_max, v[i].y); }

    const i32 row0 = std::max(0, static_cast<i32>(std::ceil(y_min)));
    const i32 row1 = std::min(fb.height() - 1, static_cast<i32>(std::floor(y_max)));

    for (i32 y = row0; y <= row1; ++y) {
        const f32 fy = static_cast<f32>(y) + 0.5f;
        f32 x_left = 1e9f, x_right = -1e9f;
        for (int i = 0; i < 4; ++i) {
            const Vec2& a = v[i], & b = v[(i + 1) % 4];
            if ((a.y <= fy && b.y > fy) || (b.y <= fy && a.y > fy)) {
                const f32 t = (fy - a.y) / (b.y - a.y);
                const f32 x = a.x + t * (b.x - a.x);
                x_left  = std::min(x_left, x);
                x_right = std::max(x_right, x);
            }
        }
        const i32 x0 = std::max(0, static_cast<i32>(std::ceil(x_left)));
        const i32 x1 = std::min(fb.width() - 1, static_cast<i32>(std::floor(x_right)));
        for (i32 x = x0; x <= x1; ++x)
            fb.set_pixel(x, y, compositor::blend(color, fb.get_pixel(x, y), BlendMode::Normal));
    }
}

void bline(Framebuffer& fb, Vec2 p0, Vec2 p1, const Color& color) {
    if (color.a <= 0.0f) return;
    i32 x0 = static_cast<i32>(p0.x), y0 = static_cast<i32>(p0.y);
    i32 x1 = static_cast<i32>(p1.x), y1 = static_cast<i32>(p1.y);
    const i32 dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const i32 dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    i32 err = dx + dy, e2;
    while (true) {
        if (x0 >= 0 && x0 < fb.width() && y0 >= 0 && y0 < fb.height())
            fb.set_pixel(x0, y0, compositor::blend(color, fb.get_pixel(x0, y0), BlendMode::Normal));
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void draw_fake_box3d(Framebuffer& fb, const RenderNode& node, const RenderState& state) {
    const auto& s = node.shape.fake_box3d;
    if (!s.cam_ready) return;

    const Vec3& wp = s.world_pos;
    const f32 hw = s.size.x * 0.5f;
    const f32 hh = s.size.y * 0.5f;

    Vec3 c[8] = {
        {wp.x-hw, wp.y+hh, wp.z},            // 0 front top-left
        {wp.x+hw, wp.y+hh, wp.z},            // 1 front top-right
        {wp.x+hw, wp.y-hh, wp.z},            // 2 front bottom-right
        {wp.x-hw, wp.y-hh, wp.z},            // 3 front bottom-left
        {wp.x-hw, wp.y+hh, wp.z+s.depth},   // 4 back top-left
        {wp.x+hw, wp.y+hh, wp.z+s.depth},   // 5 back top-right
        {wp.x+hw, wp.y-hh, wp.z+s.depth},   // 6 back bottom-right
        {wp.x-hw, wp.y-hh, wp.z+s.depth},   // 7 back bottom-left
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
        {{0,1,2,3}, { 0, 0,-1}},  // front
        {{5,4,7,6}, { 0, 0,+1}},  // back
        {{4,5,1,0}, { 0,+1, 0}},  // top
        {{3,2,6,7}, { 0,-1, 0}},  // bottom
        {{1,5,6,2}, {+1, 0, 0}},  // right
        {{4,0,3,7}, {-1, 0, 0}},  // left
    };

    const Color face_colors[NFACES] = {
        {base.r, base.g, base.b, base.a * op},                       // 0 front
        darken(base, 0.60f).with_alpha(base.a * op),                // 1 back
        brighten(base, 0.20f).with_alpha(base.a * op),              // 2 top
        darken(base, 0.45f).with_alpha(base.a * op),                // 3 bottom
        darken(base, 0.25f).with_alpha(base.a * op),                // 4 right
        darken(base, 0.35f).with_alpha(base.a * op),                // 5 left
    };

    const int draw_order[NFACES] = {1, 3, 5, 4, 2, 0};

    for (int oi = 0; oi < NFACES; ++oi) {
        const int fi = draw_order[oi];
        if (glm::dot(to_cam, faces[fi].normal) <= 0.0f) continue;

        const auto& f = faces[fi];
        bool all_ok = true;
        for (int ci = 0; ci < 4; ++ci) if (!ok[f.idx[ci]]) { all_ok = false; break; }
        if (!all_ok) continue;

        Vec2 quad[4];
        for (int ci = 0; ci < 4; ++ci) quad[ci] = sc[f.idx[ci]];
        fill_convex_quad(fb, quad, face_colors[fi]);
    }

    const Color hl = Color{1, 1, 1, 0.35f * op};
    auto draw_edge = [&](int i0, int i1) {
        if (ok[i0] && ok[i1]) bline(fb, sc[i0], sc[i1], hl);
    };

    draw_edge(0, 1); // front-top
    draw_edge(1, 5); // top-right
    draw_edge(5, 4); // top-back
    draw_edge(4, 0); // top-left
}

bool clip_and_project_line(const Vec3& w0, const Vec3& w1,
                           const Mat4& view, f32 focal, f32 vp_cx, f32 vp_cy,
                           Vec2& p0, Vec2& p1) {
    Vec4 c0 = view * Vec4(w0, 1.0f);
    Vec4 c1 = view * Vec4(w1, 1.0f);
    constexpr f32 near = 0.5f;

    if (c0.z >= -near && c1.z >= -near) return false;

    if (c0.z >= -near) {
        const f32 t = (-near - c1.z) / (c0.z - c1.z);
        c0 = c1 + (c0 - c1) * t;
    }
    if (c1.z >= -near) {
        const f32 t = (-near - c0.z) / (c1.z - c0.z);
        c1 = c0 + (c1 - c0) * t;
    }

    const f32 ps0 = focal / (-c0.z);
    const f32 ps1 = focal / (-c1.z);
    p0 = {c0.x * ps0 + vp_cx, -c0.y * ps0 + vp_cy};
    p1 = {c1.x * ps1 + vp_cx, -c1.y * ps1 + vp_cy};
    return true;
}

void draw_grid_plane(Framebuffer& fb, const RenderNode& node, const RenderState& state) {
    const auto& s = node.shape.grid_plane;
    if (!s.cam_ready) return;

    const Color col = s.color.to_linear();
    Color draw_col = col;
    draw_col.a *= state.opacity;
    if (draw_col.a <= 0.0f) return;

    const Vec3& cp = s.world_pos;
    const int n = static_cast<int>(s.extent / s.spacing) + 1;

    auto draw_segment = [&](const Vec3& w0, const Vec3& w1) {
        Vec2 p0, p1;
        if (clip_and_project_line(w0, w1, s.cam_view, s.cam_focal, s.vp_cx, s.vp_cy, p0, p1))
            bline(fb, p0, p1, draw_col);
    };

    if (s.axis == PlaneAxis::XZ) {
        for (int i = -n; i <= n; ++i) {
            const f32 z = cp.z + i * s.spacing;
            draw_segment({cp.x - s.extent, cp.y, z}, {cp.x + s.extent, cp.y, z});
        }
        for (int i = -n; i <= n; ++i) {
            const f32 x = cp.x + i * s.spacing;
            draw_segment({x, cp.y, cp.z - s.extent}, {x, cp.y, cp.z + s.extent});
        }
    } else { // XY
        for (int i = -n; i <= n; ++i) {
            const f32 y = cp.y + i * s.spacing;
            draw_segment({cp.x - s.extent, y, cp.z}, {cp.x + s.extent, y, cp.z});
        }
        for (int i = -n; i <= n; ++i) {
            const f32 x = cp.x + i * s.spacing;
            draw_segment({x, cp.y - s.extent, cp.z}, {x, cp.y + s.extent, cp.z});
        }
    }
}

} // namespace renderer
} // namespace chronon3d
