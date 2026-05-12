#pragma once

#include <chronon3d/core/types.hpp>
#include <chronon3d/math/vec3.hpp>
#include <chronon3d/math/quat.hpp>
#include <array>
#include <cmath>

namespace chronon3d {

struct Mat4 {
    std::array<f32, 16> m{0.0f};

    constexpr Mat4() {
        m[0] = 1.0f; m[5] = 1.0f; m[10] = 1.0f; m[15] = 1.0f;
    }

    static constexpr Mat4 identity() { return Mat4(); }

    static Mat4 translate(const Vec3& v) {
        Mat4 res;
        res.m[12] = v.x;
        res.m[13] = v.y;
        res.m[14] = v.z;
        return res;
    }

    static Mat4 scale(const Vec3& v) {
        Mat4 res;
        res.m[0] = v.x;
        res.m[5] = v.y;
        res.m[10] = v.z;
        return res;
    }

    static Mat4 rotate(const Quat& q) {
        Mat4 res;
        f32 xx = q.x * q.x; f32 yy = q.y * q.y; f32 zz = q.z * q.z;
        f32 xy = q.x * q.y; f32 xz = q.x * q.z; f32 yz = q.y * q.z;
        f32 wx = q.w * q.x; f32 wy = q.w * q.y; f32 wz = q.w * q.z;

        res.m[0] = 1.0f - 2.0f * (yy + zz);
        res.m[1] = 2.0f * (xy + wz);
        res.m[2] = 2.0f * (xz - wy);

        res.m[4] = 2.0f * (xy - wz);
        res.m[5] = 1.0f - 2.0f * (xx + zz);
        res.m[6] = 2.0f * (yz + wx);

        res.m[8] = 2.0f * (xz + wy);
        res.m[9] = 2.0f * (yz - wx);
        res.m[10] = 1.0f - 2.0f * (xx + yy);

        return res;
    }

    static Mat4 perspective(f32 fov_rad, f32 aspect, f32 near, f32 far) {
        Mat4 res;
        f32 tan_half_fov = std::tan(fov_rad / 2.0f);
        res.m[0] = 1.0f / (aspect * tan_half_fov);
        res.m[5] = 1.0f / tan_half_fov;
        res.m[10] = -(far + near) / (far - near);
        res.m[11] = -1.0f;
        res.m[14] = -(2.0f * far * near) / (far - near);
        res.m[15] = 0.0f;
        return res;
    }

    Mat4 operator*(const Mat4& o) const {
        Mat4 res;
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                res.m[r + c * 4] = 
                    m[r + 0 * 4] * o.m[0 + c * 4] +
                    m[r + 1 * 4] * o.m[1 + c * 4] +
                    m[r + 2 * 4] * o.m[2 + c * 4] +
                    m[r + 3 * 4] * o.m[3 + c * 4];
            }
        }
        return res;
    }

    [[nodiscard]] Vec3 transform_point(const Vec3& v) const {
        f32 x = v.x * m[0] + v.y * m[4] + v.z * m[8] + m[12];
        f32 y = v.x * m[1] + v.y * m[5] + v.z * m[9] + m[13];
        f32 z = v.x * m[2] + v.y * m[6] + v.z * m[10] + m[14];
        f32 w = v.x * m[3] + v.y * m[7] + v.z * m[11] + m[15];
        if (w != 0.0f && w != 1.0f) {
            return {x / w, y / w, z / w};
        }
        return {x, y, z};
    }
};

} // namespace chronon3d
