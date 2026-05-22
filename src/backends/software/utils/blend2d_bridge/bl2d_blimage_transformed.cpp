// bl2d_blimage_transformed.cpp - Transformed compositing for BLImage

#include "bl2d_transform.hpp"
#include <algorithm>
#include <cmath>

namespace chronon3d::blend2d_bridge {

void composite_bl_image_transformed(Framebuffer& fb, const BLImage& img, const Mat4& model, float opacity, BlendMode mode, const RenderState* state) {
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

    // Full projective path
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

} // namespace chronon3d::blend2d_bridge
