// bl2d_compositing.cpp - Basic compositing operations for blend2d_bridge

#include "bl2d_compositing.hpp"
#include <chronon3d/scene/mask/mask_utils.hpp>

namespace chronon3d::blend2d_bridge {

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

} // namespace chronon3d::blend2d_bridge
