#pragma once

// ── fill_style_lerp.hpp — Gradient/FillStyle/StrokeStyle interpolation ────
//
// Extracted from fill_style.hpp (FASE 9 Step 2).
// Provides lerp helpers for GradientStop, GradientDefinition, FillStyle,
// and StrokeStyle, plus the interpolate_values<FillStyle/StrokeStyle>
// specializations that wire them into the keyframe animation system.
//
// Include this header when you need FillStyle/StrokeStyle interpolation.
// It pulls in fill_style.hpp and stroke_style.hpp transitively.

#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/graphics/shape_style/stroke_style.hpp>
#include <chronon3d/animation/core/keyframe.hpp>
#include <algorithm>

namespace chronon3d::graphics {

// ══════════════════════════════════════════════════════════════════════
//  FillStyle / GradientDefinition interpolation (animation helpers)
// ══════════════════════════════════════════════════════════════════════

/// Lerp two GradientStop values by position and colour.
[[nodiscard]] inline GradientStop lerp_gradient_stop(
    const GradientStop& a,
    const GradientStop& b,
    f32 t) noexcept
{
    return {
        a.position + (b.position - a.position) * t,
        {
            a.color.r + (b.color.r - a.color.r) * t,
            a.color.g + (b.color.g - a.color.g) * t,
            a.color.b + (b.color.b - a.color.b) * t,
            a.color.a + (b.color.a - a.color.a) * t,
        }
    };
}

namespace detail {

/// Merge two sorted position vectors into a single sorted unique set.
[[nodiscard]] inline std::vector<f32> merge_stop_positions(
    const std::vector<GradientStop>& a,
    const std::vector<GradientStop>& b)
{
    std::vector<f32> positions;
    positions.reserve(a.size() + b.size());
    for (const auto& s : a) positions.push_back(s.position);
    for (const auto& s : b) positions.push_back(s.position);
    std::sort(positions.begin(), positions.end());
    auto last = std::unique(positions.begin(), positions.end());
    positions.erase(last, positions.end());
    return positions;
}

/// Lerp two opacity stop vectors: merge positions, sample & lerp.
[[nodiscard]] inline std::vector<OpacityStop> lerp_opacity_stops(
    const std::vector<OpacityStop>& a,
    const std::vector<OpacityStop>& b,
    f32 t)
{
    if (a.empty() && b.empty()) return {};
    if (a.empty()) return b;
    if (b.empty()) return a;

    std::vector<f32> positions;
    positions.reserve(a.size() + b.size());
    for (const auto& s : a) positions.push_back(s.position);
    for (const auto& s : b) positions.push_back(s.position);
    std::sort(positions.begin(), positions.end());
    auto last = std::unique(positions.begin(), positions.end());
    positions.erase(last, positions.end());

    std::vector<OpacityStop> result;
    result.reserve(positions.size());
    for (f32 pos : positions) {
        const f32 oa = detail::sample_opacity_stops(a, pos);
        const f32 ob = detail::sample_opacity_stops(b, pos);
        result.push_back({pos, oa + (ob - oa) * t});
    }
    return result;
}

} // namespace detail

/// Interpolate two GradientDefinition values.
[[nodiscard]] inline GradientDefinition lerp_gradient(
    const GradientDefinition& a,
    const GradientDefinition& b,
    f32 t) noexcept
{
    const GradientType result_type = (t < 0.5f) ? a.type : b.type;

    const auto positions = detail::merge_stop_positions(
        a.color_stops, b.color_stops);

    std::vector<GradientStop> lerped_stops;
    lerped_stops.reserve(positions.size());
    for (f32 pos : positions) {
        const Color ca = detail::sample_color_stops(a.color_stops, pos);
        const Color cb = detail::sample_color_stops(b.color_stops, pos);
        lerped_stops.push_back({
            pos,
            {
                ca.r + (cb.r - ca.r) * t,
                ca.g + (cb.g - ca.g) * t,
                ca.b + (cb.b - ca.b) * t,
                ca.a + (cb.a - ca.a) * t,
            }
        });
    }

    const auto lerped_op = detail::lerp_opacity_stops(
        a.opacity_stops, b.opacity_stops, t);

    GradientDefinition result;
    result.type = result_type;
    result.color_stops = std::move(lerped_stops);
    result.opacity_stops = std::move(lerped_op);
    result.spread      = (t < 0.5f) ? a.spread : b.spread;

    switch (result_type) {
        case GradientType::Linear: {
            result.start = {
                a.start.x + (b.start.x - a.start.x) * t,
                a.start.y + (b.start.y - a.start.y) * t
            };
            result.end   = {
                a.end.x   + (b.end.x   - a.end.x)   * t,
                a.end.y   + (b.end.y   - a.end.y)   * t
            };
            break;
        }
        case GradientType::Radial: {
            result.center = {
                a.center.x + (b.center.x - a.center.x) * t,
                a.center.y + (b.center.y - a.center.y) * t
            };
            result.radius = a.radius + (b.radius - a.radius) * t;
            break;
        }
        case GradientType::Conic: {
            result.center = {
                a.center.x + (b.center.x - a.center.x) * t,
                a.center.y + (b.center.y - a.center.y) * t
            };
            result.angle  = a.angle + (b.angle - a.angle) * t;
            break;
        }
    }

    return result;
}

/// Interpolate two FillStyle values.
[[nodiscard]] inline FillStyle lerp_fill_style(
    const FillStyle& a,
    const FillStyle& b,
    f32 t) noexcept
{
    const bool a_solid = a.is_solid() || !a.enabled;
    const bool b_solid = b.is_solid() || !b.enabled;

    if (a_solid && b_solid) {
        const Color& ca = a.solid_color;
        const Color& cb = b.solid_color;
        return FillStyle{
            true,
            {
                ca.r + (cb.r - ca.r) * t,
                ca.g + (cb.g - ca.g) * t,
                ca.b + (cb.b - ca.b) * t,
                ca.a + (cb.a - ca.a) * t,
            },
            std::nullopt
        };
    }

    const auto to_gdef = [](const FillStyle& fs) -> GradientDefinition {
        if (fs.gradient.has_value()) return *fs.gradient;
        GradientDefinition g;
        g.type = GradientType::Linear;
        g.start = {0.0f, 0.5f};
        g.end   = {1.0f, 0.5f};
        g.color_stops = {
            {0.0f, fs.solid_color},
            {1.0f, fs.solid_color},
        };
        return g;
    };

    const GradientDefinition ga = to_gdef(a);
    const GradientDefinition gb = to_gdef(b);
    const GradientDefinition lerped = lerp_gradient(ga, gb, t);

    FillStyle result;
    result.enabled = true;
    result.gradient = lerped;
    return result;
}

/// Interpolate two StrokeStyle values.
[[nodiscard]] inline StrokeStyle lerp_stroke_style(
    const StrokeStyle& a,
    const StrokeStyle& b,
    f32 t) noexcept
{
    StrokeStyle result;
    result.enabled = (t < 0.5f) ? a.enabled : b.enabled;

    result.color = {
        a.color.r + (b.color.r - a.color.r) * t,
        a.color.g + (b.color.g - a.color.g) * t,
        a.color.b + (b.color.b - a.color.b) * t,
        a.color.a + (b.color.a - a.color.a) * t,
    };

    result.width       = a.width + (b.width - a.width) * t;
    result.dash_offset = a.dash_offset + (b.dash_offset - a.dash_offset) * t;
    result.trim_start  = a.trim_start + (b.trim_start - a.trim_start) * t;
    result.trim_end    = a.trim_end + (b.trim_end - a.trim_end) * t;

    result.alignment = (t < 0.5f) ? a.alignment : b.alignment;
    result.cap       = (t < 0.5f) ? a.cap : b.cap;
    result.join      = (t < 0.5f) ? a.join : b.join;

    result.dash_array = (t < 0.5f) ? a.dash_array : b.dash_array;

    const bool a_grad = a.gradient.has_value();
    const bool b_grad = b.gradient.has_value();

    if (!a_grad && !b_grad) {
        result.gradient = std::nullopt;
    } else {
        const auto to_gdef = [](const StrokeStyle& s) -> GradientDefinition {
            if (s.gradient.has_value()) return *s.gradient;
            GradientDefinition g;
            g.type = GradientType::Linear;
            g.start = {0.0f, 0.5f};
            g.end   = {1.0f, 0.5f};
            g.color_stops = {
                {0.0f, s.color},
                {1.0f, s.color},
            };
            return g;
        };

        const GradientDefinition ga = to_gdef(a);
        const GradientDefinition gb = to_gdef(b);
        result.gradient = lerp_gradient(ga, gb, t);
    }

    return result;
}

} // namespace chronon3d::graphics

// ── interpolate_values<FillStyle> / interpolate_values<StrokeStyle> ──────

namespace chronon3d {

template <>
inline graphics::FillStyle interpolate_values<graphics::FillStyle>(
    const graphics::FillStyle& a,
    const graphics::FillStyle& b,
    f32 t,
    EasingCurve e)
{
    if (!e.cubic.has_value() && e.preset == Easing::Hold) return a;
    const f32 eased_t = e.apply(t);
    return graphics::lerp_fill_style(a, b, eased_t);
}

template <>
inline graphics::StrokeStyle interpolate_values<graphics::StrokeStyle>(
    const graphics::StrokeStyle& a,
    const graphics::StrokeStyle& b,
    f32 t,
    EasingCurve e)
{
    if (!e.cubic.has_value() && e.preset == Easing::Hold) return a;
    const f32 eased_t = e.apply(t);
    return graphics::lerp_stroke_style(a, b, eased_t);
}

} // namespace chronon3d
