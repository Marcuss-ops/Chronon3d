#include <chronon3d/backends/software/rasterizers/projected_card_rasterizer.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d::renderer {

// ─── Triangle rasterizer with per-pixel depth test ───────────────────────────
// Interpolates Z across the triangle for depth comparison.
// When depth_buffer is empty, behaves as before (no depth testing).

static void fill_triangle_depth(
    Framebuffer& fb,
    Vec3 v0, Vec3 v1, Vec3 v2,
    const Color& color,
    std::span<float> depth_buffer
) {
    // Sort vertices by y
    if (v0.y > v1.y) std::swap(v0, v1);
    if (v1.y > v2.y) std::swap(v1, v2);
    if (v0.y > v1.y) std::swap(v0, v1);

    const int y0 = std::max(0, static_cast<int>(std::ceil(v0.y)));
    const int y2 = std::min(fb.height() - 1, static_cast<int>(std::floor(v2.y)));
    if (y0 > y2) return;

    const float h = v2.y - v0.y;
    if (h < 0.5f) return;

    const i32 fb_w = fb.width();
    const bool use_depth = depth_buffer.size() == static_cast<usize>(fb_w) * fb.height();

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

        Color* row = fb.pixels_row(y);
        const float* db_row = use_depth ? &depth_buffer[static_cast<size_t>(y) * fb_w] : nullptr;

        for (int x = x0; x <= x1; ++x) {
            // Guard: skip NaN/Inf source color to prevent framebuffer contamination.
            if (std::isnan(color.r) || std::isnan(color.g) || std::isnan(color.b) || std::isnan(color.a) ||
                std::isinf(color.r) || std::isinf(color.g) || std::isinf(color.b) || std::isinf(color.a)) {
                continue;
            }

            // Interpolate depth at this pixel
            const float pixel_z = z_start + dz_dx * (static_cast<float>(x) - x_start);

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
}

// Old scanline fill without depth (used when no depth buffer provided)
static void fill_triangle_flat(Framebuffer& fb, Vec2 v0, Vec2 v1, Vec2 v2, const Color& color) {
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
            if (std::isnan(color.r) || std::isnan(color.g) || std::isnan(color.b) || std::isnan(color.a) ||
                std::isinf(color.r) || std::isinf(color.g) || std::isinf(color.b) || std::isinf(color.a)) {
                continue;
            }
            const float inv_a = 1.0f - color.a;
            dst.r = color.r + dst.r * inv_a;
            dst.g = color.g + dst.g * inv_a;
            dst.b = color.b + dst.b * inv_a;
            dst.a = color.a + dst.a * inv_a;
        }
    }
}

// ─── Public API ──────────────────────────────────────────────────────────────

void fill_projected_card(
    Framebuffer& fb,
    const rendering::ProjectedCard& card,
    const Color& color,
    std::span<float> depth_buffer
) {
    if (!card.visible || color.a <= 0.0f) return;
    // TL=0, TR=1, BR=2, BL=3

    if (depth_buffer.empty()) {
        // Legacy path: no depth test
        fill_triangle_flat(fb, {card.corners[0].x, card.corners[0].y},
                                {card.corners[1].x, card.corners[1].y},
                                {card.corners[2].x, card.corners[2].y}, color);
        fill_triangle_flat(fb, {card.corners[0].x, card.corners[0].y},
                                {card.corners[2].x, card.corners[2].y},
                                {card.corners[3].x, card.corners[3].y}, color);
    } else {
        // Depth-tested path
        fill_triangle_depth(fb, card.corners[0], card.corners[1], card.corners[2], color, depth_buffer);
        fill_triangle_depth(fb, card.corners[0], card.corners[2], card.corners[3], color, depth_buffer);
    }
}

void composite_projected_framebuffer(Framebuffer& dst, const Framebuffer& src,
                                     const rendering::ProjectedCard& card, float opacity,
                                     BlendMode mode, std::span<float> depth_buffer) {
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

    const i32 fb_w = dst.width();
    const i32 fb_h = dst.height();
    const bool use_depth = depth_buffer.size() == static_cast<usize>(fb_w) * fb_h;

    // Build a 3x3 homography from card corners (dst) ← unit square UVs (src)
    const float sw = static_cast<float>(src.width());
    const float sh = static_cast<float>(src.height());

    // card corners in order: TL(0), TR(1), BR(2), BL(3)
    const Vec2 TL{card.corners[0].x, card.corners[0].y};
    const Vec2 TR{card.corners[1].x, card.corners[1].y};
    const Vec2 BR{card.corners[2].x, card.corners[2].y};
    const Vec2 BL{card.corners[3].x, card.corners[3].y};

    const Vec2 src_pts[4] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f},
    };
    const Vec2 dst_pts[4] = {TL, TR, BR, BL};
    glm::mat3 homography{};
    if (!solve_homography_4pt(src_pts, dst_pts, homography)) {
        return;
    }

    const float det = glm::determinant(homography);
    if (std::abs(det) < 1e-8f) {
        return;
    }
    const glm::mat3 inv_h = glm::inverse(homography);

    const float swm1 = std::max(1.0f, sw - 1.0f);
    const float shm1 = std::max(1.0f, sh - 1.0f);

    // Per-corner depths for bilinear interpolation
    const float z_TL = card.corners[0].z;
    const float z_TR = card.corners[1].z;
    const float z_BR = card.corners[2].z;
    const float z_BL = card.corners[3].z;

    for (int y = y0; y <= y1; ++y) {
        Color* dst_row = dst.pixels_row(y);
        const float fy = static_cast<float>(y) + 0.5f;
        const float* db_row = use_depth ? &depth_buffer[static_cast<size_t>(y) * fb_w] : nullptr;

        for (int x = x0; x <= x1; ++x) {
            const float fx = static_cast<float>(x) + 0.5f;

            const Vec3 uvw = inv_h * Vec3{fx, fy, 1.0f};
            if (std::abs(uvw.z) < 1e-8f) {
                continue;
            }

            const float u = uvw.x / uvw.z;
            const float v = uvw.y / uvw.z;
            if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
                continue;
            }

            // Interpolate depth using bilinear interpolation over the quad
            const float pixel_z = (1.0f - u) * (1.0f - v) * z_TL
                                +       u  * (1.0f - v) * z_TR
                                +       u  *       v  * z_BR
                                + (1.0f - u) *       v  * z_BL;

            // Depth test: only composite if closer than existing depth.
            // existing > 0.0f → valid depth (camera-space Z is always positive).
            // existing <= 0.0f → uninitialized (pass through).
            if (use_depth) {
                const float existing = db_row[x];
                if (existing > 0.0f && pixel_z >= existing) continue;
                const_cast<float*>(db_row)[x] = pixel_z;
            }

            const float sx = (sw > 1) ? (u * swm1 + 0.5f) : 0.5f;
            const float sy = (sh > 1) ? (v * shm1 + 0.5f) : 0.5f;

            const Color src_c = src.sample(sx, sy, SamplingMode::Bilinear);
            if (src_c.a <= 0.001f) continue;
            if (std::isnan(src_c.r) || std::isnan(src_c.g) || std::isnan(src_c.b) || std::isnan(src_c.a) ||
                std::isinf(src_c.r) || std::isinf(src_c.g) || std::isinf(src_c.b) || std::isinf(src_c.a)) {
                continue;
            }

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
