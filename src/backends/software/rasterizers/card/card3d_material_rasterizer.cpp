#include <chronon3d/backends/software/rasterizers/card3d_material_rasterizer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/rendering/lighting_eval.hpp>
#include "../scanline_rasterizer.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace chronon3d::renderer {

// ─── ProjectedCard-based render_card3d ─────────────────────────────────────

static Vec2 normalize_v2(Vec2 v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y);
    if (len < 1e-8f) return {0.0f, 0.0f};
    return {v.x / len, v.y / len};
}

static Vec2 extrude(Vec2 corner, Vec2 dir, float thickness) {
    return {corner.x + dir.x * thickness, corner.y + dir.y * thickness};
}

static Vec2 light_extrude_dir(const Vec3& light_dir) {
    Vec2 dir{light_dir.x, -light_dir.y};
    return normalize_v2(dir);
}

void render_card3d(
    Framebuffer& fb,
    const rendering::ProjectedCard& card,
    const Card3DMaterial& material,
    float opacity,
    std::span<float> depth_buffer
) {
    if (!card.visible || opacity <= 0.0f) return;

    const Vec3& TL = card.corners[0];
    const Vec3& TR = card.corners[1];
    const Vec3& BR = card.corners[2];
    const Vec3& BL = card.corners[3];

    const bool use_depth = !depth_buffer.empty();

    const Vec2 extrude_dir = light_extrude_dir(material.light_direction);
    const float t = material.thickness_px;
    const float avg_z = (TL.z + TR.z + BR.z + BL.z) * 0.25f;

    // ── Side quads (extruded edges) ──────────────────────────────────────
    auto side_quad_depth = [&](const Vec3& a, const Vec3& b) {
        const Vec2 b2d{b.x, b.y};
        const Vec2 e_a = extrude({a.x, a.y}, extrude_dir, t);
        const Vec2 e_b = extrude(b2d, extrude_dir, t);
        const Vec3 q[4] = {
            a,
            b,
            {e_b.x, e_b.y, avg_z},   // extruded → use avg_z (slightly behind card)
            {e_a.x, e_a.y, avg_z},
        };
        fill_convex_quad(fb, q, material.side_color.with_alpha(material.side_color.a * opacity),
                         depth_buffer);
    };

    const bool show_right = extrude_dir.x > 0.0f;
    const bool show_bottom = extrude_dir.y > 0.0f;

    if (show_right) {
        side_quad_depth(TR, BR);
    } else {
        side_quad_depth(BL, TL);
    }

    if (show_bottom) {
        side_quad_depth(BR, BL);
    } else {
        side_quad_depth(TR, TL);
    }

    // ── Front face with gradient ────────────────────────────────────────
    Color gc[4] = {
        material.front_top_color.with_alpha(material.front_top_color.a * opacity),
        material.front_top_color.with_alpha(material.front_top_color.a * opacity),
        material.front_bottom_color.with_alpha(material.front_bottom_color.a * opacity),
        material.front_bottom_color.with_alpha(material.front_bottom_color.a * opacity),
    };
    if (use_depth) {
        fill_gradient_quad(fb, card.corners, gc, depth_buffer);
    } else {
        const Vec2 corners2d[4] = {{TL.x, TL.y}, {TR.x, TR.y}, {BR.x, BR.y}, {BL.x, BL.y}};
        fill_gradient_quad(fb, corners2d, gc);
    }

    // ── Edge highlight ──────────────────────────────────────────────────
    if (material.edge_highlight_intensity > 0.0f && material.edge_highlight_color.a > 0.0f) {
        const float hl_w = std::max(1.5f, t * 0.12f);
        const Color hl_c = material.edge_highlight_color.with_alpha(
            material.edge_highlight_color.a * material.edge_highlight_intensity * opacity
        );

        auto highlight_edge = [&](const Vec3& e0, const Vec3& e1, bool flip_normal) {
            Vec2 edge_dir = normalize_v2({e1.x - e0.x, e1.y - e0.y});
            Vec2 out_normal = flip_normal ? Vec2{edge_dir.y, -edge_dir.x} : Vec2{-edge_dir.y, edge_dir.x};
            Vec2 center = {(TL.x + TR.x + BR.x + BL.x) * 0.25f, (TL.y + TR.y + BR.y + BL.y) * 0.25f};
            Vec2 to_center = {center.x - (e0.x + e1.x) * 0.5f, center.y - (e0.y + e1.y) * 0.5f};
            if (out_normal.x * to_center.x + out_normal.y * to_center.y > 0.0f) {
                out_normal = {-out_normal.x, -out_normal.y};
            }
            const Vec3 hl[4] = {
                e0,
                e1,
                {e1.x + out_normal.x * hl_w, e1.y + out_normal.y * hl_w, avg_z},
                {e0.x + out_normal.x * hl_w, e0.y + out_normal.y * hl_w, avg_z},
            };
            if (use_depth) {
                fill_convex_quad(fb, hl, hl_c, depth_buffer);
            } else {
                const Vec2 hl2[4] = {{hl[0].x, hl[0].y}, {hl[1].x, hl[1].y}, {hl[2].x, hl[2].y}, {hl[3].x, hl[3].y}};
                fill_convex_quad(fb, hl2, hl_c);
            }
        };

        highlight_edge(TL, TR, false); // top
        highlight_edge(TL, BL, true);  // left
    }

    // ── Rim light ───────────────────────────────────────────────────────
    if (material.rim_light_intensity > 0.0f && material.rim_light_color.a > 0.0f) {
        const float rim_w = std::max(2.0f, t * 0.20f);
        const Color rim_c = material.rim_light_color.with_alpha(
            material.rim_light_color.a * material.rim_light_intensity * opacity * 0.5f
        );

        auto rim_edge = [&](const Vec3& e0, const Vec3& e1) {
            Vec2 edge_dir = normalize_v2({e1.x - e0.x, e1.y - e0.y});
            Vec2 out_normal = {-edge_dir.y, edge_dir.x};
            Vec2 center = {(TL.x + TR.x + BR.x + BL.x) * 0.25f, (TL.y + TR.y + BR.y + BL.y) * 0.25f};
            Vec2 to_center = {center.x - (e0.x + e1.x) * 0.5f, center.y - (e0.y + e1.y) * 0.5f};
            if (out_normal.x * to_center.x + out_normal.y * to_center.y > 0.0f) {
                out_normal = {-out_normal.x, -out_normal.y};
            }
            const Vec3 r[4] = {
                e0,
                e1,
                {e1.x + out_normal.x * rim_w, e1.y + out_normal.y * rim_w, avg_z},
                {e0.x + out_normal.x * rim_w, e0.y + out_normal.y * rim_w, avg_z},
            };
            if (use_depth) {
                fill_convex_quad(fb, r, rim_c, depth_buffer);
            } else {
                const Vec2 r2[4] = {{r[0].x, r[0].y}, {r[1].x, r[1].y}, {r[2].x, r[2].y}, {r[3].x, r[3].y}};
                fill_convex_quad(fb, r2, rim_c);
            }
        };

        rim_edge(TL, TR); // top
        rim_edge(TR, BR); // right
    }
}

