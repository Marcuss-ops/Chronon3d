#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <blend2d.h>

#include <chronon3d/scene/layer/render_node.hpp>
#include <chronon3d/scene/mask/mask_utils.hpp>

namespace chronon3d::blend2d_bridge {

inline bool pixel_passes_mask(const RenderState& state, i32 x, i32 y) {
    if (!state.mask || !state.mask->enabled()) return true;
    Vec4 local = state.layer_inv_matrix * Vec4(static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f, 0.0f, 1.0f);
    return mask_contains_local_point(*state.mask, Vec2{local.x, local.y});
}

/**
 * Composites a Blend2D image (PRGB32) into a Chronon3D Framebuffer (FloatRGBA).
 */
inline void composite_bl_image(Framebuffer& fb, const BLImage& img, int x, int y, float opacity, BlendMode mode, const RenderState* state = nullptr) {
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
            if (state && !pixel_passes_mask(*state, px, py)) continue;

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

/**
 * Composites a Blend2D image (PRGB32) into a Chronon3D Framebuffer with a full 4x4 transform.
 * This uses inverse mapping to support perspective and rotation.
 */
inline void composite_bl_image_transformed(Framebuffer& fb, const BLImage& img, const Mat4& model, float opacity, BlendMode mode, const RenderState* state = nullptr) {
    BLImageData data;
    if (img.getData(&data) != BL_SUCCESS) return;

    const uint32_t* src_pixels = reinterpret_cast<const uint32_t*>(data.pixelData);
    const int sw = data.size.w;
    const int sh = data.size.h;
    const int stride = static_cast<int>(data.stride / sizeof(uint32_t));

    // Extract 3x3 homography for local_z = 0 plane
    glm::mat3 H;
    H[0][0] = model[0][0]; H[0][1] = model[0][1]; H[0][2] = model[0][3];
    H[1][0] = model[1][0]; H[1][1] = model[1][1]; H[1][2] = model[1][3];
    H[2][0] = model[3][0]; H[2][1] = model[3][1]; H[2][2] = model[3][3];

    glm::mat3 invH = glm::inverse(H);

    // Compute screen-space bounding box
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

    const float inv255 = 1.0f / 255.0f;

    for (int y = y0; y < y1; ++y) {
        Color* dst_row = fb.pixels_row(y);
        for (int x = x0; x < x1; ++x) {
            if (state && !pixel_passes_mask(*state, x, y)) continue;

            Vec3 local_h = invH * Vec3(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, 1.0f);
            if (std::abs(local_h.z) < 1e-7f) continue;
            
            const float lx = local_h.x / local_h.z;
            const float ly = local_h.y / local_h.z;

            if (lx < 0.0f || ly < 0.0f || lx >= static_cast<float>(sw) || ly >= static_cast<float>(sh)) continue;

            const int sx = std::clamp(static_cast<int>(lx), 0, sw - 1);
            const int sy = std::clamp(static_cast<int>(ly), 0, sh - 1);
            
            const uint32_t p = src_pixels[sy * stride + sx];
            const float sa = ((p >> 24) & 0xFF) * inv255 * opacity;
            if (sa <= 0.001f) continue;

            const float sr = ((p >> 16) & 0xFF) * inv255 * opacity;
            const float sg = ((p >> 8) & 0xFF) * inv255 * opacity;
            const float sb = (p & 0xFF) * inv255 * opacity;

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
}

/**
 * Composites a Framebuffer into another Framebuffer with a full 4x4 transform.
 */
inline void composite_framebuffer_transformed(Framebuffer& dst_fb, const Framebuffer& src_fb, const Mat4& model, float opacity, BlendMode mode, const RenderState* state = nullptr) {
    const int sw = src_fb.width();
    const int sh = src_fb.height();

    // Extract 3x3 homography for local_z = 0 plane
    glm::mat3 H;
    H[0][0] = model[0][0]; H[0][1] = model[0][1]; H[0][2] = model[0][3];
    H[1][0] = model[1][0]; H[1][1] = model[1][1]; H[1][2] = model[1][3];
    H[2][0] = model[3][0]; H[2][1] = model[3][1]; H[2][2] = model[3][3];

    glm::mat3 invH = glm::inverse(H);

    // Compute screen-space bounding box
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

    for (int y = y0; y < y1; ++y) {
        Color* dst_row = dst_fb.pixels_row(y);
        for (int x = x0; x < x1; ++x) {
            if (state && !pixel_passes_mask(*state, x, y)) continue;

            Vec3 local_h = invH * Vec3(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, 1.0f);
            if (std::abs(local_h.z) < 1e-7f) continue;
            
            const float lx = local_h.x / local_h.z;
            const float ly = local_h.y / local_h.z;

            if (lx < 0.0f || ly < 0.0f || lx >= static_cast<float>(sw) || ly >= static_cast<float>(sh)) continue;

            const int sx = std::clamp(static_cast<int>(lx), 0, sw - 1);
            const int sy = std::clamp(static_cast<int>(ly), 0, sh - 1);
            
            Color src = src_fb.get_pixel(sx, sy);
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
}

} // namespace chronon3d::blend2d_bridge
