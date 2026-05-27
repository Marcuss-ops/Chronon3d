#pragma once

#include "blend2d_bridge.hpp"
#include <algorithm>
#include <cmath>

namespace chronon3d::blend2d_bridge::detail {

[[nodiscard]] inline bool is_simple_translation(const Mat4& model) {
    return std::abs(model[0][0] - 1.0f) < 1e-5f && std::abs(model[0][1]) < 1e-5f && std::abs(model[0][3]) < 1e-5f &&
           std::abs(model[1][0]) < 1e-5f && std::abs(model[1][1] - 1.0f) < 1e-5f && std::abs(model[1][3]) < 1e-5f &&
           std::abs(model[3][3] - 1.0f) < 1e-5f;
}

[[nodiscard]] inline bool is_scale_translation(const Mat4& model) {
    return std::abs(model[0][0]) > 1e-5f && std::abs(model[1][1]) > 1e-5f &&
           std::abs(model[0][1]) < 1e-5f && std::abs(model[0][3]) < 1e-5f &&
           std::abs(model[1][0]) < 1e-5f && std::abs(model[1][3]) < 1e-5f &&
           std::abs(model[3][3] - 1.0f) < 1e-5f;
}

struct TransformInfo {
    bool is_affine;
    glm::mat3 invH;
    float a00, a10, a20, a01, a11, a21;

    explicit TransformInfo(const Mat4& model) {
        glm::mat3 H;
        H[0][0] = model[0][0]; H[0][1] = model[0][1]; H[0][2] = model[0][3];
        H[1][0] = model[1][0]; H[1][1] = model[1][1]; H[1][2] = model[1][3];
        H[2][0] = model[3][0]; H[2][1] = model[3][1]; H[2][2] = model[3][3];
        invH = glm::inverse(H);

        is_affine = std::abs(invH[0][2]) < 1e-6f && std::abs(invH[1][2]) < 1e-6f;
        a00 = a10 = a20 = a01 = a11 = a21 = 0.0f;
        if (is_affine) {
            const float inv_z = 1.0f / invH[2][2];
            a00 = invH[0][0] * inv_z; a10 = invH[1][0] * inv_z; a20 = invH[2][0] * inv_z;
            a01 = invH[0][1] * inv_z; a11 = invH[1][1] * inv_z; a21 = invH[2][1] * inv_z;
        }
    }

    [[nodiscard]] inline bool map(float x, float y, float& lx, float& ly) const {
        if (is_affine) {
            lx = a00 * x + a10 * y + a20;
            ly = a01 * x + a11 * y + a21;
            return true;
        }
        Vec3 local = invH * Vec3(x, y, 1.0f);
        if (std::abs(local.z) < 1e-7f) return false;
        lx = local.x / local.z;
        ly = local.y / local.z;
        return true;
    }
};

inline void get_projective_bounds(const Mat4& model, float sw, float sh, int fb_width, int fb_height,
                                                 int& out_x0, int& out_y0, int& out_x1, int& out_y1) {
    auto project = [&](float lx, float ly) -> Vec2 {
        float w = model[0][3] * lx + model[1][3] * ly + model[3][3];
        if (std::abs(w) < 1e-7f) return Vec2(0);
        return Vec2((model[0][0] * lx + model[1][0] * ly + model[3][0]) / w,
                    (model[0][1] * lx + model[1][1] * ly + model[3][1]) / w);
    };

    Vec2 corners[4] = { project(0, 0), project(sw, 0), project(sw, sh), project(0, sh) };
    float min_x = corners[0].x, max_x = corners[0].x;
    float min_y = corners[0].y, max_y = corners[0].y;
    for (int i = 1; i < 4; ++i) {
        min_x = std::min(min_x, corners[i].x); max_x = std::max(max_x, corners[i].x);
        min_y = std::min(min_y, corners[i].y); max_y = std::max(max_y, corners[i].y);
    }
    out_x0 = std::max<int>(0, static_cast<int>(std::floor(min_x)));
    out_y0 = std::max<int>(0, static_cast<int>(std::floor(min_y)));
    out_x1 = std::min<int>(fb_width,  static_cast<int>(std::ceil(max_x)));
    out_y1 = std::min<int>(fb_height, static_cast<int>(std::ceil(max_y)));
}

inline void blend_pixel(Color& dst, const Color& src, BlendMode mode) {
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

} // namespace chronon3d::blend2d_bridge::detail