// ─── Local-pixel-coordinates render_card3d_material ───────────────────────

namespace {

void fill_triangle(Framebuffer& fb, Vec2 v0, Vec2 v1, Vec2 v2, const Color& color) {
    if (color.a <= 0.0f) return;
    if (std::isnan(color.r) || std::isnan(color.g) || std::isnan(color.b) || std::isnan(color.a) ||
        std::isinf(color.r) || std::isinf(color.g) || std::isinf(color.b) || std::isinf(color.a)) {
        return;
    }

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

        float x_short;
        if (static_cast<float>(y) < v1.y) {
            const float seg = v1.y - v0.y;
            x_short = (seg > 0.5f) ? v0.x + (v1.x - v0.x) * ((static_cast<float>(y) - v0.y) / seg) : v1.x;
        } else {
            const float seg = v2.y - v1.y;
            x_short = (seg > 0.5f) ? v1.x + (v2.x - v1.x) * ((static_cast<float>(y) - v1.y) / seg) : v2.x;
        }

        const int x_start = std::max(0, static_cast<int>(std::ceil(std::min(x_long, x_short))));
        const int x_end   = std::min(fb.width() - 1, static_cast<int>(std::floor(std::max(x_long, x_short))));

        Color* row = fb.pixels_row(y);
        for (int x = x_start; x <= x_end; ++x) {
            Color& dst = row[x];
            const float inv_a = 1.0f - color.a;
            dst.r = color.r * color.a + dst.r * inv_a;
            dst.g = color.g * color.a + dst.g * inv_a;
            dst.b = color.b * color.a + dst.b * inv_a;
            dst.a = color.a + dst.a * inv_a;
        }
    }
}

