#pragma once

#include <chronon3d/core/types.hpp>
#include <algorithm>
#include <ostream>

namespace chronon3d {

struct Color {
    f32 r{0.0f};
    f32 g{0.0f};
    f32 b{0.0f};
    f32 a{1.0f};

    constexpr Color() = default;
    constexpr Color(f32 r, f32 g, f32 b, f32 a = 1.0f) : r(r), g(g), b(b), a(a) {}

    static constexpr Color black() { return {0.0f, 0.0f, 0.0f, 1.0f}; }
    static constexpr Color white() { return {1.0f, 1.0f, 1.0f, 1.0f}; }
    static constexpr Color red()   { return {1.0f, 0.0f, 0.0f, 1.0f}; }
    static constexpr Color green() { return {0.0f, 1.0f, 0.0f, 1.0f}; }
    static constexpr Color blue()  { return {0.0f, 0.0f, 1.0f, 1.0f}; }

    constexpr Color operator+(const Color& other) const { return {r + other.r, g + other.g, b + other.b, a + other.a}; }
    constexpr Color operator*(f32 scalar) const { return {r * scalar, g * scalar, b * scalar, a * scalar}; }

    [[nodiscard]] constexpr Color clamped() const {
        return {
            std::clamp(r, 0.0f, 1.0f),
            std::clamp(g, 0.0f, 1.0f),
            std::clamp(b, 0.0f, 1.0f),
            std::clamp(a, 0.0f, 1.0f)
        };
    }

    [[nodiscard]] constexpr Color with_alpha(f32 new_a) const {
        return {r, g, b, new_a};
    }

    constexpr bool operator==(const Color& other) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }
};

// Specialized lerp for AnimatedValue
inline Color lerp(const Color& a, const Color& b, f32 t) {
    return {
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t,
        a.a + (b.a - a.a) * t
    };
}

} // namespace chronon3d

inline std::ostream& operator<<(std::ostream& os, const chronon3d::Color& c) {
    return os << "Color(" << c.r << ", " << c.g << ", " << c.b << ", " << c.a << ")";
}
