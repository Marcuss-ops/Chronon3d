#include <chronon3d/backends/software/rasterizers/projected_card_rasterizer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d::renderer {

// ─── Triangle rasterizer (scanline) ──────────────────────────────────────────

static void fill_triangle(Framebuffer& fb, Vec2 v0, Vec2 v1, Vec2 v2, const Color& color) {
    // Sort vertices by y
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

        const int x0 = std::max(0, static_cast<int>(std::ceil(std::min(x_long, x_short))));
        const int x1 = std::min(fb.width() - 1, static_cast<int>(std::floor(std::max(x_long, x_short))));

        Color* row = fb.pixels_row(y);
        for (int x = x0; x <= x1; ++x) {
            Color& dst = row[x];
            const float inv_a = 1.0f - color.a;
            dst.r = color.r + dst.r * inv_a;
            dst.g = color.g + dst.g * inv_a;
            dst.b = color.b + dst.b * inv_a;
            dst.a = color.a + dst.a * inv_a;
        }
    }
}

// ─── Public API ──────────────────────────────────────────────────────────────

void fill_projected_card(Framebuffer& fb, const rendering::ProjectedCard& card, const Color& color) {
    if (!card.visible || color.a <= 0.0f) return;
    // TL=0, TR=1, BR=2, BL=3
    fill_triangle(fb, card.corners[0], card.corners[1], card.corners[2], color);
    fill_triangle(fb, card.corners[0], card.corners[2], card.corners[3], color);
}

void composite_projected_framebuffer(Framebuffer& dst, const Framebuffer& src,
                                     const rendering::ProjectedCard& card, float opacity,
                                     BlendMode mode) {
    if (!card.visible || opacity <= 0.0f) return;

    // Bounding box of the card in dst pixels
    float min_x = card.corners[0].x, max_x = card.corners[0].x;
    float min_y = card.corners[0].y, max_y = card.corners[0].y;
    for (int i = 1; i < 4; ++i) {
        min_x = std::min(min_x, card.corners[i].x);
        max_x = std::max(max_x, card.corners[i].x);
        min_y = std::min(min_y, card.corners[i].y);
        max_y = std::max(max_y, card.corners[i].y);
    }

    const int x0 = std::max(0, static_cast<int>(std::floor(min_x)));
    const int x1 = std::min(dst.width() - 1, static_cast<int>(std::ceil(max_x)));
    const int y0 = std::max(0, static_cast<int>(std::floor(min_y)));
    const int y1 = std::min(dst.height() - 1, static_cast<int>(std::ceil(max_y)));

    // Build a 3x3 homography from card corners (dst) ← unit square UVs (src)
    // We use bilinear interpolation instead of a full homography solve for speed.
    // For small-angle cards (no extreme perspective), bilinear is good enough.
    // For proper perspective, the TransformNode path (graph) handles this.

    const float sw = static_cast<float>(src.width());
    const float sh = static_cast<float>(src.height());

    // card corners in order: TL(0), TR(1), BR(2), BL(3)
    // UV: TL=(0,0), TR=(1,0), BR=(1,1), BL=(0,1)
    const Vec2& TL = card.corners[0];
    const Vec2& TR = card.corners[1];
    const Vec2& BR = card.corners[2];
    const Vec2& BL = card.corners[3];

    for (int y = y0; y <= y1; ++y) {
        Color* dst_row = dst.pixels_row(y);
        const float fy = static_cast<float>(y) + 0.5f;

        for (int x = x0; x <= x1; ++x) {
            const float fx = static_cast<float>(x) + 0.5f;

            // Bilinear UV interpolation: find (u,v) from (fx,fy) inside the quad
            // Using barycentric-style approach: solve for (s,t) such that
            // P = lerp(lerp(TL,TR,s), lerp(BL,BR,s), t)
            // This is an approximation valid when the quad is nearly rectangular.

            // Horizontal interpolation parameter s: project onto average horizontal axis
            const Vec2 top = {TR.x - TL.x, TR.y - TL.y};
            const Vec2 bot = {BR.x - BL.x, BR.y - BL.y};
            const float top_len2 = top.x*top.x + top.y*top.y;
            const float bot_len2 = bot.x*bot.x + bot.y*bot.y;

            // Estimate s from top edge projection
            float s = 0.5f;
            if (top_len2 > 0.1f) {
                s = ((fx - TL.x) * top.x + (fy - TL.y) * top.y) / top_len2;
            }
            s = std::clamp(s, 0.0f, 1.0f);

            // Interpolate point on top and bottom edges at s
            const Vec2 pt = {TL.x + top.x * s, TL.y + top.y * s};
            const Vec2 pb = {BL.x + bot.x * s, BL.y + bot.y * s};

            // t from vertical position between pt and pb
            const Vec2 vert = {pb.x - pt.x, pb.y - pt.y};
            const float vert_len2 = vert.x*vert.x + vert.y*vert.y;
            float t = 0.5f;
            if (vert_len2 > 0.1f) {
                t = ((fx - pt.x) * vert.x + (fy - pt.y) * vert.y) / vert_len2;
            }
            t = std::clamp(t, 0.0f, 1.0f);

            // Skip pixels outside the quad
            if (s < 0.0f || s > 1.0f || t < 0.0f || t > 1.0f) continue;

            const float sx = s * sw;
            const float sy = t * sh;

            if (sx < 0 || sx >= sw || sy < 0 || sy >= sh) continue;

            const Color src_c = src.sample(sx, sy, SamplingMode::Bilinear);
            if (src_c.a <= 0.001f) continue;

            const float final_a = src_c.a * opacity;
            Color& d = dst_row[x];

            if (mode == BlendMode::Add) {
                d.r = std::min(1.0f, d.r + src_c.r * opacity);
                d.g = std::min(1.0f, d.g + src_c.g * opacity);
                d.b = std::min(1.0f, d.b + src_c.b * opacity);
                d.a = std::min(1.0f, d.a + final_a);
            } else {
                const float inv_a = 1.0f - final_a;
                d.r = src_c.r * opacity + d.r * inv_a;
                d.g = src_c.g * opacity + d.g * inv_a;
                d.b = src_c.b * opacity + d.b * inv_a;
                d.a = final_a + d.a * inv_a;
            }
        }
    }
}

} // namespace chronon3d::renderer
