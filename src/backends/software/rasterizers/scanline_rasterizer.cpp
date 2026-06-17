#include "scanline_rasterizer.hpp"
#include <chronon3d/compositor/blend_mode.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d {
namespace renderer {

// ─── Utility: guard against NaN / Inf source colors ────────────────────────
static bool is_clean_color(const Color& c) {
    return !(std::isnan(c.r) || std::isnan(c.g) || std::isnan(c.b) || std::isnan(c.a) ||
             std::isinf(c.r) || std::isinf(c.g) || std::isinf(c.b) || std::isinf(c.a));
}

// ─── Scanline helper: fill a span with depth testing ───────────────────────
static void fill_span(Framebuffer& fb, int y, int x0, int x1,
                      const Color& color,
                      float z_start, float dz_dx,
                      std::span<float> depth_buffer) {
    if (x0 > x1) return;
    Color* row = fb.pixels_row(y);
    const i32 fb_w = fb.width();
    const bool use_depth = depth_buffer.size() == static_cast<usize>(fb_w) * fb.height();
    const float* db_row = use_depth ? &depth_buffer[static_cast<size_t>(y) * fb_w] : nullptr;

    for (int x = x0; x <= x1; ++x) {
        if (!is_clean_color(color)) continue;

        // Interpolate depth at this pixel
        const float pixel_z = z_start + dz_dx * (static_cast<float>(x) - static_cast<float>(x0));

        // Depth test: only write if closer than existing depth.
        // existing > 0.0f → valid depth (camera-space Z is always positive).
        // existing <= 0.0f → uninitialized (pass through).
        if (use_depth) {
            const float existing = db_row[x];
            if (existing > 0.0f && pixel_z >= existing) continue;
            const_cast<float*>(db_row)[x] = pixel_z;
        }

        Color& dst = row[x];
        const float inv_a = 1.0f - color.a;
        dst.r = color.r + dst.r * inv_a;
        dst.g = color.g + dst.g * inv_a;
        dst.b = color.b + dst.b * inv_a;
        dst.a = color.a + dst.a * inv_a;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 2D (flat) scanline rasterizers — no depth testing
// ─────────────────────────────────────────────────────────────────────────────

void fill_convex_quad(Framebuffer& fb, std::span<const Vec2, 4> v, const Color& color) {
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
        Color* row = fb.pixels_row(y);
        for (i32 x = x0; x <= x1; ++x) {
            row[x] = compositor::blend(color, row[x], BlendMode::Normal);
        }
    }
}

void fill_gradient_quad(Framebuffer& fb, std::span<const Vec2, 4> v, std::span<const Color, 4> colors) {
    f32 y_min = v[0].y, y_max = v[0].y;
    for (int i = 1; i < 4; ++i) { y_min = std::min(y_min, v[i].y); y_max = std::max(y_max, v[i].y); }
    const i32 row0 = std::max(0, static_cast<i32>(std::ceil(y_min)));
    const i32 row1 = std::min(fb.height() - 1, static_cast<i32>(std::floor(y_max)));

    for (i32 y = row0; y <= row1; ++y) {
        const f32 fy = static_cast<f32>(y) + 0.5f;
        f32 x_left = 1e9f, x_right = -1e9f;
        Color cl{}, cr{};
        for (int i = 0; i < 4; ++i) {
            const Vec2& a = v[i], &b = v[(i+1)%4];
            if (!((a.y <= fy && b.y > fy) || (b.y <= fy && a.y > fy))) continue;
            const f32 t = (fy - a.y) / (b.y - a.y);
            const f32 x = a.x + t*(b.x - a.x);
            const Color& ca = colors[i]; const Color& cb = colors[(i+1)%4];
            Color c{ca.r+t*(cb.r-ca.r), ca.g+t*(cb.g-ca.g),
                    ca.b+t*(cb.b-ca.b), ca.a+t*(cb.a-ca.a)};
            if (x < x_left)  { x_left  = x; cl = c; }
            if (x > x_right) { x_right = x; cr = c; }
        }
        const i32 x0 = std::max(0, static_cast<i32>(std::ceil(x_left)));
        const i32 x1 = std::min(fb.width()-1, static_cast<i32>(std::floor(x_right)));
        const f32 span = x_right - x_left;
        Color* row = fb.pixels_row(y);
        for (i32 x = x0; x <= x1; ++x) {
            f32 t = (span > 0.01f) ? (x - x_left) / span : 0.5f;
            Color c{cl.r+t*(cr.r-cl.r), cl.g+t*(cr.g-cl.g),
                    cl.b+t*(cr.b-cl.b), cl.a+t*(cr.a-cl.a)};
            if (c.a > 0.01f) {
                row[x] = compositor::blend(c, row[x], BlendMode::Normal);
            }
        }
    }
}

void fill_triangle(Framebuffer& fb, std::span<const Vec2, 3> v, const Color& color) {
    if (color.a <= 0.0f) return;

    f32 y_min = v[0].y, y_max = v[0].y;
    for (int i = 1; i < 3; ++i) { y_min = std::min(y_min, v[i].y); y_max = std::max(y_max, v[i].y); }

    const i32 row0 = std::max(0, static_cast<i32>(std::ceil(y_min)));
    const i32 row1 = std::min(fb.height() - 1, static_cast<i32>(std::floor(y_max)));

    for (i32 y = row0; y <= row1; ++y) {
        const f32 fy = static_cast<f32>(y) + 0.5f;
        f32 x_left = 1e9f, x_right = -1e9f;
        for (int i = 0; i < 3; ++i) {
            const Vec2& a = v[i], & b = v[(i + 1) % 3];
            if ((a.y <= fy && b.y > fy) || (b.y <= fy && a.y > fy)) {
                const f32 t = (fy - a.y) / (b.y - a.y);
                const f32 x = a.x + t * (b.x - a.x);
                x_left  = std::min(x_left, x);
                x_right = std::max(x_right, x);
            }
        }
        const i32 x0 = std::max(0, static_cast<i32>(std::ceil(x_left)));
        const i32 x1 = std::min(fb.width() - 1, static_cast<i32>(std::floor(x_right)));
        Color* row = fb.pixels_row(y);
        for (i32 x = x0; x <= x1; ++x) {
            row[x] = compositor::blend(color, row[x], BlendMode::Normal);
        }
    }
}

void fill_gradient_triangle(Framebuffer& fb, std::span<const Vec2, 3> v, std::span<const Color, 3> colors) {
    f32 y_min = v[0].y, y_max = v[0].y;
    for (int i = 1; i < 3; ++i) { y_min = std::min(y_min, v[i].y); y_max = std::max(y_max, v[i].y); }
    const i32 row0 = std::max(0, static_cast<i32>(std::ceil(y_min)));
    const i32 row1 = std::min(fb.height() - 1, static_cast<i32>(std::floor(y_max)));

    for (i32 y = row0; y <= row1; ++y) {
        const f32 fy = static_cast<f32>(y) + 0.5f;
        f32 x_left = 1e9f, x_right = -1e9f;
        Color cl{}, cr{};
        for (int i = 0; i < 3; ++i) {
            const Vec2& a = v[i], &b = v[(i+1)%3];
            if (!((a.y <= fy && b.y > fy) || (b.y <= fy && a.y > fy))) continue;
            const f32 t = (fy - a.y) / (b.y - a.y);
            const f32 x = a.x + t*(b.x - a.x);
            const Color& ca = colors[i]; const Color& cb = colors[(i+1)%3];
            Color c{ca.r+t*(cb.r-ca.r), ca.g+t*(cb.g-ca.g),
                    ca.b+t*(cb.b-ca.b), ca.a+t*(cb.a-ca.a)};
            if (x < x_left)  { x_left  = x; cl = c; }
            if (x > x_right) { x_right = x; cr = c; }
        }
        const i32 x0 = std::max(0, static_cast<i32>(std::ceil(x_left)));
        const i32 x1 = std::min(fb.width()-1, static_cast<i32>(std::floor(x_right)));
        const f32 span = x_right - x_left;
        Color* row = fb.pixels_row(y);
        for (i32 x = x0; x <= x1; ++x) {
            f32 t = (span > 0.01f) ? (x - x_left) / span : 0.5f;
            Color c{cl.r+t*(cr.r-cl.r), cl.g+t*(cr.g-cl.g),
                    cl.b+t*(cr.b-cl.b), cl.a+t*(cr.a-cl.a)};
            if (c.a > 0.01f) {
                row[x] = compositor::blend(c, row[x], BlendMode::Normal);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 3D scanline rasterizers — with per-pixel depth test
// ─────────────────────────────────────────────────────────────────────────────

void fill_triangle(Framebuffer& fb, std::span<const Vec3, 3> v, const Color& color,
                    std::span<float> depth_buffer) {
    if (color.a <= 0.0f) return;
    if (!is_clean_color(color)) return;

    // Sort vertices by y
    Vec3 v0 = v[0], v1 = v[1], v2 = v[2];
    if (v0.y > v1.y) std::swap(v0, v1);
    if (v1.y > v2.y) std::swap(v1, v2);
    if (v0.y > v1.y) std::swap(v0, v1);

    const int y0 = std::max(0, static_cast<int>(std::ceil(v0.y)));
    const int y2 = std::min(fb.height() - 1, static_cast<int>(std::floor(v2.y)));
    if (y0 > y2) return;

    const float h = v2.y - v0.y;
    if (h < 0.5f) return;

    for (int y = y0; y <= y2; ++y) {
        const float t_full = (static_cast<float>(y) - v0.y) / h;
        const float x_long = v0.x + (v2.x - v0.x) * t_full;
        const float z_long = v0.z + (v2.z - v0.z) * t_full;

        float x_short, z_short;
        if (static_cast<float>(y) < v1.y) {
            const float seg = v1.y - v0.y;
            if (seg > 0.5f) {
                const float t = (static_cast<float>(y) - v0.y) / seg;
                x_short = v0.x + (v1.x - v0.x) * t;
                z_short = v0.z + (v1.z - v0.z) * t;
            } else {
                x_short = v1.x;
                z_short = v1.z;
            }
        } else {
            const float seg = v2.y - v1.y;
            if (seg > 0.5f) {
                const float t = (static_cast<float>(y) - v1.y) / seg;
                x_short = v1.x + (v2.x - v1.x) * t;
                z_short = v1.z + (v2.z - v1.z) * t;
            } else {
                x_short = v2.x;
                z_short = v2.z;
            }
        }

        const bool long_is_left = x_long < x_short;
        const float x_start = long_is_left ? x_long : x_short;
        const float x_end   = long_is_left ? x_short : x_long;
        const float z_start = long_is_left ? z_long : z_short;
        const float z_end   = long_is_left ? z_short : z_long;

        const int x0 = std::max(0, static_cast<int>(std::ceil(x_start)));
        const int x1 = std::min(fb.width() - 1, static_cast<int>(std::floor(x_end)));
        if (x0 > x1) continue;

        const float span = x_end - x_start;
        const float dz_dx = (span > 0.001f) ? (z_end - z_start) / span : 0.0f;

        fill_span(fb, y, x0, x1, color, z_start, dz_dx, depth_buffer);
    }
}

void fill_gradient_triangle(Framebuffer& fb, std::span<const Vec3, 3> v, std::span<const Color, 3> colors,
                             std::span<float> depth_buffer) {
    if (!is_clean_color(colors[0]) && !is_clean_color(colors[1]) && !is_clean_color(colors[2])) return;

    // Sort vertices by y (carry colors and z)
    struct Vert { Vec3 pos; Color col; };
    Vert va{v[0], colors[0]}, vb{v[1], colors[1]}, vc{v[2], colors[2]};
    if (va.pos.y > vb.pos.y) std::swap(va, vb);
    if (vb.pos.y > vc.pos.y) std::swap(vb, vc);
    if (va.pos.y > vb.pos.y) std::swap(va, vb);

    const int y0 = std::max(0, static_cast<int>(std::ceil(va.pos.y)));
    const int y2 = std::min(fb.height() - 1, static_cast<int>(std::floor(vc.pos.y)));
    if (y0 > y2) return;

    const float h = vc.pos.y - va.pos.y;
    if (h < 0.5f) return;

    for (int y = y0; y <= y2; ++y) {
        const float fy = static_cast<float>(y);
        const float t_full = (fy - va.pos.y) / h;
        const float x_long = va.pos.x + (vc.pos.x - va.pos.x) * t_full;
        const float z_long = va.pos.z + (vc.pos.z - va.pos.z) * t_full;
        const Color c_long = {
            va.col.r + (vc.col.r - va.col.r) * t_full,
            va.col.g + (vc.col.g - va.col.g) * t_full,
            va.col.b + (vc.col.b - va.col.b) * t_full,
            va.col.a + (vc.col.a - va.col.a) * t_full,
        };

        float x_short, z_short;
        Color c_short;
        if (fy < vb.pos.y) {
            const float seg = vb.pos.y - va.pos.y;
            if (seg > 0.5f) {
                const float t = (fy - va.pos.y) / seg;
                x_short = va.pos.x + (vb.pos.x - va.pos.x) * t;
                z_short = va.pos.z + (vb.pos.z - va.pos.z) * t;
                c_short = {
                    va.col.r + (vb.col.r - va.col.r) * t,
                    va.col.g + (vb.col.g - va.col.g) * t,
                    va.col.b + (vb.col.b - va.col.b) * t,
                    va.col.a + (vb.col.a - va.col.a) * t,
                };
            } else {
                x_short = vb.pos.x;
                z_short = vb.pos.z;
                c_short = vb.col;
            }
        } else {
            const float seg = vc.pos.y - vb.pos.y;
            if (seg > 0.5f) {
                const float t = (fy - vb.pos.y) / seg;
                x_short = vb.pos.x + (vc.pos.x - vb.pos.x) * t;
                z_short = vb.pos.z + (vc.pos.z - vb.pos.z) * t;
                c_short = {
                    vb.col.r + (vc.col.r - vb.col.r) * t,
                    vb.col.g + (vc.col.g - vb.col.g) * t,
                    vb.col.b + (vc.col.b - vb.col.b) * t,
                    vb.col.a + (vc.col.a - vb.col.a) * t,
                };
            } else {
                x_short = vc.pos.x;
                z_short = vc.pos.z;
                c_short = vc.col;
            }
        }

        const bool long_is_left = x_long < x_short;
        const float x_left  = long_is_left ? x_long : x_short;
        const float x_right = long_is_left ? x_short : x_long;
        const float z_left  = long_is_left ? z_long : z_short;
        const float z_right = long_is_left ? z_short : z_long;
        const Color& c_left  = long_is_left ? c_long : c_short;
        const Color& c_right = long_is_left ? c_short : c_long;

        const int x_start = std::max(0, static_cast<int>(std::ceil(x_left)));
        const int x_end   = std::min(fb.width() - 1, static_cast<int>(std::floor(x_right)));
        if (x_start > x_end) continue;

        const float span = x_right - x_left;
        const float dz_dx = (span > 0.001f) ? (z_right - z_left) / span : 0.0f;

        Color* row = fb.pixels_row(y);
        const i32 fb_w = fb.width();
        const bool use_depth = depth_buffer.size() == static_cast<usize>(fb_w) * fb.height();
        const float* db_row = use_depth ? &depth_buffer[static_cast<size_t>(y) * fb_w] : nullptr;

        for (int x = x_start; x <= x_end; ++x) {
            const float t = (span > 0.01f) ? (static_cast<float>(x) - x_left) / span : 0.5f;
            const Color pixel_c = {
                c_left.r + t * (c_right.r - c_left.r),
                c_left.g + t * (c_right.g - c_left.g),
                c_left.b + t * (c_right.b - c_left.b),
                c_left.a + t * (c_right.a - c_left.a),
            };
            if (pixel_c.a <= 0.01f) continue;
            if (!is_clean_color(pixel_c)) continue;

            // Depth test
            if (use_depth) {
                const float pixel_z = z_left + dz_dx * (static_cast<float>(x) - x_left);
                const float existing = db_row[x];
                if (existing > 0.0f && pixel_z >= existing) continue;
                const_cast<float*>(db_row)[x] = pixel_z;
            }

            Color& dst = row[x];
            const float inv_a = 1.0f - pixel_c.a;
            dst.r = pixel_c.r * pixel_c.a + dst.r * inv_a;
            dst.g = pixel_c.g * pixel_c.a + dst.g * inv_a;
            dst.b = pixel_c.b * pixel_c.a + dst.b * inv_a;
            dst.a = pixel_c.a + dst.a * inv_a;
        }
    }
}

void fill_convex_quad(Framebuffer& fb, std::span<const Vec3, 4> v, const Color& color,
                       std::span<float> depth_buffer) {
    // Split quad into two triangles: TL→TR→BR and TL→BR→BL.
    // first<3>() extracts v[0..2] = TL, TR, BR for the first triangle.
    fill_triangle(fb, v.first<3>(), color, depth_buffer);
    {
        Vec3 tri[3] = {v[0], v[2], v[3]};             // v[0]=TL, v[2]=BR, v[3]=BL
        fill_triangle(fb, tri, color, depth_buffer);
    }
}

void fill_gradient_quad(Framebuffer& fb, std::span<const Vec3, 4> v, std::span<const Color, 4> colors,
                         std::span<float> depth_buffer) {
    // Split quad into two gradient triangles: TL→TR→BR and TL→BR→BL
    {
        Vec3 tri_v[3] = {v[0], v[1], v[2]};
        Color tri_c[3] = {colors[0], colors[1], colors[2]};
        fill_gradient_triangle(fb, tri_v, tri_c, depth_buffer);
    }
    {
        Vec3 tri_v[3] = {v[0], v[2], v[3]};
        Color tri_c[3] = {colors[0], colors[2], colors[3]};
        fill_gradient_triangle(fb, tri_v, tri_c, depth_buffer);
    }
}

} // namespace renderer
} // namespace chronon3d
