#pragma once

// ── FillStyle / StrokeStyle — variant colour-source types ────────────────
//
// FillStyle is a discriminated union: solid colour OR gradient.
// StrokeStyle extends the legacy shape/stroke types with an optional gradient.
//
// Both reference chronon3d::graphics::GradientDefinition as the single source
// of truth for gradient data, keeping colour stops, geometry, spread mode and
// opacity stops in one canonical model.
//
// Each type provides a bridge to the legacy scene-layer Fill / ShapeStroke /
// PathStroke so that the existing render pipeline (shape_rasterizer,
// path_rasterizer, text_rasterizer) can consume FillStyle-authored content
// without modification.

#include <chronon3d/graphics/gradient.hpp>
#include <chronon3d/scene/model/shape/fill.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/scene/model/shape/path.hpp>
#include <optional>
#include <vector>

namespace chronon3d::graphics {

// ── FillStyle ────────────────────────────────────────────────────────
//
// A fill that is either a solid colour or a gradient.
// Drop-in replacement for the legacy Fill struct in scene model types.
// Use .to_fill() to bridge to the render pipeline.

struct FillStyle {
    bool enabled{true};

    /// Solid colour (used when gradient is nullopt).
    Color solid_color{1.0f, 1.0f, 1.0f, 1.0f};

    /// Optional gradient definition. When present, gradient overrides
    /// solid_color as the fill source.
    std::optional<GradientDefinition> gradient;

    // ── Factory helpers ──────────────────────────────────────────────

    static FillStyle solid(Color c) {
        return {true, c, std::nullopt};
    }

    static FillStyle linear(Vec2 from, Vec2 to,
                             std::vector<GradientStop> stops,
                             GradientSpread sp = GradientSpread::Pad) {
        return {true, {1.0f, 1.0f, 1.0f, 1.0f},
                GradientDefinition::linear(from, to, std::move(stops), sp)};
    }

    static FillStyle radial(Vec2 center, f32 radius,
                             std::vector<GradientStop> stops,
                             GradientSpread sp = GradientSpread::Pad) {
        return {true, {1.0f, 1.0f, 1.0f, 1.0f},
                GradientDefinition::radial(center, radius, std::move(stops), sp)};
    }

    static FillStyle conic(Vec2 center, f32 angle_rad,
                            std::vector<GradientStop> stops,
                            GradientSpread sp = GradientSpread::Pad) {
        return {true, {1.0f, 1.0f, 1.0f, 1.0f},
                GradientDefinition::conic(center, angle_rad, std::move(stops), sp)};
    }

    // ── Query helpers ────────────────────────────────────────────────

    [[nodiscard]] bool is_solid()   const noexcept { return !gradient.has_value(); }
    [[nodiscard]] bool is_gradient() const noexcept { return  gradient.has_value(); }

    // ── Bridge to legacy Fill ────────────────────────────────────────

    /// Convert FillStyle to the legacy Fill struct for the existing render
    /// pipeline.  Handles Solid → FillType::Solid, Linear → LinearGradient,
    /// Radial → RadialGradient, Conic → ConicGradient.
    /// Opacity stops from GradientDefinition are flattened into the colour
    /// stop alpha during conversion.
    Fill to_fill() const;
};


// ── StrokeStyle ─────────────────────────────────────────────────────
//
// A stroke with optional gradient fill.  Extends the capabilities of the
// legacy ShapeStroke / PathStroke types with gradient colour sources.
// Use .to_shape_stroke() / .to_path_stroke() to bridge to the renderer.

struct StrokeStyle {
    bool enabled{false};
    Color color{0.0f, 0.0f, 0.0f, 1.0f};
    f32  width{1.0f};
    StrokeAlignment alignment{StrokeAlignment::Center};

    // Path-specific stroke attributes (reused from PathStroke).
    LineCap             cap{LineCap::Butt};
    LineJoin            join{LineJoin::Miter};
    std::vector<f32>    dash_array{};
    f32                 dash_offset{0.0f};
    f32                 trim_start{0.0f};
    f32                 trim_end{1.0f};

    /// Optional gradient definition. When present, the stroke uses this
    /// gradient instead of the solid color.
    std::optional<GradientDefinition> gradient;

