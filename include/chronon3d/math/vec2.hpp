#pragma once

#include <chronon3d/core/types.hpp>
#include <cmath>

namespace chronon3d {

struct Vec2 {
    f32 x{0.0f};
    f32 y{0.0f};

    constexpr Vec2() = default;
    constexpr Vec2(f32 x, f32 y) : x(x), y(y) {}
    constexpr explicit Vec2(f32 scalar) : x(scalar), y(scalar) {}

    // Operators
    constexpr Vec2 operator+(const Vec2& other) const { return {x + other.x, y + other.y}; }
    constexpr Vec2 operator-(const Vec2& other) const { return {x - other.x, y - other.y}; }
    constexpr Vec2 operator*(const Vec2& other) const { return {x * other.x, y * other.y}; }
    constexpr Vec2 operator/(const Vec2& other) const { return {x / other.x, y / other.y}; }

    constexpr Vec2 operator*(f32 scalar) const { return {x * scalar, y * scalar}; }
    constexpr Vec2 operator/(f32 scalar) const { return {x / scalar, y / scalar}; }

    Vec2& operator+=(const Vec2& other) { x += other.x; y += other.y; return *this; }
    Vec2& operator-=(const Vec2& other) { x -= other.x; y -= other.y; return *this; }
    Vec2& operator*=(f32 scalar) { x *= scalar; y *= scalar; return *this; }
    Vec2& operator/=(f32 scalar) { x /= scalar; y /= scalar; return *this; }

    // Logic
    constexpr bool operator==(const Vec2& other) const { return x == other.x && y == other.y; }
    constexpr bool operator!=(const Vec2& other) const { return !(*this == other); }

    // Common functions
    [[nodiscard]] f32 length_squared() const { return x * x + y * y; }
    [[nodiscard]] f32 length() const { return std::sqrt(length_squared()); }
    
    [[nodiscard]] constexpr f32 dot(const Vec2& other) const { return x * other.x + y * other.y; }

    [[nodiscard]] Vec2 normalized() const {
        f32 len = length();
        if (len > 0.0f) {
            return *this / len;
        }
        return *this;
    }
};

// Scalar multiplication (scalar * vector)
constexpr Vec2 operator*(f32 scalar, const Vec2& vec) { return vec * scalar; }

} // namespace chronon3d
