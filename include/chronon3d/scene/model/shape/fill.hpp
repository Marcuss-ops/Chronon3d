#pragma once

#include <chronon3d/graphics/gradient.hpp>
#include <chronon3d/graphics/field2d.hpp>
#include <chronon3d/graphics/gradient_mesh.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace chronon3d {

enum class FillType {
    Solid,
    LinearGradient,
    RadialGradient,
    ConicGradient,
    Field2D,
    GradientMesh,
};

/// Cached low-resolution scalar field. Sampling interpolates this grid over
/// normalized shape coordinates, keeping high-frequency field evaluation out
/// of the per-output-pixel loop.
struct Field2DSampleGrid {
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::vector<float> values;

    [[nodiscard]] float sample(Vec2 p) const noexcept {
        if (width == 0 || height == 0 || values.size() !=
            static_cast<std::size_t>(width) * height) return 0.0f;
        const float x = std::clamp(p.x, 0.0f, 1.0f) * static_cast<float>(width - 1);
        const float y = std::clamp(p.y, 0.0f, 1.0f) * static_cast<float>(height - 1);
        const auto x0 = static_cast<std::uint32_t>(x);
        const auto y0 = static_cast<std::uint32_t>(y);
        const auto x1 = std::min(x0 + 1, width - 1);
        const auto y1 = std::min(y0 + 1, height - 1);
        const float tx = x - static_cast<float>(x0);
        const float ty = y - static_cast<float>(y0);
        const auto at = [&](std::uint32_t ix, std::uint32_t iy) {
            return values[static_cast<std::size_t>(iy) * width + ix];
        };
        const float top = std::lerp(at(x0, y0), at(x1, y0), tx);
        const float bottom = std::lerp(at(x0, y1), at(x1, y1), tx);
        return std::lerp(top, bottom, ty);
    }
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
    std::optional<graphics::Field2D> field;
    std::optional<GradientFill> field_ramp;
    std::optional<Field2DSampleGrid> field_grid;
    std::optional<graphics::GradientMesh> gradient_mesh;

    static Fill solid_color(Color color) {
        Fill fill;
        fill.enabled = true;
        fill.type = FillType::Solid;
        fill.solid = color;
        return fill;
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

    static Fill field2d(graphics::Field2D value, Color tint = Color{1.0f, 1.0f, 1.0f, 1.0f},
                        std::optional<GradientFill> ramp = std::nullopt,
                        std::uint32_t render_scale = 1,
                        std::uint32_t render_width = 0,
                        std::uint32_t render_height = 0) {
        if (render_scale != 1 && render_scale != 2 && render_scale != 4)
            throw std::invalid_argument("Field2D render scale must be 1, 2, or 4");
        if (render_scale != 1 && (render_width == 0 || render_height == 0 ||
            render_width > 1000000 || render_height > 1000000))
            throw std::invalid_argument("Field2D low-resolution grid dimensions are outside supported bounds");
        Fill fill;
        fill.type = FillType::Field2D;
        fill.solid = tint;
        fill.field = std::move(value);
        fill.field_ramp = std::move(ramp);
        if (render_scale != 1 && render_width > 0 && render_height > 0) {
            Field2DSampleGrid grid;
            grid.width = std::max(1U, (render_width + render_scale - 1) / render_scale);
            grid.height = std::max(1U, (render_height + render_scale - 1) / render_scale);
            const auto cell_count = static_cast<std::uint64_t>(grid.width) * grid.height;
            if (cell_count > 16U * 1024U * 1024U)
                throw std::invalid_argument("Field2D low-resolution grid exceeds the 16M sample budget");
            grid.values.reserve(static_cast<std::size_t>(grid.width) * grid.height);
            for (std::uint32_t y = 0; y < grid.height; ++y) {
                for (std::uint32_t x = 0; x < grid.width; ++x) {
                    const float nx = grid.width > 1
                        ? static_cast<float>(x) / static_cast<float>(grid.width - 1) : 0.5f;
                    const float ny = grid.height > 1
                        ? static_cast<float>(y) / static_cast<float>(grid.height - 1) : 0.5f;
                    grid.values.push_back(fill.field->sample({nx, ny}));
                }
            }
            fill.field_grid = std::move(grid);
        }
        return fill;
    }

    static Fill mesh(graphics::GradientMesh value) {
        Fill fill;
        fill.type = FillType::GradientMesh;
        fill.gradient_mesh = std::move(value);
        return fill;
    }
};

/// Compat predicate so callers outside this header never name the legacy
/// `FillType` enumerator directly (see TICKET-SHAPE-FILL-COMPAT).
[[nodiscard]] inline constexpr bool is_solid_fill(const Fill& fill) noexcept {
    return fill.type == FillType::Solid;
}

[[nodiscard]] inline Color sample_fill_at(const Fill& fill, const Vec2& point) {
    if (fill.type == FillType::Field2D && fill.field) {
        const float value = fill.field_grid
            ? fill.field_grid->sample(point) : fill.field->sample(point);
        if (fill.field_ramp) {
            Color ramp_color = graphics::sample_gradient(*fill.field_ramp, value);
            ramp_color.r *= fill.solid.r;
            ramp_color.g *= fill.solid.g;
            ramp_color.b *= fill.solid.b;
            ramp_color.a *= fill.solid.a;
            return ramp_color;
        }
        return Color{fill.solid.r * value, fill.solid.g * value,
                     fill.solid.b * value, fill.solid.a};
    }
    if (fill.type == FillType::GradientMesh && fill.gradient_mesh)
        return fill.gradient_mesh->sample(point);
    return sample_gradient_at(fill.gradient, point);
}

} // namespace chronon3d