    // ── Factory helpers ──────────────────────────────────────────────

    static StrokeStyle solid(Color c, f32 w = 1.0f) {
        return {true, c, w};
    }

    static StrokeStyle linear_gradient(
        Vec2 from, Vec2 to,
        std::vector<GradientStop> stops,
        f32 w = 1.0f,
        GradientSpread sp = GradientSpread::Pad) {
        return {true, {1.0f, 1.0f, 1.0f, 1.0f}, w,
                StrokeAlignment::Center,
                LineCap::Butt, LineJoin::Miter, {}, 0.0f, 0.0f, 1.0f,
                GradientDefinition::linear(from, to, std::move(stops), sp)};
    }

    static StrokeStyle radial_gradient(
        Vec2 center, f32 radius,
        std::vector<GradientStop> stops,
        f32 w = 1.0f,
        GradientSpread sp = GradientSpread::Pad) {
        return {true, {1.0f, 1.0f, 1.0f, 1.0f}, w,
                StrokeAlignment::Center,
                LineCap::Butt, LineJoin::Miter, {}, 0.0f, 0.0f, 1.0f,
                GradientDefinition::radial(center, radius, std::move(stops), sp)};
    }

    static StrokeStyle conic_gradient(
        Vec2 center, f32 angle_rad,
        std::vector<GradientStop> stops,
        f32 w = 1.0f,
        GradientSpread sp = GradientSpread::Pad) {
        return {true, {1.0f, 1.0f, 1.0f, 1.0f}, w,
                StrokeAlignment::Center,
                LineCap::Butt, LineJoin::Miter, {}, 0.0f, 0.0f, 1.0f,
                GradientDefinition::conic(center, angle_rad, std::move(stops), sp)};
    }

    // ── Query helpers ────────────────────────────────────────────────

    [[nodiscard]] bool is_solid()   const noexcept { return !gradient.has_value(); }
    [[nodiscard]] bool is_gradient() const noexcept { return  gradient.has_value(); }

    // ── Bridge to legacy types ───────────────────────────────────────

    /// Convert to the legacy ShapeStroke (dropping gradient info — the
    /// gradient is only meaningful when the stroke is rendered through a
    /// gradient-aware path).
    ShapeStroke to_shape_stroke() const;

