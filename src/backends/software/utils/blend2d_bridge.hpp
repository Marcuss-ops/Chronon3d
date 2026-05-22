#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <blend2d.h>

#include <chronon3d/scene/layer/render_node.hpp>
#include <chronon3d/scene/mask/mask_utils.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace chronon3d::blend2d_bridge {

inline bool pixel_passes_mask(const RenderState& state, i32 x, i32 y) {
    if (!state.mask || !state.mask->enabled()) return true;
    if (state.mask_alpha_cache && y >= 0 && y < state.mask_alpha_cache->height() &&
        x >= 0 && x < state.mask_alpha_cache->width()) {
        return state.mask_alpha_cache->get_pixel(x, y).a > 0.0f;
    }
    Vec4 local = state.layer_inv_matrix * Vec4(static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f, 0.0f, 1.0f);
    return mask_contains_local_point(*state.mask, Vec2{local.x, local.y});
}

inline void sample_bilinear_prgb32(const uint32_t* base, int stride, int sw, int sh, float lx, float ly, float& r, float& g, float& b, float& a) {
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

inline void sample_bilinear_float(const Color* base, int stride, int sw, int sh, float lx, float ly, Color& result) {
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

inline void composite_bl_image(Framebuffer& fb, const BLImage& img, int x, int y, float opacity, BlendMode mode, const RenderState* state = nullptr) {
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

inline void composite_framebuffer(Framebuffer& dst_fb, const Framebuffer& src_fb, int x, int y, float opacity, BlendMode mode, const RenderState* state = nullptr) {
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
                    dst.r = std::min(dst.r + src.r, 1.0f);
                    dst.g = std::min(dst.g + src.g, 1.0f);
                    dst.b = std::min(dst.b + src.b, 1.0f);
                    dst.a = std::min(dst.a + src.a, 1.0f);
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

inline void composite_bl_image_transformed(Framebuffer& fb, const BLImage& img, const Mat4& model, float opacity, BlendMode mode, const RenderState* state = nullptr) {
    BLImageData data;
    if (img.getData(&data) != BL_SUCCESS) return;

    const uint32_t* src_pixels = reinterpret_cast<const uint32_t*>(data.pixelData);
    const int sw = data.size.w;
    const int sh = data.size.h;
    const int stride = static_cast<int>(data.stride / sizeof(uint32_t));

    const bool is_simple_translation =
        std::abs(model[0][0] - 1.0f) < 1e-5f && std::abs(model[0][1]) < 1e-5f && std::abs(model[0][3]) < 1e-5f &&
        std::abs(model[1][0]) < 1e-5f && std::abs(model[1][1] - 1.0f) < 1e-5f && std::abs(model[1][3]) < 1e-5f &&
        std::abs(model[3][3] - 1.0f) < 1e-5f;

    const float sx = model[0][0];
    const float sy = model[1][1];
    const float tx = model[3][0];
    const float ty = model[3][1];

    const bool is_scale_translation =
        std::abs(sx) > 1e-5f && std::abs(sy) > 1e-5f &&
        std::abs(model[0][1]) < 1e-5f && std::abs(model[0][3]) < 1e-5f &&
        std::abs(model[1][0]) < 1e-5f && std::abs(model[1][3]) < 1e-5f &&
        std::abs(model[3][3] - 1.0f) < 1e-5f;

    if (is_simple_translation) {
        int tx_val = static_cast<int>(std::round(model[3][0]));
        int ty_val = static_cast<int>(std::round(model[3][1]));
        composite_bl_image(fb, img, tx_val, ty_val, opacity, mode, state);
        return;
    }

    if (is_scale_translation) {
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

                        Color& dst = dst_row[x];
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

    if (state && state->mask && state->mask->enabled()) {
        ensure_mask_alpha_cache(*state, fb.width(), fb.height());
    }

    glm::mat3 H;
    H[0][0] = model[0][0]; H[0][1] = model[0][1]; H[0][2] = model[0][3];
    H[1][0] = model[1][0]; H[1][1] = model[1][1]; H[1][2] = model[1][3];
    H[2][0] = model[3][0]; H[2][1] = model[3][1]; H[2][2] = model[3][3];

    glm::mat3 invH = glm::inverse(H);

    auto project = [&](float lx, float ly) -> Vec2 {
        float w = model[0][3] * lx + model[1][3] * ly + model[3][3];
        if (std::abs(w) < 1e-7f) return Vec2(0);
        float sx = (model[0][0] * lx + model[1][0] * ly + model[3][0]) / w;
        float sy = (model[0][1] * lx + model[1][1] * ly + model[3][1]) / w;
        return Vec2(sx, sy);
    };

    Vec2 corners[4] = {
        project(0, 0),
        project(static_cast<float>(sw), 0),
        project(static_cast<float>(sw), static_cast<float>(sh)),
        project(0, static_cast<float>(sh))
    };

    float min_x = corners[0].x, max_x = corners[0].x;
    float min_y = corners[0].y, max_y = corners[0].y;
    for (int i = 1; i < 4; ++i) {
        min_x = std::min(min_x, corners[i].x); max_x = std::max(max_x, corners[i].x);
        min_y = std::min(min_y, corners[i].y); max_y = std::max(max_y, corners[i].y);
    }

    const int x0 = std::max<int>(0, static_cast<int>(std::floor(min_x)));
    const int y0 = std::max<int>(0, static_cast<int>(std::floor(min_y)));
    const int x1 = std::min<int>(fb.width(),  static_cast<int>(std::ceil(max_x)));
    const int y1 = std::min<int>(fb.height(), static_cast<int>(std::ceil(max_y)));

    if (x0 >= x1 || y0 >= y1) return;

    auto process_rows = [&](int row_begin, int row_end) {
        for (int y = row_begin; y < row_end; ++y) {
            Color* dst_row = fb.pixels_row(y);
            for (int x = x0; x < x1; ++x) {
                if (state && !pixel_passes_mask(*state, x, y)) continue;

                Vec3 local_h = invH * Vec3(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, 1.0f);
                if (std::abs(local_h.z) < 1e-7f) continue;

                const float lx = local_h.x / local_h.z;
                const float ly = local_h.y / local_h.z;

                if (lx < 0.0f || ly < 0.0f || lx >= static_cast<float>(sw) || ly >= static_cast<float>(sh)) continue;

                float sr, sg, sb, sa;
                sample_bilinear_prgb32(src_pixels, stride, sw, sh, lx, ly, sr, sg, sb, sa);
                sa *= opacity;
                if (sa <= 0.001f) continue;
                sr *= opacity;
                sg *= opacity;
                sb *= opacity;

                Color& dst = dst_row[x];
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
    };

    if (y1 - y0 >= 16) {
        tbb::parallel_for(tbb::blocked_range<int>(y0, y1), [&](const tbb::blocked_range<int>& range) {
            process_rows(range.begin(), range.end());
        });
    } else {
        process_rows(y0, y1);
    }
}

inline void composite_framebuffer_transformed(Framebuffer& dst_fb, const Framebuffer& src_fb, const Mat4& model, float opacity, BlendMode mode, const RenderState* state = nullptr) {
    const int sw = src_fb.width();
    const int sh = src_fb.height();

    const bool is_simple_translation =
        std::abs(model[0][0] - 1.0f) < 1e-5f && std::abs(model[0][1]) < 1e-5f && std::abs(model[0][3]) < 1e-5f &&
        std::abs(model[1][0]) < 1e-5f && std::abs(model[1][1] - 1.0f) < 1e-5f && std::abs(model[1][3]) < 1e-5f &&
        std::abs(model[3][3] - 1.0f) < 1e-5f;

    if (is_simple_translation) {
        int tx = static_cast<int>(std::round(model[3][0]));
        int ty = static_cast<int>(std::round(model[3][1]));
        composite_framebuffer(dst_fb, src_fb, tx, ty, opacity, mode, state);
        return;
    }

    const float sx = model[0][0];
    const float sy = model[1][1];
    const float tx = model[3][0];
    const float ty = model[3][1];

    const bool is_scale_translation =
        std::abs(sx) > 1e-5f && std::abs(sy) > 1e-5f &&
        std::abs(model[0][1]) < 1e-5f && std::abs(model[0][3]) < 1e-5f &&
        std::abs(model[1][0]) < 1e-5f && std::abs(model[1][3]) < 1e-5f &&
        std::abs(model[3][3] - 1.0f) < 1e-5f;

    if (is_scale_translation) {
        if (state && state->mask && state->mask->enabled()) {
            ensure_mask_alpha_cache(*state, dst_fb.width(), dst_fb.height());
        }

        float min_x = tx;
        float max_x = static_cast<float>(sw) * sx + tx;
        if (sx < 0.0f) std::swap(min_x, max_x);

        float min_y = ty;
        float max_y = static_cast<float>(sh) * sy + ty;
        if (sy < 0.0f) std::swap(min_y, max_y);

        const int x0_st = std::max<int>(0, static_cast<int>(std::floor(min_x)));
        const int y0_st = std::max<int>(0, static_cast<int>(std::floor(min_y)));
        const int x1_st = std::min<int>(dst_fb.width(),  static_cast<int>(std::ceil(max_x)));
        const int y1_st = std::min<int>(dst_fb.height(), static_cast<int>(std::ceil(max_y)));

        if (x0_st < x1_st && y0_st < y1_st) {
            const float inv_sx = 1.0f / sx;
            const float inv_sy = 1.0f / sy;
            const Color* src_base = src_fb.data();

            auto process_rows_st = [&](int row_begin, int row_end) {
                for (int y = row_begin; y < row_end; ++y) {
                    Color* dst_row = dst_fb.pixels_row(y);
                    const float ly = (static_cast<float>(y) + 0.5f - ty) * inv_sy;
                    if (ly < 0.0f || ly >= static_cast<float>(sh)) continue;

                    // Precompute constant Y bilinear factors for this row
                    const float vy = ly - 0.5f;
                    int sy0 = static_cast<int>(std::floor(vy));
                    const float ty_factor = vy - static_cast<float>(sy0);
                    int sy1 = sy0 + 1;
                    sy0 = std::clamp(sy0, 0, sh - 1);
                    sy1 = std::clamp(sy1, 0, sh - 1);
                    const float wyb = 1.0f - ty_factor;
                    const float wyt = ty_factor;
                    const Color* src_row0 = src_base + sy0 * sw;
                    const Color* src_row1 = src_base + sy1 * sw;

                    int x = x0_st;
#if defined(__AVX2__)
                    // Process 2 pixels at a time with AVX2 blend.
                    // Individual mask/bounds failures set src to transparent
                    // instead of using continue, so the other pixel isn't lost.
                    for (; x + 1 < x1_st; x += 2) {
                        const bool mask0 = !state || pixel_passes_mask(*state, x, y);
                        const bool mask1 = !state || pixel_passes_mask(*state, x + 1, y);

                        Color src[2];

                        // Pixel 0
                        if (mask0) {
                            const float lx0 = (static_cast<float>(x) + 0.5f - tx) * inv_sx;
                            if (lx0 >= 0.0f && lx0 < static_cast<float>(sw)) {
                                const float vx0 = lx0 - 0.5f;
                                int sx00 = static_cast<int>(std::floor(vx0));
                                const float tx0 = vx0 - static_cast<float>(sx00);
                                int sx01 = sx00 + 1;
                                sx00 = std::clamp(sx00, 0, sw - 1);
                                sx01 = std::clamp(sx01, 0, sw - 1);
                                const Color& c000 = src_row0[sx00];
                                const Color& c001 = src_row0[sx01];
                                const Color& c010 = src_row1[sx00];
                                const Color& c011 = src_row1[sx01];
                                const float w00 = (1.0f - tx0) * wyb;
                                const float w01 = tx0 * wyb;
                                const float w02 = (1.0f - tx0) * wyt;
                                const float w03 = tx0 * wyt;
                                src[0].r = c000.r * w00 + c001.r * w01 + c010.r * w02 + c011.r * w03;
                                src[0].g = c000.g * w00 + c001.g * w01 + c010.g * w02 + c011.g * w03;
                                src[0].b = c000.b * w00 + c001.b * w01 + c010.b * w02 + c011.b * w03;
                                src[0].a = c000.a * w00 + c001.a * w01 + c010.a * w02 + c011.a * w03;
                                src[0].r *= opacity;
                                src[0].g *= opacity;
                                src[0].b *= opacity;
                                src[0].a *= opacity;
                            } else {
                                src[0] = Color{0, 0, 0, 0};
                            }
                        } else {
                            src[0] = Color{0, 0, 0, 0};
                        }

                        // Pixel 1
                        if (mask1) {
                            const float lx1 = (static_cast<float>(x + 1) + 0.5f - tx) * inv_sx;
                            if (lx1 >= 0.0f && lx1 < static_cast<float>(sw)) {
                                const float vx1 = lx1 - 0.5f;
                                int sx10 = static_cast<int>(std::floor(vx1));
                                const float tx1 = vx1 - static_cast<float>(sx10);
                                int sx11 = sx10 + 1;
                                sx10 = std::clamp(sx10, 0, sw - 1);
                                sx11 = std::clamp(sx11, 0, sw - 1);
                                const Color& c100 = src_row0[sx10];
                                const Color& c101 = src_row0[sx11];
                                const Color& c110 = src_row1[sx10];
                                const Color& c111 = src_row1[sx11];
                                const float w10 = (1.0f - tx1) * wyb;
                                const float w11 = tx1 * wyb;
                                const float w12 = (1.0f - tx1) * wyt;
                                const float w13 = tx1 * wyt;
                                src[1].r = c100.r * w10 + c101.r * w11 + c110.r * w12 + c111.r * w13;
                                src[1].g = c100.g * w10 + c101.g * w11 + c110.g * w12 + c111.g * w13;
                                src[1].b = c100.b * w10 + c101.b * w11 + c110.b * w12 + c111.b * w13;
                                src[1].a = c100.a * w10 + c101.a * w11 + c110.a * w12 + c111.a * w13;
                                src[1].r *= opacity;
                                src[1].g *= opacity;
                                src[1].b *= opacity;
                                src[1].a *= opacity;
                            } else {
                                src[1] = Color{0, 0, 0, 0};
                            }
                        } else {
                            src[1] = Color{0, 0, 0, 0};
                        }

                        // Skip SIMD blend if both pixels are transparent
                        if (src[0].a <= 0.001f && src[1].a <= 0.001f) continue;

                        __m256 src_v = _mm256_loadu_ps(&src[0].r);
                        __m256 dst_v = _mm256_loadu_ps(&dst_row[x].r);

                        if (mode == BlendMode::Add) {
                            dst_v = _mm256_min_ps(_mm256_add_ps(dst_v, src_v), _mm256_set1_ps(1.0f));
                        } else {
                            const __m256 sa = _mm256_shuffle_ps(src_v, src_v, 0xFF);
                            const __m256 inv_sa = _mm256_sub_ps(_mm256_set1_ps(1.0f), sa);
                            dst_v = _mm256_add_ps(_mm256_mul_ps(dst_v, inv_sa), src_v);
                        }
                        _mm256_storeu_ps(&dst_row[x].r, dst_v);
                    }
#endif
                    for (; x < x1_st; ++x) {
                        if (state && !pixel_passes_mask(*state, x, y)) continue;

                        const float lx = (static_cast<float>(x) + 0.5f - tx) * inv_sx;
                        if (lx < 0.0f || lx >= static_cast<float>(sw)) continue;

                        Color src;
                        sample_bilinear_float(src_base, sw, sw, sh, lx, ly, src);
                        src.r *= opacity;
                        src.g *= opacity;
                        src.b *= opacity;
                        src.a *= opacity;
                        if (src.a <= 0.001f) continue;

                        Color& dst = dst_row[x];
                        if (mode == BlendMode::Add) {
                            dst.r = std::min(dst.r + src.r, 1.0f);
                            dst.g = std::min(dst.g + src.g, 1.0f);
                            dst.b = std::min(dst.b + src.b, 1.0f);
                            dst.a = std::min(dst.a + src.a, 1.0f);
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

    if (state && state->mask && state->mask->enabled()) {
        ensure_mask_alpha_cache(*state, dst_fb.width(), dst_fb.height());
    }

    glm::mat3 H;
    H[0][0] = model[0][0]; H[0][1] = model[0][1]; H[0][2] = model[0][3];
    H[1][0] = model[1][0]; H[1][1] = model[1][1]; H[1][2] = model[1][3];
    H[2][0] = model[3][0]; H[2][1] = model[3][1]; H[2][2] = model[3][3];

    glm::mat3 invH = glm::inverse(H);

    auto project = [&](float lx, float ly) -> Vec2 {
        float w = model[0][3] * lx + model[1][3] * ly + model[3][3];
        if (std::abs(w) < 1e-7f) return Vec2(0);
        float sx = (model[0][0] * lx + model[1][0] * ly + model[3][0]) / w;
        float sy = (model[0][1] * lx + model[1][1] * ly + model[3][1]) / w;
        return Vec2(sx, sy);
    };

    Vec2 corners[4] = {
        project(0, 0),
        project(static_cast<float>(sw), 0),
        project(static_cast<float>(sw), static_cast<float>(sh)),
        project(0, static_cast<float>(sh))
    };

    float min_x = corners[0].x, max_x = corners[0].x;
    float min_y = corners[0].y, max_y = corners[0].y;
    for (int i = 1; i < 4; ++i) {
        min_x = std::min(min_x, corners[i].x); max_x = std::max(max_x, corners[i].x);
        min_y = std::min(min_y, corners[i].y); max_y = std::max(max_y, corners[i].y);
    }

    const int x0 = std::max<int>(0, static_cast<int>(std::floor(min_x)));
    const int y0 = std::max<int>(0, static_cast<int>(std::floor(min_y)));
    const int x1 = std::min<int>(dst_fb.width(),  static_cast<int>(std::ceil(max_x)));
    const int y1 = std::min<int>(dst_fb.height(), static_cast<int>(std::ceil(max_y)));

    if (x0 >= x1 || y0 >= y1) return;

    const Color* src_base = src_fb.data();

    auto process_rows = [&](int row_begin, int row_end) {
        for (int y = row_begin; y < row_end; ++y) {
            Color* dst_row = dst_fb.pixels_row(y);
            for (int x = x0; x < x1; ++x) {
                if (state && !pixel_passes_mask(*state, x, y)) continue;

                Vec3 local_h = invH * Vec3(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, 1.0f);
                if (std::abs(local_h.z) < 1e-7f) continue;

                const float lx = local_h.x / local_h.z;
                const float ly = local_h.y / local_h.z;

                if (lx < 0.0f || ly < 0.0f || lx >= static_cast<float>(sw) || ly >= static_cast<float>(sh)) continue;

                Color src;
                sample_bilinear_float(src_base, sw, sw, sh, lx, ly, src);
                src.r *= opacity;
                src.g *= opacity;
                src.b *= opacity;
                src.a *= opacity;

                if (src.a <= 0.001f) continue;

                Color& dst = dst_row[x];
                if (mode == BlendMode::Add) {
                    dst.r = std::min(dst.r + src.r, 1.0f);
                    dst.g = std::min(dst.g + src.g, 1.0f);
                    dst.b = std::min(dst.b + src.b, 1.0f);
                    dst.a = std::min(dst.a + src.a, 1.0f);
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

    if (y1 - y0 >= 16) {
        tbb::parallel_for(tbb::blocked_range<int>(y0, y1), [&](const tbb::blocked_range<int>& range) {
            process_rows(range.begin(), range.end());
        });
    } else {
        process_rows(y0, y1);
    }
}

} // namespace chronon3d::blend2d_bridge
