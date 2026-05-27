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

namespace {

[[nodiscard]] inline float wrap_repeat(float value, float period) {
    if (period <= 0.0f) return 0.0f;
    value -= period * std::floor(value / period);
    if (value < 0.0f) value += period;
    return value;
}

} // anonymous namespace

// ── BLImage transformed ────────────────────────────────────────────

void composite_bl_image_transformed(Framebuffer& fb, const BLImage& img, const Mat4& model, float opacity, BlendMode mode, const RenderState* state) {
    BLImageData data;
    if (img.getData(&data) != BL_SUCCESS) return;

    const uint32_t* src_pixels = reinterpret_cast<const uint32_t*>(data.pixelData);
    const int sw = data.size.w;
    const int sh = data.size.h;
    const int stride = static_cast<int>(data.stride / sizeof(uint32_t));

    // Fast path: simple translation
    if (detail::is_simple_translation(model)) {
        int tx_val = static_cast<int>(std::round(model[3][0]));
        int ty_val = static_cast<int>(std::round(model[3][1]));
        composite_bl_image(fb, img, tx_val, ty_val, opacity, mode, state);
        return;
    }

    const float sx = model[0][0];
    const float sy = model[1][1];
    const float tx = model[3][0];
    const float ty = model[3][1];

    // Fast path: scale + translation
    if (detail::is_scale_translation(model)) {
        if (state && state->mask && state->mask->enabled()) {
            ensure_mask_alpha_cache(*state, fb.width(), fb.height());
        }

        float min_x = tx;
        float max_x = static_cast<float>(sw) * sx + tx;
        if (sx < 0.0f) std::swap(min_x, max_x);

        float min_y = ty;
        float max_y = static_cast<float>(sh) * sy + ty;
        if (sy < 0.0f) std::swap(min_y, max_y);

        const int x0_st = std::max<int>(0, static_cast<int>(std::floor(min_x)));
        const int y0_st = std::max<int>(0, static_cast<int>(std::floor(min_y)));
        const int x1_st = std::min<int>(fb.width(),  static_cast<int>(std::ceil(max_x)));
        const int y1_st = std::min<int>(fb.height(), static_cast<int>(std::ceil(max_y)));

        if (x0_st < x1_st && y0_st < y1_st) {
            const float inv_sx = 1.0f / sx;
            const float inv_sy = 1.0f / sy;

            auto process_rows_st = [&](int row_begin, int row_end) {
                for (int y = row_begin; y < row_end; ++y) {
                    Color* dst_row = fb.pixels_row(y);
                    const float ly = (static_cast<float>(y) + 0.5f - ty) * inv_sy;
                    if (ly < 0.0f || ly >= static_cast<float>(sh)) continue;

                    for (int x = x0_st; x < x1_st; ++x) {
                        if (state && !pixel_passes_mask(*state, x, y)) continue;

                        const float lx = (static_cast<float>(x) + 0.5f - tx) * inv_sx;
                        if (lx < 0.0f || lx >= static_cast<float>(sw)) continue;

                        float sr, sg, sb, sa;
                        sample_bilinear_prgb32(src_pixels, stride, sw, sh, lx, ly, sr, sg, sb, sa);
                        sa *= opacity;
                        if (sa <= 0.001f) continue;
                        sr *= opacity;
                        sg *= opacity;
                        sb *= opacity;

                        detail::blend_pixel(dst_row[x], Color{sr, sg, sb, sa}, mode);
                    }
                }
            };

            if (y1_st - y0_st >= 16) {
                tbb::parallel_for(tbb::blocked_range<int>(y0_st, y1_st), [&](const tbb::blocked_range<int>& range) {
                    process_rows_st(range.begin(), range.end());
                });
            } else {
                process_rows_st(y0_st, y1_st);
            }
        }
        return;
    }

    // Full projective path
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

                float sr, sg, sb, sa;
                sample_bilinear_prgb32(src_pixels, stride, sw, sh, lx, ly, sr, sg, sb, sa);
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

    std::optional<detail::TransformInfo> ti;
    if (!simple_translation && !scale_translation) {
        ti.emplace(model);
    }

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

                lx = wrap_repeat(lx, static_cast<float>(sw));
                ly = wrap_repeat(ly, static_cast<float>(sh));

                float sr, sg, sb, sa;
                sample_bilinear_prgb32(src_pixels, stride, sw, sh, lx, ly, sr, sg, sb, sa);
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
