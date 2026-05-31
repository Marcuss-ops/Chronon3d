// blend2d_bridge_core.cpp
// Core helpers: pixel_passes_mask, bilinear sampling, basic compositing.
// Transformed variants live in blend2d_bridge_transforms.cpp.

#include "blend2d_bridge.hpp"
#include <chronon3d/scene/mask/mask_utils.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <algorithm>
#include <cmath>

namespace chronon3d::blend2d_bridge {

// ── mask helper ────────────────────────────────────────────────────

bool pixel_passes_mask(const RenderState& state, i32 x, i32 y) {
    if (!state.mask || !state.mask->enabled()) return true;
    if (state.mask_alpha_cache && y >= 0 && y < state.mask_alpha_cache->height() &&
        x >= 0 && x < state.mask_alpha_cache->width()) {
        return state.mask_alpha_cache->get_pixel(x, y).a > 0.0f;
    }
    Vec4 local = state.layer_inv_matrix * Vec4(static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f, 0.0f, 1.0f);
    return mask_contains_local_point(*state.mask, Vec2{local.x, local.y});
}

// ── bilinear sampling ──────────────────────────────────────────────

void sample_bilinear_prgb32(const uint32_t* base, int stride, int sw, int sh, float lx, float ly, float& r, float& g, float& b, float& a) {
    const float u = lx - 0.5f;
    const float v = ly - 0.5f;
    int x0 = static_cast<int>(std::floor(u));
    int y0 = static_cast<int>(std::floor(v));
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    const float tx = u - static_cast<float>(x0);
    const float ty = v - static_cast<float>(y0);

    x0 = std::clamp(x0, 0, sw - 1);
    y0 = std::clamp(y0, 0, sh - 1);
    x1 = std::clamp(x1, 0, sw - 1);
    y1 = std::clamp(y1, 0, sh - 1);

    constexpr float inv255 = 1.0f / 255.0f;

    auto unpack = [&](uint32_t p, float& cr, float& cg, float& cb, float& ca) {
        ca = ((p >> 24) & 0xFF) * inv255;
        cr = ((p >> 16) & 0xFF) * inv255;
        cg = ((p >> 8) & 0xFF) * inv255;
        cb = (p & 0xFF) * inv255;
    };

    float r00, g00, b00, a00; unpack(base[y0 * stride + x0], r00, g00, b00, a00);
    float r10, g10, b10, a10; unpack(base[y0 * stride + x1], r10, g10, b10, a10);
    float r01, g01, b01, a01; unpack(base[y1 * stride + x0], r01, g01, b01, a01);
    float r11, g11, b11, a11; unpack(base[y1 * stride + x1], r11, g11, b11, a11);

    const float w0 = (1.0f - tx) * (1.0f - ty);
    const float w1 = tx * (1.0f - ty);
    const float w2 = (1.0f - tx) * ty;
    const float w3 = tx * ty;

    r = r00 * w0 + r10 * w1 + r01 * w2 + r11 * w3;
    g = g00 * w0 + g10 * w1 + g01 * w2 + g11 * w3;
    b = b00 * w0 + b10 * w1 + b01 * w2 + b11 * w3;
    a = a00 * w0 + a10 * w1 + a01 * w2 + a11 * w3;
}

void sample_bilinear_float(const Color* base, int stride, int sw, int sh, float lx, float ly, Color& result) {
    const float u = lx - 0.5f;
    const float v = ly - 0.5f;
    int x0 = static_cast<int>(std::floor(u));
    int y0 = static_cast<int>(std::floor(v));
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    const float tx = u - static_cast<float>(x0);
    const float ty = v - static_cast<float>(y0);

    x0 = std::clamp(x0, 0, sw - 1);
    y0 = std::clamp(y0, 0, sh - 1);
    x1 = std::clamp(x1, 0, sw - 1);
    y1 = std::clamp(y1, 0, sh - 1);

    const Color& c00 = base[y0 * stride + x0];
    const Color& c10 = base[y0 * stride + x1];
    const Color& c01 = base[y1 * stride + x0];
    const Color& c11 = base[y1 * stride + x1];

    const float w0 = (1.0f - tx) * (1.0f - ty);
    const float w1 = tx * (1.0f - ty);
    const float w2 = (1.0f - tx) * ty;
    const float w3 = tx * ty;

    result.r = c00.r * w0 + c10.r * w1 + c01.r * w2 + c11.r * w3;
    result.g = c00.g * w0 + c10.g * w1 + c01.g * w2 + c11.g * w3;
    result.b = c00.b * w0 + c10.b * w1 + c01.b * w2 + c11.b * w3;
    result.a = c00.a * w0 + c10.a * w1 + c01.a * w2 + c11.a * w3;
}

// ── basic compositing ──────────────────────────────────────────────

void composite_bl_image(Framebuffer& fb, const BLImage& img, int x, int y, float opacity, BlendMode mode, const RenderState* state) {
    BLImageData data;
    if (img.getData(&data) != BL_SUCCESS) return;

    const uint32_t* src_pixels = reinterpret_cast<const uint32_t*>(data.pixelData);
    const int sw = data.size.w;
    const int sh = data.size.h;
    const int stride = static_cast<int>(data.stride / sizeof(uint32_t));

    const float inv255 = 1.0f / 255.0f;
    const bool has_mask = state && state->mask && state->mask->enabled();
    if (has_mask) {
        ensure_mask_alpha_cache(*state, fb.width(), fb.height());
    }

    auto process_rows = [&](int row_begin, int row_end) {
        if (has_mask) {
            for (int iy = row_begin; iy < row_end; ++iy) {
                const int py = y + iy;
                if (py < 0 || py >= fb.height()) continue;

                Color* dst_row = fb.pixels_row(py);
                const uint32_t* src_row = src_pixels + (iy * stride);

                for (int ix = 0; ix < sw; ++ix) {
                    const int px = x + ix;
                    if (px < 0 || px >= fb.width()) continue;
                    if (!pixel_passes_mask(*state, px, py)) continue;

                    const uint32_t p = src_row[ix];
                    const float sa = ((p >> 24) & 0xFF) * inv255 * opacity;
                    if (sa <= 0.001f) continue;

                    const float sr = ((p >> 16) & 0xFF) * inv255 * opacity;
                    const float sg = ((p >> 8) & 0xFF) * inv255 * opacity;
                    const float sb = (p & 0xFF) * inv255 * opacity;

                    Color& dst = dst_row[px];
                    if (mode == BlendMode::Add) {
                        dst.r += sr;
                        dst.g += sg;
                        dst.b += sb;
                        dst.a += sa;
                    } else {
                        const float inv_sa = 1.0f - sa;
                        dst.r = sr + dst.r * inv_sa;
                        dst.g = sg + dst.g * inv_sa;
                        dst.b = sb + dst.b * inv_sa;
                        dst.a = sa + dst.a * inv_sa;
                    }
                }
            }
        } else {
            for (int iy = row_begin; iy < row_end; ++iy) {
                const int py = y + iy;
                if (py < 0 || py >= fb.height()) continue;

                Color* dst_row = fb.pixels_row(py);
                const uint32_t* src_row = src_pixels + (iy * stride);

                for (int ix = 0; ix < sw; ++ix) {
                    const int px = x + ix;
                    if (px < 0 || px >= fb.width()) continue;

                    const uint32_t p = src_row[ix];
                    const float sa = ((p >> 24) & 0xFF) * inv255 * opacity;
                    if (sa <= 0.001f) continue;

                    const float sr = ((p >> 16) & 0xFF) * inv255 * opacity;
                    const float sg = ((p >> 8) & 0xFF) * inv255 * opacity;
                    const float sb = (p & 0xFF) * inv255 * opacity;

                    Color& dst = dst_row[px];
                    if (mode == BlendMode::Add) {
                        dst.r = std::min(dst.r + sr, 1.0f);
                        dst.g = std::min(dst.g + sg, 1.0f);
                        dst.b = std::min(dst.b + sb, 1.0f);
                        dst.a = std::min(dst.a + sa, 1.0f);
                    } else {
                        const float inv_sa = 1.0f - sa;
                        dst.r = sr + dst.r * inv_sa;
                        dst.g = sg + dst.g * inv_sa;
                        dst.b = sb + dst.b * inv_sa;
                        dst.a = sa + dst.a * inv_sa;
                    }
                }
            }
        }
    };

    if (sh >= 16) {
        tbb::parallel_for(tbb::blocked_range<int>(0, sh), [&](const tbb::blocked_range<int>& range) {
            process_rows(range.begin(), range.end());
        });
    } else {
        process_rows(0, sh);
    }
}

void composite_framebuffer(Framebuffer& dst_fb, const Framebuffer& src_fb, int x, int y, float opacity, BlendMode mode, const RenderState* state) {
    const int sw = src_fb.width();
    const int sh = src_fb.height();
    if (state && state->mask && state->mask->enabled()) {
        ensure_mask_alpha_cache(*state, dst_fb.width(), dst_fb.height());
    }

    auto process_rows = [&](int row_begin, int row_end) {
        for (int iy = row_begin; iy < row_end; ++iy) {
            const int py = y + iy;
            if (py < 0 || py >= dst_fb.height()) continue;

            const Color* src_row = src_fb.pixels_row(iy);
            Color* dst_row = dst_fb.pixels_row(py);

            for (int ix = 0; ix < sw; ++ix) {
                const int px = x + ix;
                if (px < 0 || px >= dst_fb.width()) continue;
                if (state && !pixel_passes_mask(*state, px, py)) continue;

                Color src = src_row[ix];
                src.r *= opacity;
                src.g *= opacity;
                src.b *= opacity;
                src.a *= opacity;
                if (src.a <= 0.001f) continue;

                    Color& dst = dst_row[px];
                    if (mode == BlendMode::Add) {
                        dst.r += src.r;
                        dst.g += src.g;
                        dst.b += src.b;
                        dst.a += src.a;
                    } else {
                        const float inv_sa = 1.0f - src.a;
                        dst.r = src.r + dst.r * inv_sa;
                    dst.g = src.g + dst.g * inv_sa;
                    dst.b = src.b + dst.b * inv_sa;
                    dst.a = src.a + dst.a * inv_sa;
                }
            }
        }
    };

    if (sh >= 16) {
        tbb::parallel_for(tbb::blocked_range<int>(0, sh), [&](const tbb::blocked_range<int>& range) {
            process_rows(range.begin(), range.end());
        });
    } else {
        process_rows(0, sh);
    }
}

} // namespace chronon3d::blend2d_bridge
