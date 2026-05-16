#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <blend2d.h>

namespace chronon3d::blend2d_bridge {

/**
 * Composites a Blend2D image (PRGB32) into a Chronon3D Framebuffer (FloatRGBA).
 */
inline void composite_bl_image(Framebuffer& fb, const BLImage& img, int x, int y, float opacity, BlendMode mode) {
    BLImageData data;
    if (img.getData(&data) != BL_SUCCESS) return;

    const uint32_t* src_pixels = reinterpret_cast<const uint32_t*>(data.pixelData);
    const int sw = data.size.w;
    const int sh = data.size.h;
    const int stride = static_cast<int>(data.stride / sizeof(uint32_t));

    const float inv255 = 1.0f / 255.0f;

    for (int iy = 0; iy < sh; ++iy) {
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

            // Blend2D PRGB32 is premultiplied
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
                // Normal blend (Source-Over)
                // Since src is already premultiplied by Blend2D (and we applied opacity)
                const float inv_sa = 1.0f - sa;
                dst.r = sr + dst.r * inv_sa;
                dst.g = sg + dst.g * inv_sa;
                dst.b = sb + dst.b * inv_sa;
                dst.a = sa + dst.a * inv_sa;
            }
        }
    }
}
/**
 * Composites a Framebuffer into another Framebuffer at an offset.
 */
inline void composite_framebuffer_offset(Framebuffer& dst_fb, const Framebuffer& src_fb, int x, int y, float opacity, BlendMode mode) {
    const int sw = src_fb.width();
    const int sh = src_fb.height();

    for (int iy = 0; iy < sh; ++iy) {
        const int py = y + iy;
        if (py < 0 || py >= dst_fb.height()) continue;

        Color* dst_row = dst_fb.pixels_row(py);
        const Color* src_row = src_fb.pixels_row(iy);

        for (int ix = 0; ix < sw; ++ix) {
            const int px = x + ix;
            if (px < 0 || px >= dst_fb.width()) continue;

            Color src = src_row[ix];
            // Since src is already premultiplied (from composite_bl_image), 
            // applying opacity means multiplying ALL channels.
            src.r *= opacity;
            src.g *= opacity;
            src.b *= opacity;
            src.a *= opacity;
            
            if (src.a <= 0.001f) continue;

            Color& dst = dst_row[px];
            if (mode == BlendMode::Add) {
                // For additive blend, we just add the premultiplied source.
                dst.r = std::min(dst.r + src.r, 1.0f);
                dst.g = std::min(dst.g + src.g, 1.0f);
                dst.b = std::min(dst.b + src.b, 1.0f);
                dst.a = std::min(dst.a + src.a, 1.0f);
            } else {
                // Normal blend (Source-Over)
                const float inv_sa = 1.0f - src.a;
                dst.r = src.r + dst.r * inv_sa;
                dst.g = src.g + dst.g * inv_sa;
                dst.b = src.b + dst.b * inv_sa;
                dst.a = src.a + dst.a * inv_sa;
            }
        }
    }
}

} // namespace chronon3d::blend2d_bridge