    /// Convert to the legacy PathStroke (dropping gradient info).
    PathStroke  to_path_stroke() const;
};


// ══════════════════════════════════════════════════════════════════════
//  Inline implementations
// ══════════════════════════════════════════════════════════════════════

inline Fill FillStyle::to_fill() const {
    // If disabled, return a disabled Fill regardless of gradient state.
    if (!enabled) {
        return {false, FillType::Solid, solid_color, {}};
    }
    if (!gradient.has_value()) {
        return Fill::solid_color(solid_color);
    }

    const auto& g = *gradient;

    // Convert GradientStop → Fill::GradientStop (same struct in same
    // namespace, but gradient.hpp uses chronon3d::graphics::GradientStop
    // while fill.hpp uses chronon3d::GradientStop — the member layout is
    // identical).
    auto convert_stops = [&]() -> std::vector<chronon3d::GradientStop> {
        std::vector<chronon3d::GradientStop> out;
        out.reserve(g.color_stops.size());
        for (const auto& s : g.color_stops) {
            chronon3d::GradientStop fs;
            fs.offset = s.position;
            fs.color  = s.color;
            // Blend independent opacity stops into the colour stop alpha.
            if (!g.opacity_stops.empty()) {
                const f32 op = detail::sample_opacity_stops(
                    g.opacity_stops, s.position);
                fs.color.a *= op;
            }
            out.push_back(fs);
        }
        return out;
    };

    switch (g.type) {
        case GradientType::Linear: {
            Fill f = Fill::linear(g.start, g.end, convert_stops());
            f.enabled = enabled;
            return f;
        }
        case GradientType::Radial: {
            Fill f = Fill::radial(g.center, g.radius, convert_stops());
            f.enabled = enabled;
            return f;
        }
        case GradientType::Conic: {
            Fill f = Fill::conic(g.center, g.angle, convert_stops());
            f.enabled = enabled;
            return f;
        }
    }
    // Unreachable (both enum values handled), but compilers may warn.
    return Fill::solid_color(solid_color);
}

inline PathStroke StrokeStyle::to_path_stroke() const {
    PathStroke s;
    s.enabled     = enabled;
    s.color       = color;
    s.width       = width;
    s.cap         = cap;
    s.join        = join;
    s.dash_array  = dash_array;
    s.dash_offset = dash_offset;
    s.trim_start  = trim_start;
    s.trim_end    = trim_end;

    // Bridge gradient if present (GradientDefinition → GradientFill).
    if (gradient.has_value()) {
        const auto& g = *gradient;
        GradientFill gf;
        gf.stops.reserve(g.color_stops.size());
        for (const auto& cs : g.color_stops) {
            chronon3d::GradientStop fs;
            fs.offset = cs.position;
            fs.color  = cs.color;
            // Blend opacity stops into colour stop alpha.
            if (!g.opacity_stops.empty()) {
                const f32 op = detail::sample_opacity_stops(
                    g.opacity_stops, cs.position);
                fs.color.a *= op;
            }
            gf.stops.push_back(std::move(fs));
        }
        // Map gradient geometry to GradientFill from/to.
        // For Linear: start→from, end→to.
        // For Radial: center→from, to encodes radius from center.
        // For Conic: center→from, to encodes angle direction.
        switch (g.type) {
            case GradientType::Linear:
                gf.type = FillType::LinearGradient;
                gf.from = g.start;
                gf.to   = g.end;
                break;
            case GradientType::Radial:
                gf.type = FillType::RadialGradient;
                gf.from = g.center;
                gf.to   = {g.center.x + g.radius, g.center.y};
                break;
            case GradientType::Conic:
                gf.type = FillType::ConicGradient;
                gf.from = g.center;
                gf.to   = {g.center.x + std::cos(g.angle),
                           g.center.y + std::sin(g.angle)};
                break;
        }
        s.gradient = std::move(gf);
    }

    return s;
}

inline ShapeStroke StrokeStyle::to_shape_stroke() const {
    ShapeStroke s;
    s.enabled   = enabled;
    s.color     = color;
    s.width     = width;
    s.alignment = alignment;

    // Bridge gradient if present (same conversion as to_path_stroke).
    if (gradient.has_value()) {
        const auto& g = *gradient;
        GradientFill gf;
        gf.stops.reserve(g.color_stops.size());
        for (const auto& cs : g.color_stops) {
            chronon3d::GradientStop fs;
            fs.offset = cs.position;
            fs.color  = cs.color;
            if (!g.opacity_stops.empty()) {
                const f32 op = detail::sample_opacity_stops(
                    g.opacity_stops, cs.position);
                fs.color.a *= op;
            }
            gf.stops.push_back(std::move(fs));
        }
        switch (g.type) {
            case GradientType::Linear:
                gf.type = FillType::LinearGradient;
                gf.from = g.start;
                gf.to   = g.end;
                break;
            case GradientType::Radial:
                gf.type = FillType::RadialGradient;
                gf.from = g.center;
                gf.to   = {g.center.x + g.radius, g.center.y};
                break;
            case GradientType::Conic:
                gf.type = FillType::ConicGradient;
                gf.from = g.center;
                gf.to   = {g.center.x + std::cos(g.angle),
                           g.center.y + std::sin(g.angle)};
                break;
        }
        s.gradient = std::move(gf);
    }

    return s;
}


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

