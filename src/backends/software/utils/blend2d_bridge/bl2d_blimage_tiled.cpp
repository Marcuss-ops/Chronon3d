// bl2d_blimage_tiled.cpp - Tiled image support for Blend2D bridge

#include "../blend2d_bridge.hpp"
#include "bl2d_transform.hpp"
#include "bl2d_sampling.hpp"
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/scene/mask/mask_utils.hpp>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <algorithm>
#include <cmath>

namespace chronon3d::blend2d_bridge {

namespace {

[[nodiscard]] inline float wrap_repeat(float value, float period) {
    if (period <= 0.0f) {
        return 0.0f;
    }

    value -= period * std::floor(value / period);
    if (value < 0.0f) {
        value += period;
    }
    return value;
}

} // namespace

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

    const bool is_simple_translation =
        std::abs(model[0][0] - 1.0f) < 1e-5f &&
        std::abs(model[0][1]) < 1e-5f &&
        std::abs(model[0][3]) < 1e-5f &&
        std::abs(model[1][0]) < 1e-5f &&
        std::abs(model[1][1] - 1.0f) < 1e-5f &&
        std::abs(model[1][3]) < 1e-5f &&
        std::abs(model[3][3] - 1.0f) < 1e-5f;

    const float sx = model[0][0];
    const float sy = model[1][1];
    const float tx = model[3][0];
    const float ty = model[3][1];

    const bool is_scale_translation =
        std::abs(sx) > 1e-5f &&
        std::abs(sy) > 1e-5f &&
        std::abs(model[0][1]) < 1e-5f &&
        std::abs(model[0][3]) < 1e-5f &&
        std::abs(model[1][0]) < 1e-5f &&
        std::abs(model[1][3]) < 1e-5f &&
        std::abs(model[3][3] - 1.0f) < 1e-5f;

    glm::mat3 invH{};
    bool is_affine = false;
    float a00 = 0.0f, a10 = 0.0f, a20 = 0.0f;
    float a01 = 0.0f, a11 = 0.0f, a21 = 0.0f;

    if (!is_simple_translation && !is_scale_translation) {
        glm::mat3 H;
        H[0][0] = model[0][0]; H[0][1] = model[0][1]; H[0][2] = model[0][3];
        H[1][0] = model[1][0]; H[1][1] = model[1][1]; H[1][2] = model[1][3];
        H[2][0] = model[3][0]; H[2][1] = model[3][1]; H[2][2] = model[3][3];
        invH = glm::inverse(H);

        is_affine = std::abs(invH[0][2]) < 1e-6f && std::abs(invH[1][2]) < 1e-6f;
        if (is_affine) {
            const float inv_z = 1.0f / invH[2][2];
            a00 = invH[0][0] * inv_z;
            a10 = invH[1][0] * inv_z;
            a20 = invH[2][0] * inv_z;
            a01 = invH[0][1] * inv_z;
            a11 = invH[1][1] * inv_z;
            a21 = invH[2][1] * inv_z;
        }
    }

    auto process_rows = [&](int row_begin, int row_end) {
        for (int y = row_begin; y < row_end; ++y) {
            Color* dst_row = fb.pixels_row(y);
            for (int x = x0; x < x1; ++x) {
                if (state && !pixel_passes_mask(*state, x, y)) continue;

                float lx = 0.0f;
                float ly = 0.0f;

                if (is_simple_translation) {
                    lx = static_cast<float>(x) + 0.5f - tx;
                    ly = static_cast<float>(y) + 0.5f - ty;
                } else if (is_scale_translation) {
                    const float inv_sx = 1.0f / sx;
                    const float inv_sy = 1.0f / sy;
                    lx = (static_cast<float>(x) + 0.5f - tx) * inv_sx;
                    ly = (static_cast<float>(y) + 0.5f - ty) * inv_sy;
                } else if (is_affine) {
                    const float px = static_cast<float>(x) + 0.5f;
                    const float py = static_cast<float>(y) + 0.5f;
                    lx = a00 * px + a10 * py + a20;
                    ly = a01 * px + a11 * py + a21;
                } else {
                    const Vec3 local_h = invH * Vec3(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, 1.0f);
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

} // namespace chronon3d::blend2d_bridge
