// blend2d_bridge_transforms.cpp
// Transformed / tiled compositing for BLImage → Framebuffer.
// Core helpers (sampling, basic compositing, pixel_passes_mask) live in blend2d_bridge_core.cpp.
// Framebuffer → Framebuffer transforms live in blend2d_bridge_transforms_fb.cpp.

#include "blend2d_bridge.hpp"
#include "blend2d_bridge_detail.hpp"
#include <chronon3d/scene/mask/mask_utils.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <algorithm>
#include <cmath>

namespace chronon3d::blend2d_bridge {

// ── BLImage transformed ────────────────────────────────────────────

void composite_bl_image_transformed(Framebuffer& fb, const BLImage& img, const Mat4& model, float opacity, BlendMode mode, const RenderState* state, float radius) {
    BLImageData data;
    if (img.getData(&data) != BL_SUCCESS) return;

    const uint32_t* src_pixels = reinterpret_cast<const uint32_t*>(data.pixelData);
    const int sw = data.size.w;
    const int sh = data.size.h;
    const int stride = static_cast<int>(data.stride / sizeof(uint32_t));

    // Fast path: simple translation
    if (radius <= 0.0f && detail::is_simple_translation(model)) {
        int tx_val = static_cast<int>(std::round(model[3][0]));
        int ty_val = static_cast<int>(std::round(model[3][1]));
        composite_bl_image(fb, img, tx_val, ty_val, opacity, mode, state);
        return;
    }

    const float sx = model[0][0];
    const float sy = model[1][1];
    const float tx = model[3][0];
    const float ty = model[3][1];
    const bool downscale_x = std::abs(sx) < 0.999f;
    const bool downscale_y = std::abs(sy) < 0.999f;
    const float sample_ox = downscale_x ? 0.25f / std::max(std::abs(sx), 1e-6f) : 0.0f;
    const float sample_oy = downscale_y ? 0.25f / std::max(std::abs(sy), 1e-6f) : 0.0f;

    auto sample_source = [&](float lx, float ly, float& sr, float& sg, float& sb, float& sa) {
        if (!downscale_x && !downscale_y) {
            sample_bilinear_prgb32(src_pixels, stride, sw, sh, lx, ly, sr, sg, sb, sa);
            return;
        }

        const float sx0 = downscale_x ? (lx - sample_ox) : lx;
        const float sx1 = downscale_x ? (lx + sample_ox) : lx;
        const float sy0 = downscale_y ? (ly - sample_oy) : ly;
        const float sy1 = downscale_y ? (ly + sample_oy) : ly;

        float r0, g0, b0, a0;
        float r1, g1, b1, a1;
        float r2, g2, b2, a2;
        float r3, g3, b3, a3;
        sample_bilinear_prgb32(src_pixels, stride, sw, sh, sx0, sy0, r0, g0, b0, a0);
        sample_bilinear_prgb32(src_pixels, stride, sw, sh, sx1, sy0, r1, g1, b1, a1);
        sample_bilinear_prgb32(src_pixels, stride, sw, sh, sx0, sy1, r2, g2, b2, a2);
        sample_bilinear_prgb32(src_pixels, stride, sw, sh, sx1, sy1, r3, g3, b3, a3);

        sr = (r0 + r1 + r2 + r3) * 0.25f;
        sg = (g0 + g1 + g2 + g3) * 0.25f;
        sb = (b0 + b1 + b2 + b3) * 0.25f;
        sa = (a0 + a1 + a2 + a3) * 0.25f;
    };

    // Full projective / affine path.
    // We keep the dedicated simple-translation fast path above, but all
    // scaled image draws go through the same sampler to avoid path-specific
    // artifacts.
    if (state && state->mask && state->mask->enabled()) {
        ensure_mask_alpha_cache(*state, fb.width(), fb.height());
    }

    const detail::TransformInfo ti(model);

    int x0, y0, x1, y1;
    detail::get_projective_bounds(model, static_cast<float>(sw), static_cast<float>(sh), fb.width(), fb.height(), x0, y0, x1, y1);
    if (x0 >= x1 || y0 >= y1) return;

    auto process_rows = [&](int row_begin, int row_end) {
        for (int y = row_begin; y < row_end; ++y) {
            Color* dst_row = fb.pixels_row(y);
            for (int x = x0; x < x1; ++x) {
                if (state && !pixel_passes_mask(*state, x, y)) continue;

                float lx = 0.0f;
                float ly = 0.0f;
                if (!ti.map(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, lx, ly)) continue;

                if (lx < 0.0f || ly < 0.0f || lx >= static_cast<float>(sw) || ly >= static_cast<float>(sh)) continue;

                if (radius > 0.0f) {
                    const float r = std::max(0.0f, std::min({radius, static_cast<float>(sw) * 0.5f, static_cast<float>(sh) * 0.5f}));
                    if (r > 0.0f) {
                        if (lx < r && ly < r) {
                            const float dx = lx - r; const float dy = ly - r;
                            if ((dx * dx + dy * dy) > r * r) continue;
                        } else if (lx > sw - r && ly < r) {
                            const float dx = lx - (sw - r); const float dy = ly - r;
                            if ((dx * dx + dy * dy) > r * r) continue;
                        } else if (lx < r && ly > sh - r) {
                            const float dx = lx - r; const float dy = ly - (sh - r);
                            if ((dx * dx + dy * dy) > r * r) continue;
                        } else if (lx > sw - r && ly > sh - r) {
                            const float dx = lx - (sw - r); const float dy = ly - (sh - r);
                            if ((dx * dx + dy * dy) > r * r) continue;
                        }
                    }
                }

                float sr, sg, sb, sa;
                sample_source(lx, ly, sr, sg, sb, sa);
                sa *= opacity;
                if (sa <= 0.001f) continue;
                sr *= opacity;
                sg *= opacity;
                sb *= opacity;

                detail::blend_pixel(dst_row[x], Color{sr, sg, sb, sa}, mode);
            }
        }
    };

    if (y1 - y0 >= 16) {
        tbb::parallel_for(tbb::blocked_range<int>(y0, y1), [&](const tbb::blocked_range<int>& range) {
            process_rows(range.begin(), range.end());
        });
    } else {
        process_rows(y0, y1);
    }
}

// ── BLImage tiled ──────────────────────────────────────────────────

void composite_bl_image_tiled(Framebuffer& fb, const BLImage& img, const Mat4& model, float opacity, BlendMode mode, const RenderState* state) {
    BLImageData data;
    if (img.getData(&data) != BL_SUCCESS) return;

    const uint32_t* src_pixels = reinterpret_cast<const uint32_t*>(data.pixelData);
    const int sw = data.size.w;
    const int sh = data.size.h;
    const int stride = static_cast<int>(data.stride / sizeof(uint32_t));

    if (state && state->mask && state->mask->enabled()) {
        ensure_mask_alpha_cache(*state, fb.width(), fb.height());
    }

    int x0 = 0;
    int y0 = 0;
    int x1 = fb.width();
    int y1 = fb.height();
    if (state && state->clip_rect) {
        x0 = std::max(0, state->clip_rect->x0);
        y0 = std::max(0, state->clip_rect->y0);
        x1 = std::min(fb.width(), state->clip_rect->x1);
        y1 = std::min(fb.height(), state->clip_rect->y1);
    }

    if (x1 <= x0 || y1 <= y0) return;

    const bool simple_translation = detail::is_simple_translation(model);
    const bool scale_translation = detail::is_scale_translation(model);

    const float sx = model[0][0];
    const float sy = model[1][1];
    const float tx = model[3][0];
    const float ty = model[3][1];
    const bool downscale_x = std::abs(sx) < 0.999f;
    const bool downscale_y = std::abs(sy) < 0.999f;
    const float sample_ox = downscale_x ? 0.25f / std::max(std::abs(sx), 1e-6f) : 0.0f;
    const float sample_oy = downscale_y ? 0.25f / std::max(std::abs(sy), 1e-6f) : 0.0f;

    std::optional<detail::TransformInfo> ti;
    if (!simple_translation && !scale_translation) {
        ti.emplace(model);
    }

    auto sample_source = [&](float lx, float ly, float& sr, float& sg, float& sb, float& sa) {
        if (!downscale_x && !downscale_y) {
            sample_bilinear_prgb32(src_pixels, stride, sw, sh, lx, ly, sr, sg, sb, sa);
            return;
        }

        const float sx0 = downscale_x ? (lx - sample_ox) : lx;
        const float sx1 = downscale_x ? (lx + sample_ox) : lx;
        const float sy0 = downscale_y ? (ly - sample_oy) : ly;
        const float sy1 = downscale_y ? (ly + sample_oy) : ly;

        float r0, g0, b0, a0;
        float r1, g1, b1, a1;
        float r2, g2, b2, a2;
        float r3, g3, b3, a3;
        sample_bilinear_prgb32(src_pixels, stride, sw, sh, sx0, sy0, r0, g0, b0, a0);
        sample_bilinear_prgb32(src_pixels, stride, sw, sh, sx1, sy0, r1, g1, b1, a1);
        sample_bilinear_prgb32(src_pixels, stride, sw, sh, sx0, sy1, r2, g2, b2, a2);
        sample_bilinear_prgb32(src_pixels, stride, sw, sh, sx1, sy1, r3, g3, b3, a3);

        sr = (r0 + r1 + r2 + r3) * 0.25f;
        sg = (g0 + g1 + g2 + g3) * 0.25f;
        sb = (b0 + b1 + b2 + b3) * 0.25f;
        sa = (a0 + a1 + a2 + a3) * 0.25f;
    };

    auto process_rows = [&](int row_begin, int row_end) {
        for (int y = row_begin; y < row_end; ++y) {
            Color* dst_row = fb.pixels_row(y);
            for (int x = x0; x < x1; ++x) {
                if (state && !pixel_passes_mask(*state, x, y)) continue;

                float lx = 0.0f;
                float ly = 0.0f;

                if (simple_translation) {
                    lx = static_cast<float>(x) + 0.5f - tx;
                    ly = static_cast<float>(y) + 0.5f - ty;
                } else if (scale_translation) {
                    const float inv_sx = 1.0f / sx;
                    const float inv_sy = 1.0f / sy;
                    lx = (static_cast<float>(x) + 0.5f - tx) * inv_sx;
                    ly = (static_cast<float>(y) + 0.5f - ty) * inv_sy;
                } else if (ti->is_affine) {
                    const float px = static_cast<float>(x) + 0.5f;
                    const float py = static_cast<float>(y) + 0.5f;
                    lx = ti->a00 * px + ti->a10 * py + ti->a20;
                    ly = ti->a01 * px + ti->a11 * py + ti->a21;
                } else {
                    const Vec3 local_h = ti->invH * Vec3(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, 1.0f);
                    if (std::abs(local_h.z) < 1e-7f) continue;
                    lx = local_h.x / local_h.z;
                    ly = local_h.y / local_h.z;
                }

                float sr, sg, sb, sa;
                sample_source(lx, ly, sr, sg, sb, sa);
                sa *= opacity;
                if (sa <= 0.001f) continue;
                sr *= opacity;
                sg *= opacity;
                sb *= opacity;

                detail::blend_pixel(dst_row[x], Color{sr, sg, sb, sa}, mode);
            }
        }
    };

    if (y1 - y0 >= 16) {
        tbb::parallel_for(tbb::blocked_range<int>(y0, y1), [&](const tbb::blocked_range<int>& range) {
            process_rows(range.begin(), range.end());
        });
    } else {
        process_rows(y0, y1);
    }
}

} // namespace chronon3d::blend2d_bridge