    // Merge positions
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
///
/// For same-type gradients, geometry is lerped and colour stops are
/// resampled at merged positions then lerped.  Opacity stops are also
/// merged and lerped.
///
/// For different-type gradients, the result type follows the type of
/// the input closer to t=1 (i.e. switches at t=0.5) while stops are
/// resampled and lerped.
[[nodiscard]] inline GradientDefinition lerp_gradient(
    const GradientDefinition& a,
    const GradientDefinition& b,
    f32 t) noexcept
{
    // Determine result type — morph toward b's type as t → 1
    const GradientType result_type = (t < 0.5f) ? a.type : b.type;

    // Merge colour stop positions, sample both at each, lerp
    const auto positions = detail::merge_stop_positions(
        a.color_stops, b.color_stops);

    std::vector<GradientStop> lerped_stops;
    lerped_stops.reserve(positions.size());
    for (f32 pos : positions) {
        // Sample both gradients at this normalised position.
        // Use detail::sample_color_stops (no opacity baking) so that the
        // separately-lerped opacity stops in the result are the single
        // source of opacity — avoiding double-application of opacity.
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

    // Merge and lerp opacity stops (stored separately in the result so
    // they are applied once during rendering).
    const auto lerped_op = detail::lerp_opacity_stops(
        a.opacity_stops, b.opacity_stops, t);

    GradientDefinition result;
    result.type = result_type;
    result.color_stops = std::move(lerped_stops);
    result.opacity_stops = std::move(lerped_op);
    result.spread      = (t < 0.5f) ? a.spread : b.spread;

    // Lerp geometry based on result type
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
///
/// Both solid  → lerp colours.
/// One solid   → treat solid as a 1-stop gradient of the solid color,
///                then delegate to lerp_gradient.
/// Both gradient → delegate to lerp_gradient.
[[nodiscard]] inline FillStyle lerp_fill_style(
    const FillStyle& a,
    const FillStyle& b,
    f32 t) noexcept
{
    const bool a_solid = a.is_solid() || !a.enabled;
    const bool b_solid = b.is_solid() || !b.enabled;

    // Both solid: simple colour lerp
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

    // At least one is a gradient: convert solid(s) to 1-stop gradients
    const auto to_gdef = [](const FillStyle& fs) -> GradientDefinition {
        if (fs.gradient.has_value()) return *fs.gradient;
        // Solid colour → 1-stop gradient (same colour at both ends)
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
///
/// Both solid  → lerp colours + all scalar attributes.
/// One solid   → treat solid as a 1-stop gradient, then delegate gradient
///                interpolation to lerp_gradient.
/// Both gradient → delegate gradient interpolation to lerp_gradient.
///
/// Enum properties (alignment, cap, join) and dash_array switch at t=0.5.
/// Scalar properties (width, dash_offset, trim_start, trim_end) are lerped.
[[nodiscard]] inline StrokeStyle lerp_stroke_style(
    const StrokeStyle& a,
    const StrokeStyle& b,
    f32 t) noexcept
{
    StrokeStyle result;
    result.enabled = (t < 0.5f) ? a.enabled : b.enabled;

    // Lerp solid colour
    result.color = {
        a.color.r + (b.color.r - a.color.r) * t,
        a.color.g + (b.color.g - a.color.g) * t,
        a.color.b + (b.color.b - a.color.b) * t,
        a.color.a + (b.color.a - a.color.a) * t,
    };

    // Lerp scalar stroke attributes
    result.width       = a.width + (b.width - a.width) * t;
    result.dash_offset = a.dash_offset + (b.dash_offset - a.dash_offset) * t;
    result.trim_start  = a.trim_start + (b.trim_start - a.trim_start) * t;
    result.trim_end    = a.trim_end + (b.trim_end - a.trim_end) * t;

    // Enum properties — switch at t=0.5
    result.alignment = (t < 0.5f) ? a.alignment : b.alignment;
    result.cap       = (t < 0.5f) ? a.cap : b.cap;
    result.join      = (t < 0.5f) ? a.join : b.join;

    // Dash array — switch at t=0.5 (too complex to interpolate element-wise)
    result.dash_array = (t < 0.5f) ? a.dash_array : b.dash_array;

    // ── Gradient handling (same pattern as lerp_fill_style) ────────────
    const bool a_grad = a.gradient.has_value();
    const bool b_grad = b.gradient.has_value();

    if (!a_grad && !b_grad) {
        // Both solid — gradient stays nullopt, color already lerped above
        result.gradient = std::nullopt;
    } else {
        // At least one is a gradient: convert solid(s) to 1-stop gradients
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
// Wire FillStyle and StrokeStyle into the generic keyframe animation system.
// Without these, AnimatedValue<T> (aliased as KeyframeTrack<T>) would try to
// use operator+ and operator* on T, which are not defined for these types.

#include <chronon3d/animation/core/keyframe.hpp>

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
