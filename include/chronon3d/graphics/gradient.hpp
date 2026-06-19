#pragma once

// ── Canonical Gradient Model ───────────────────────────────────────────────
//
// GradientDefinition is the single source of truth for gradient data.
// It is reusable across shapes, text, strokes, and backgrounds — the
// consumer (FillStyle, StrokeStyle, TextPaint) references it, not the
// other way around.
//
// Separated from scene/model/shape/fill.hpp so that graphics primitives
// don't depend on the scene layer.

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace chronon3d::graphics {

// ── Gradient type ──────────────────────────────────────────────────────

enum class GradientType : u8 {
    Linear,
    Radial,
    Conic,
};

// ── Spread mode (how coordinates outside [0, 1] are handled) ───────────

enum class GradientSpread : u8 {
    Pad,      // clamp to edge stops
    Repeat,   // tile the pattern
    Reflect,  // mirror-tile the pattern
};

// ── Stops ──────────────────────────────────────────────────────────────

/// A colour stop at a normalised position [0..1].
/// Colours are stored in sRGB (the authoring colour space) and converted
/// to linear by the sampler.
struct GradientStop {
    f32   position{0.0f};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
};

/// An independent opacity stop.  When empty, opacity is driven by
/// the colour stop's alpha.  When populated, the sampler multiplies
/// the interpolated colour alpha by the interpolated opacity.
struct OpacityStop {
    f32 position{0.0f};
    f32 opacity{1.0f};
};

// ── Gradient definition ────────────────────────────────────────────────

struct GradientDefinition {
    GradientType type{GradientType::Linear};

    // Colour ramp (at least two stops expected for a visible gradient).
    std::vector<GradientStop> color_stops;

    // Optional independent opacity ramp.
    std::vector<OpacityStop> opacity_stops;

    // ── Linear gradient geometry (in gradient-local normalised space) ──
    Vec2 start{0.0f, 0.0f};
    Vec2 end{1.0f, 0.0f};

    // ── Radial gradient geometry ──────────────────────────────────────
    Vec2 center{0.5f, 0.5f};
    f32  radius{0.5f};

    // ── Conic gradient geometry ──────────────────────────────────────
    f32  angle{0.0f};  // start angle in radians (used only for Conic)

    // ── Spread ────────────────────────────────────────────────────────
    GradientSpread spread{GradientSpread::Pad};

    // ── Factory helpers ───────────────────────────────────────────────

    static GradientDefinition linear(
        Vec2 s, Vec2 e,
        std::vector<GradientStop> stops,
        GradientSpread sp = GradientSpread::Pad)
    {
        // Stable-sort stops by ascending position so binary-search sampling
        // works and equal-position stops preserve insertion order (determinism).
        std::stable_sort(stops.begin(), stops.end(),
            [](const GradientStop& a, const GradientStop& b) {
                return a.position < b.position;
            });
        GradientDefinition def;
        def.set_type(GradientType::Linear;
        def.start       = s;
        def.end         = e;
        def.color_stops = std::move(stops);
        def.spread      = sp;
        return def;
    }

    static GradientDefinition radial(
        Vec2 c, f32 r,
        std::vector<GradientStop> stops,
        GradientSpread sp = GradientSpread::Pad)
    {
        // Stable-sort stops by ascending position so binary-search sampling
        // works and equal-position stops preserve insertion order (determinism).
        std::stable_sort(stops.begin(), stops.end(),
            [](const GradientStop& a, const GradientStop& b) {
                return a.position < b.position;
            });
        GradientDefinition def;
        def.set_type(GradientType::Radial;
        def.center      = c;
        def.radius      = r;
        def.color_stops = std::move(stops);
        def.spread      = sp;
        return def;
    }

    static GradientDefinition conic(
        Vec2 c, f32 a,
        std::vector<GradientStop> stops,
        GradientSpread sp = GradientSpread::Pad)
    {
        // Stable-sort stops by ascending position so binary-search sampling
        // works and equal-position stops preserve insertion order (determinism).
        std::stable_sort(stops.begin(), stops.end(),
            [](const GradientStop& a, const GradientStop& b) {
                return a.position < b.position;
            });
        GradientDefinition def;
        def.set_type(GradientType::Conic;
        def.center      = c;
        def.angle       = a;
        def.color_stops = std::move(stops);
        def.spread      = sp;
        return def;
    }
};

// ── Sampling helpers (header-only for inlining) ────────────────────────

namespace detail {

/// Map a raw progress value through the spread mode.
/// Returns the wrapped t ∈ [0, 1].
[[nodiscard]] inline f32 apply_spread(f32 raw, GradientSpread spread) {
    switch (spread) {
        case GradientSpread::Pad:
            return std::clamp(raw, 0.0f, 1.0f);
        case GradientSpread::Repeat: {
            f32 t = raw - std::floor(raw);
            // Handle negative by shifting into [0,1)
            if (t < 0.0f) t += 1.0f;
            return t;
        }
        case GradientSpread::Reflect: {
            // Map to [0, 2) sawtooth, then fold [1, 2) back to [0, 1]
            f32 t = raw - 2.0f * std::floor(raw * 0.5f);
            if (t < 0.0f) t += 2.0f;
            if (t > 1.0f) t = 2.0f - t;
            return t;
        }
    }
    return std::clamp(raw, 0.0f, 1.0f);
}

/// Interpolate colour stops at normalised t ∈ [0, 1].
/// Stops must be sorted by ascending position.
/// Returns linear-space colour.
[[nodiscard]] inline Color sample_color_stops(
    const std::vector<GradientStop>& stops, f32 t)
{
    if (stops.empty()) return Color{1.0f, 1.0f, 1.0f, 1.0f};
    if (stops.size() == 1) return stops[0].color.to_linear();

    t = std::clamp(t, 0.0f, 1.0f);

    // Binary search for the right stop of the segment containing t.
    // stops are guaranteed sorted by position (documented invariant).
    auto it = std::lower_bound(
        stops.begin(), stops.end(), t,
        [](const GradientStop& s, f32 val) { return s.position < val; });

    // it points to first stop with position >= t
    if (it == stops.begin()) {
        return stops.front().color.to_linear();
    }
    if (it == stops.end()) {
        return stops.back().color.to_linear();
    }

    const auto& b = *it;
    const auto& a = *(it - 1);

    const f32 range = b.position - a.position;
    const f32 local = (range > 1e-6f) ? (t - a.position) / range : 0.0f;

    const Color ca = a.color.to_linear();
    const Color cb = b.color.to_linear();

    return {
        ca.r + (cb.r - ca.r) * local,
        ca.g + (cb.g - ca.g) * local,
        ca.b + (cb.b - ca.b) * local,
        ca.a + (cb.a - ca.a) * local,
    };
}

/// Interpolate opacity stops at normalised t ∈ [0, 1].
/// Stops must be sorted by ascending position.
[[nodiscard]] inline f32 sample_opacity_stops(
    const std::vector<OpacityStop>& stops, f32 t)
{
    if (stops.empty()) return 1.0f;
    if (stops.size() == 1) return stops[0].opacity;

    t = std::clamp(t, 0.0f, 1.0f);

    // Binary search (same pattern as sample_color_stops).
    auto it = std::lower_bound(
        stops.begin(), stops.end(), t,
        [](const OpacityStop& s, f32 val) { return s.position < val; });

    if (it == stops.begin()) return stops.front().opacity;
    if (it == stops.end())   return stops.back().opacity;

    const auto& b = *it;
    const auto& a = *(it - 1);

    const f32 range = b.position - a.position;
    const f32 local = (range > 1e-6f) ? (t - a.position) / range : 0.0f;
    return a.opacity + (b.opacity - a.opacity) * local;
}

} // namespace detail

/// Sample a gradient definition at normalised linear progress t ∈ [0, 1].
/// Intended for Linear gradients.  For Radial gradients, use
/// sample_gradient_radial() instead to pass a 2D point.
/// Handles spread mode, colour interpolation, and opacity stops.
/// Returns linear-space colour.
[[nodiscard]] inline Color sample_gradient(
    const GradientDefinition& def, f32 t)
{
    const f32 wrapped = detail::apply_spread(t, def.spread);
    Color result = detail::sample_color_stops(def.color_stops, wrapped);

    if (!def.opacity_stops.empty()) {
        const f32 op = detail::sample_opacity_stops(def.opacity_stops, wrapped);
        result.a *= op;
    }

    return result;
}

/// Sample a radial gradient at a point in gradient-local space.
/// Returns linear-space colour.
[[nodiscard]] inline Color sample_gradient_radial(
    const GradientDefinition& def, const Vec2& point)
{
    const Vec2 delta = point - def.center;
    const f32  dist  = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    const f32  t     = (def.radius > 1e-6f) ? dist / def.radius : 0.0f;
    return sample_gradient(def, t);
}

} // namespace chronon3d::graphics
