#pragma once

#include <chronon3d/graphics/gradient.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <cmath>
#include <type_traits>
#include <utility>
#include <vector>

namespace chronon3d {

enum class FillType {
    Solid,
    LinearGradient,
    RadialGradient,
    ConicGradient,
};

// Compatibility bridge for scene/path paint consumers. Do not add new
// references outside the legacy scene fill implementation and consumers that
// are covered by TICKET-SHAPE-FILL-COMPAT; migrate those sites to
// graphics::FillStyle/GradientDefinition before removing this alias.
using GradientStop = graphics::GradientStop;
using GradientFill = graphics::GradientDefinition;

[[nodiscard]] inline constexpr FillType fill_type_for_gradient(
    graphics::GradientType type) {
    switch (type) {
        case graphics::GradientType::Linear: return FillType::LinearGradient;
        case graphics::GradientType::Radial: return FillType::RadialGradient;
        case graphics::GradientType::Conic: return FillType::ConicGradient;
    }
    return FillType::LinearGradient;
}

[[nodiscard]] inline Color sample_gradient(const GradientFill& gradient, f32 t) {
    return graphics::sample_gradient(gradient, t);
}

[[nodiscard]] inline Color sample_gradient_radial(
    const GradientFill& gradient, const Vec2& point) {
    return graphics::sample_gradient_radial(gradient, point);
}

[[nodiscard]] inline Color sample_gradient_at(
    const GradientFill& gradient, const Vec2& point) {
    switch (gradient.type) {
        case graphics::GradientType::Linear: {
            const Vec2 direction = gradient.end - gradient.start;
            const f32 length_squared = glm::dot(direction, direction);
            const f32 t = length_squared > 1e-6f
                ? glm::dot(point - gradient.start, direction) / length_squared
                : 0.0f;
            return graphics::sample_gradient(gradient, t);
        }
        case graphics::GradientType::Radial:
            return graphics::sample_gradient_radial(gradient, point);
        case graphics::GradientType::Conic: {
            const Vec2 delta = point - gradient.center;
            f32 angle = std::atan2(delta.y, delta.x) - gradient.angle;
            constexpr f32 tau = 6.2831853071795864769f;
            angle -= tau * std::floor(angle / tau);
            return graphics::sample_gradient(gradient, angle / tau);
        }
    }
    return Color{1, 1, 1, 1};
}

struct Fill {
    bool enabled{true};
    FillType type{FillType::Solid};

    static_assert(std::is_member_object_pointer_v<decltype(&Fill::type)>,
        "Fill::type must remain a public data member (field).");
    Color solid{1, 1, 1, 1};
    GradientFill gradient{};

    static Fill solid_color(Color color) {
        return Fill{true, FillType::Solid, color, {}};
    }

    static Fill linear(Vec2 from, Vec2 to, std::vector<GradientStop> stops) {
        Fill fill;
        fill.type = FillType::LinearGradient;
        fill.gradient = graphics::GradientDefinition::linear(
            from, to, std::move(stops));
        return fill;
    }

    static Fill radial(Vec2 center, f32 radius, std::vector<GradientStop> stops) {
        Fill fill;
        fill.type = FillType::RadialGradient;
        fill.gradient = graphics::GradientDefinition::radial(
            center, radius, std::move(stops));
        return fill;
    }

    static Fill conic(Vec2 center, f32 angle, std::vector<GradientStop> stops) {
        Fill fill;
        fill.type = FillType::ConicGradient;
        fill.gradient = graphics::GradientDefinition::conic(
            center, angle, std::move(stops));
        return fill;
    }
};

} // namespace chronon3d