void fill_quad(Framebuffer& fb, const Vec2& p0, const Vec2& p1, const Vec2& p2, const Vec2& p3, const Color& color) {
    fill_triangle(fb, p0, p1, p2, color);
    fill_triangle(fb, p0, p2, p3, color);
}

void fill_gradient_quad(Framebuffer& fb, const Vec2& p0, const Vec2& p1, const Vec2& p2, const Vec2& p3,
                        const Color& top_color, const Color& bottom_color) {
    float min_y = std::min({p0.y, p1.y, p2.y, p3.y});
    float max_y = std::max({p0.y, p1.y, p2.y, p3.y});
    const int y0 = std::max(0, static_cast<int>(std::ceil(min_y)));
    const int y1 = std::min(fb.height() - 1, static_cast<int>(std::floor(max_y)));
    if (y0 > y1) return;

    const float height = max_y - min_y;
    if (height < 0.5f) return;

    for (int y = y0; y <= y1; ++y) {
        const float t = (static_cast<float>(y) - min_y) / height;
        const Color row_color = top_color * (1.0f - t) + bottom_color * t;
        if (row_color.a <= 0.0f) continue;

        float x_left = std::numeric_limits<float>::max();
        float x_right = std::numeric_limits<float>::lowest();

        auto check_edge = [&](const Vec2& a, const Vec2& b) {
            if (std::abs(b.y - a.y) < 0.5f) return;
            if (static_cast<float>(y) < std::min(a.y, b.y) || static_cast<float>(y) > std::max(a.y, b.y)) return;
            const float edge_t = (static_cast<float>(y) - a.y) / (b.y - a.y);
            const float x = a.x + (b.x - a.x) * edge_t;
            x_left = std::min(x_left, x);
            x_right = std::max(x_right, x);
        };

        check_edge(p0, p1);
        check_edge(p1, p2);
        check_edge(p2, p3);
        check_edge(p3, p0);

        if (x_left > x_right) continue;

        const int x_start = std::max(0, static_cast<int>(std::ceil(x_left)));
        const int x_end   = std::min(fb.width() - 1, static_cast<int>(std::floor(x_right)));

        Color* row = fb.pixels_row(y);
        const float inv_a = 1.0f - row_color.a;
        for (int x = x_start; x <= x_end; ++x) {
            Color& dst = row[x];
            dst.r = row_color.r * row_color.a + dst.r * inv_a;
            dst.g = row_color.g * row_color.a + dst.g * inv_a;
            dst.b = row_color.b * row_color.a + dst.b * inv_a;
            dst.a = row_color.a + dst.a * inv_a;
        }
    }
}

void draw_line(Framebuffer& fb, Vec2 p0, Vec2 p1, float thickness, const Color& color) {
    if (color.a <= 0.0f || thickness <= 0.0f) return;

    const float dx = p1.x - p0.x;
    const float dy = p1.y - p0.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.5f) return;

    const float nx = -dy / len;
    const float ny = dx / len;
    const float hw = thickness * 0.5f;

    const Vec2 v0{p0.x + nx * hw, p0.y + ny * hw};
    const Vec2 v1{p1.x + nx * hw, p1.y + ny * hw};
    const Vec2 v2{p1.x - nx * hw, p1.y - ny * hw};
    const Vec2 v3{p0.x - nx * hw, p0.y - ny * hw};

    fill_quad(fb, v0, v1, v2, v3, color);
}

} // anonymous namespace

