#pragma once

#include <chronon3d/core/types.hpp>
#include <cmath>

namespace chronon3d {

struct Quat {
    f32 x{0.0f};
    f32 y{0.0f};
    f32 z{0.0f};
    f32 w{1.0f};

    constexpr Quat() = default;
    constexpr Quat(f32 x, f32 y, f32 z, f32 w) : x(x), y(y), z(z), w(w) {}

    static constexpr Quat identity() { return {0.0f, 0.0f, 0.0f, 1.0f}; }

    // Basic arithmetic for interpolation
    constexpr Quat operator+(const Quat& other) const { return {x + other.x, y + other.y, z + other.z, w + other.w}; }
    constexpr Quat operator-(const Quat& other) const { return {x - other.x, y - other.y, z - other.z, w - other.w}; }
    constexpr Quat operator*(f32 scalar) const { return {x * scalar, y * scalar, z * scalar, w * scalar}; }

    [[nodiscard]] constexpr f32 dot(const Quat& other) const {
        return x * other.x + y * other.y + z * other.z + w * other.w;
    }

    [[nodiscard]] Quat normalized() const {
        f32 len = std::sqrt(dot(*this));
        if (len > 0.0f) {
            f32 inv_len = 1.0f / len;
            return *this * inv_len;
        }
        return identity();
    }

    // Simple Lerp (Linear Interpolation) - Slerp can be added later for better quality
    static Quat lerp(const Quat& a, const Quat& b, f32 t) {
        f32 dot_val = a.dot(b);
        Quat b_adj = b;
        if (dot_val < 0.0f) {
            b_adj = b * -1.0f;
        }
        return (a * (1.0f - t) + b_adj * t).normalized();
    }
};

// Specialized lerp for AnimatedValue
inline Quat lerp(const Quat& a, const Quat& b, f32 t) {
    return Quat::lerp(a, b, t);
}

} // namespace chronon3d
