#pragma once

#include <chronon3d/core/types.hpp>
#include <algorithm>
#include <cmath>
#include <array>
#include <ostream>
#include <cstdio>


namespace chronon3d {

namespace color_detail {

[[nodiscard]] inline const std::array<u8, 4096>& linear_to_srgb8_lut() {
    static const std::array<u8, 4096> lut = [] {
        std::array<u8, 4096> values{};
        for (usize i = 0; i < values.size(); ++i) {
            const f32 linear = static_cast<f32>(i) / static_cast<f32>(values.size() - 1);
            const f32 srgb = (linear <= 0.0031308f)
                ? (linear * 12.92f)
                : (1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f);
            values[i] = static_cast<u8>(std::clamp(srgb * 255.0f, 0.0f, 255.0f));
        }
        return values;
    }();
    return lut;
}

[[nodiscard]] inline u8 linear_to_srgb8_fast(f32 value) {
    const f32 clamped = std::clamp(value, 0.0f, 1.0f);
    const usize idx = static_cast<usize>(clamped * static_cast<f32>(linear_to_srgb8_lut().size() - 1));
    return linear_to_srgb8_lut()[idx];
}

} // namespace color_detail

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
    static constexpr Color yellow() { return {1.0f, 1.0f, 0.0f, 1.0f}; }
    static constexpr Color transparent() { return {0.0f, 0.0f, 0.0f, 0.0f}; }

    static Color from_hex(const char* hex) {
        if (!hex || hex[0] != '#') return black();
        unsigned int r, g, b;
        if (std::sscanf(hex + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
            return {static_cast<f32>(r) / 255.0f, static_cast<f32>(g) / 255.0f, static_cast<f32>(b) / 255.0f, 1.0f};
        }
        return black();
    }


    constexpr Color operator+(const Color& other) const { return {r + other.r, g + other.g, b + other.b, a + other.a}; }
    constexpr Color operator-(const Color& other) const { return {r - other.r, g - other.g, b - other.b, a - other.a}; }
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

    // Gamma correction helpers
    [[nodiscard]] Color to_linear() const {
        auto gamma_to_linear = [](f32 c) {
            return (c <= 0.04045f) ? (c / 12.92f) : std::pow((c + 0.055f) / 1.055f, 2.4f);
        };
        return {gamma_to_linear(r), gamma_to_linear(g), gamma_to_linear(b), a};
    }

    [[nodiscard]] Color to_srgb() const {
        auto linear_to_gamma = [](f32 c) {
            return (c <= 0.0031308f) ? (c * 12.92f) : (1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f);
        };
        return {linear_to_gamma(r), linear_to_gamma(g), linear_to_gamma(b), a};
    }

    // Alpha compositing helpers.
    // premultiply: RGB *= A  (for correct filtering and blending)
    // unpremultiply: RGB /= A  (to restore straight alpha for display/export)
    [[nodiscard]] constexpr Color premultiplied() const {
        if (a <= 0.0f) return {0.0f, 0.0f, 0.0f, 0.0f};
        return {r * a, g * a, b * a, a};
    }

    [[nodiscard]] constexpr Color unpremultiplied() const {
        if (a <= 0.0f) return {0.0f, 0.0f, 0.0f, 0.0f};
        const f32 inv = 1.0f / a;
        return {r * inv, g * inv, b * inv, a};
    }

    // Fast approximations (gamma 2.2)
    [[nodiscard]] Color to_linear_fast() const {
        return {std::pow(r, 2.2f), std::pow(g, 2.2f), std::pow(b, 2.2f), a};
    }

    [[nodiscard]] Color to_srgb_fast() const {
        return {std::pow(r, 1.0f / 2.2f), std::pow(g, 1.0f / 2.2f), std::pow(b, 1.0f / 2.2f), a};
    }

    [[nodiscard]] static u8 linear_to_srgb8(f32 value) {
        return color_detail::linear_to_srgb8_fast(value);
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
