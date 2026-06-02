#include <chronon3d/backends/software/rasterizers/card3d_material_rasterizer.hpp>
#include <chronon3d/rendering/lighting_eval.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d::renderer {

namespace {

// ── Helper: fill a triangle with solid color (alpha blend) ───────────
void fill_triangle(Framebuffer& fb, Vec2 v0, Vec2 v1, Vec2 v2, const Color& color) {
    if (color.a <= 0.0f) return;

    // Guard: skip NaN/Inf source color to prevent framebuffer contamination.
    if (std::isnan(color.r) || std::isnan(color.g) || std::isnan(color.b) || std::isnan(color.a) ||
        std::isinf(color.r) || std::isinf(color.g) || std::isinf(color.b) || std::isinf(color.a)) {
        return;
    }

    // Sort by y
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

// ── Helper: fill a quad as two triangles ───────────────────────────────
void fill_quad(Framebuffer& fb, const Vec2& p0, const Vec2& p1, const Vec2& p2, const Vec2& p3, const Color& color) {
    fill_triangle(fb, p0, p1, p2, color);
    fill_triangle(fb, p0, p2, p3, color);
}

// ── Helper: fill a vertical-gradient quad ────────────────────────────
void fill_gradient_quad(Framebuffer& fb, const Vec2& p0, const Vec2& p1, const Vec2& p2, const Vec2& p3,
                        const Color& top_color, const Color& bottom_color) {
    // Find vertical bounds
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

        // Find left/right x intersections for this scanline
        // Simplified: project y onto each edge and find x range
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

// ── Helper: draw a line segment with thickness and color ─────────────
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

// ── Public API ──────────────────────────────────────────────────────

void render_card3d_material(
    Framebuffer& fb,
    const Card3DMaterial& material,
    const Card3DRenderParams& params,
    const std::optional<rendering::LightContext>& lit_context
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

    // Extruded corners (shifted by thickness)
    // We offset the extruded face diagonally to simulate a light source from top-left
    const float ex = t * 0.7f;  // extrude x
    const float ey = t * 0.7f;  // extrude y

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

    // 1. Right side quad
    if (t > 0.5f) {
        fill_quad(fb, r_side_tl, r_side_tr, r_side_br, r_side_bl, material.side_color);
    }

    // 2. Bottom side quad
    if (t > 0.5f) {
        fill_quad(fb, b_side_tl, b_side_tr, b_side_br, b_side_bl, material.side_color);
    }

    // 3. Front face with gradient
    fill_gradient_quad(fb, f_tl, f_tr, f_br, f_bl, material.front_top_color, material.front_bottom_color);

    // 4. Edge highlight along extruded edges
    if (material.edge_highlight_opacity > 0.0f && t > 0.5f) {
        Color hl = material.edge_highlight_color;
        hl.a *= material.edge_highlight_opacity;

        // Top edge of right side (silhouette)
        draw_line(fb, r_side_tl, r_side_tr, 1.5f, hl);
        // Right edge of bottom side
        draw_line(fb, b_side_tr, b_side_br, 1.5f, hl);
        // Outer corner highlight
        draw_line(fb, r_side_tr, e_tr, 1.0f, hl);
    }

    // 5. Rim light overlay
    if (material.rim_intensity > 0.0f) {
        Vec2 center{pos.x + sz.x * 0.5f, pos.y + sz.y * 0.5f};
        // Apply rim as a subtle glow around the front face edges
        // We approximate by drawing a slightly larger quad with rim color
        const float rim_px = 2.0f;
        const Vec2 r_tl{f_tl.x - rim_px, f_tl.y - rim_px};
        const Vec2 r_tr{f_tr.x + rim_px, f_tr.y - rim_px};
        const Vec2 r_br{f_br.x + rim_px, f_br.y + rim_px};
        const Vec2 r_bl{f_bl.x - rim_px, f_bl.y + rim_px};

        Color rim_c = material.edge_highlight_color;
        rim_c.a *= material.rim_intensity * 0.15f; // subtle
        fill_gradient_quad(fb, r_tl, r_tr, r_br, r_bl, rim_c, Color{rim_c.r, rim_c.g, rim_c.b, 0.0f});
    }

    // 6. Apply lighting context if provided (ambient + directional tint)
    if (lit_context.has_value()) {
        const auto& ctx = *lit_context;
        if (ctx.enabled && (ctx.ambient_enabled || ctx.directional_enabled)) {
            // Compute approximate lit color by tinting the front face
            Color lit_tint = ctx.ambient_color * ctx.ambient +
                             ctx.directional_color * ctx.diffuse;
            lit_tint.a = 0.25f; // subtle overlay
            fill_quad(fb, f_tl, f_tr, f_br, f_bl, lit_tint);
        }
    }
}

} // namespace chronon3d::renderer