void render_card3d_material(
    Framebuffer& fb,
    const Card3DMaterial& material,
    const Card3DRenderParams& params,
    const std::optional<rendering::LightContext>& lit_context,
    std::span<float> depth_buffer
) {
    if (!material.enabled || params.size.x <= 0.0f || params.size.y <= 0.0f) return;

    const float t = material.thickness_px;
    const Vec2& pos = params.position;
    const Vec2& sz = params.size;

    // Front face corners (clockwise from TL)
    const Vec2 f_tl{pos.x, pos.y};
    const Vec2 f_tr{pos.x + sz.x, pos.y};
    const Vec2 f_br{pos.x + sz.x, pos.y + sz.y};
    const Vec2 f_bl{pos.x, pos.y + sz.y};

    // Extruded corners
    const float ex = t * 0.7f;
    const float ey = t * 0.7f;

    const Vec2 e_tl{f_tl.x + ex, f_tl.y + ey};
    const Vec2 e_tr{f_tr.x + ex, f_tr.y + ey};
    const Vec2 e_br{f_br.x + ex, f_br.y + ey};
    const Vec2 e_bl{f_bl.x + ex, f_bl.y + ey};

    // Side face corners
    const Vec2 r_side_tl{f_tr.x, f_tr.y};
    const Vec2 r_side_tr{e_tr.x, e_tr.y};
    const Vec2 r_side_br{e_br.x, e_br.y};
    const Vec2 r_side_bl{f_br.x, f_br.y};

    const Vec2 b_side_tl{f_bl.x, f_bl.y};
    const Vec2 b_side_tr{f_br.x, f_br.y};
    const Vec2 b_side_br{e_br.x, e_br.y};
    const Vec2 b_side_bl{e_bl.x, e_bl.y};

    bool use_depth = !depth_buffer.empty();

    // 1. Right side quad
    if (t > 0.5f) {
        if (use_depth) {
            // Use a reasonable Z for side faces (same as card position depth)
            float side_z = 0.0f; // default: render unlittered
            const std::array<Vec3, 4> pts_r{{
                {r_side_tl.x, r_side_tl.y, side_z},
                {r_side_tr.x, r_side_tr.y, side_z},
                {r_side_br.x, r_side_br.y, side_z},
                {r_side_bl.x, r_side_bl.y, side_z},
            }};
            fill_convex_quad(fb, pts_r, material.side_color, depth_buffer);
        } else {
            fill_quad(fb, r_side_tl, r_side_tr, r_side_br, r_side_bl, material.side_color);
        }
    }

    // 2. Bottom side quad
    if (t > 0.5f) {
        if (use_depth) {
            float side_z = 0.0f;
            const std::array<Vec3, 4> pts_b{{
                {b_side_tl.x, b_side_tl.y, side_z},
                {b_side_tr.x, b_side_tr.y, side_z},
                {b_side_br.x, b_side_br.y, side_z},
                {b_side_bl.x, b_side_bl.y, side_z},
            }};
            fill_convex_quad(fb, pts_b, material.side_color, depth_buffer);
        } else {
            fill_quad(fb, b_side_tl, b_side_tr, b_side_br, b_side_bl, material.side_color);
        }
    }

    // 3. Front face with gradient
    // Note: render_card3d_material uses local pixel coordinates without real Z values,
    // so depth testing is applied to side faces (for self-occlusion) but not the front face.
    fill_gradient_quad(fb, f_tl, f_tr, f_br, f_bl, material.front_top_color, material.front_bottom_color);

    // 4. Edge highlight along extruded edges
    if (material.edge_highlight_intensity > 0.0f && t > 0.5f) {
        Color hl = material.edge_highlight_color;
        hl.a *= material.edge_highlight_intensity;

        draw_line(fb, r_side_tl, r_side_tr, 1.5f, hl);
        draw_line(fb, b_side_tr, b_side_br, 1.5f, hl);
        draw_line(fb, r_side_tr, e_tr, 1.0f, hl);
    }

    // 5. Rim light overlay
    if (material.rim_light_intensity > 0.0f) {
        const float rim_px = 2.0f;
        const Vec2 r_tl{f_tl.x - rim_px, f_tl.y - rim_px};
        const Vec2 r_tr{f_tr.x + rim_px, f_tr.y - rim_px};
        const Vec2 r_br{f_br.x + rim_px, f_br.y + rim_px};
        const Vec2 r_bl{f_bl.x - rim_px, f_bl.y + rim_px};

        Color rim_c = material.rim_light_color;
        rim_c.a *= material.rim_light_intensity * 0.15f;
        fill_gradient_quad(fb, r_tl, r_tr, r_br, r_bl, rim_c, Color{rim_c.r, rim_c.g, rim_c.b, 0.0f});
    }

    // 6. Apply lighting context if provided
    if (lit_context.has_value()) {
        const auto& ctx = *lit_context;
        if (ctx.enabled && (ctx.ambient_enabled || ctx.directional_enabled)) {
            Color lit_tint = ctx.ambient_color * ctx.ambient +
                             ctx.directional_color * ctx.diffuse;
            lit_tint.a = 0.25f;
            if (use_depth) {
                float front_z = 0.0f;
                const std::array<Vec3, 4> pts_f{{
                    {f_tl.x, f_tl.y, front_z},
                    {f_tr.x, f_tr.y, front_z},
                    {f_br.x, f_br.y, front_z},
                    {f_bl.x, f_bl.y, front_z},
                }};
                fill_convex_quad(fb, pts_f, lit_tint, depth_buffer);
            } else {
                fill_quad(fb, f_tl, f_tr, f_br, f_bl, lit_tint);
            }
        }
    }
}

} // namespace chronon3d::renderer
