#pragma once

#include <chronon3d/core/types.hpp>
#include <chronon3d/math/vec2.hpp>
#include <cmath>

namespace chronon3d {

struct Vec3 {
    f32 x{0.0f};
    f32 y{0.0f};
    f32 z{0.0f};

    constexpr Vec3() = default;
    constexpr Vec3(f32 x, f32 y, f32 z) : x(x), y(y), z(z) {}
    constexpr explicit Vec3(f32 scalar) : x(scalar), y(scalar), z(scalar) {}
    constexpr Vec3(const Vec2& v2, f32 z) : x(v2.x), y(v2.y), z(z) {}

    // Operators
    constexpr Vec3 operator+(const Vec3& other) const { return {x + other.x, y + other.y, z + other.z}; }
    constexpr Vec3 operator-(const Vec3& other) const { return {x - other.x, y - other.y, z - other.z}; }
    constexpr Vec3 operator*(const Vec3& other) const { return {x * other.x, y * other.y, z * other.z}; }
    constexpr Vec3 operator/(const Vec3& other) const { return {x / other.x, y / other.y, z / other.z}; }

    constexpr Vec3 operator*(f32 scalar) const { return {x * scalar, y * scalar, z * scalar}; }
    constexpr Vec3 operator/(f32 scalar) const { return {x / scalar, y / scalar, z / scalar}; }

    Vec3& operator+=(const Vec3& other) { x += other.x; y += other.y; z += other.z; return *this; }
    Vec3& operator-=(const Vec3& other) { x -= other.x; y -= other.y; z -= other.z; return *this; }
    Vec3& operator*=(f32 scalar) { x *= scalar; y *= scalar; z *= scalar; return *this; }
    Vec3& operator/=(f32 scalar) { x /= scalar; y /= scalar; z /= scalar; return *this; }

    // Logic
    constexpr bool operator==(const Vec3& other) const { return x == other.x && y == other.y && z == other.z; }
    constexpr bool operator!=(const Vec3& other) const { return !(*this == other); }

    // Common functions
    [[nodiscard]] f32 length_squared() const { return x * x + y * y + z * z; }
    [[nodiscard]] f32 length() const { return std::sqrt(length_squared()); }
    
    [[nodiscard]] constexpr f32 dot(const Vec3& other) const { return x * other.x + y * other.y + z * other.z; }

    [[nodiscard]] constexpr Vec3 cross(const Vec3& other) const {
        return {
            y * other.z - z * other.y,
            z * other.x - x * other.z,
            x * other.y - y * other.x
        };
    }

    [[nodiscard]] Vec3 normalized() const {
        f32 len = length();
        if (len > 0.0f) {
            return *this / len;
        }
        return *this;
    }
};

// Scalar multiplication (scalar * vector)
constexpr Vec3 operator*(f32 scalar, const Vec3& vec) { return vec * scalar; }

} // namespace chronon3d
