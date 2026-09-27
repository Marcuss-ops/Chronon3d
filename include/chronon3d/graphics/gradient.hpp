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
#include <array>
#include <cmath>
#include <cstdint>
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
        def.type = GradientType::Linear;
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
        def.type = GradientType::Radial;
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
        def.type = GradientType::Conic;
        def.center      = c;
        def.angle       = a;
        def.color_stops = std::move(stops);
        def.spread      = sp;
        return def;
    }
};

/// Render-time lowering of GradientDefinition.
///
/// Authoring remains sRGB by contract, while this record owns linear stops
/// suitable for repeated sampling.  The conversion is therefore performed
/// once at the compiler/render-boundary instead of once per pixel.
struct CompiledGradientStop {
    f32 position{0.0f};
    Color linear_color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct CompiledGradient {
    GradientType type{GradientType::Linear};
    std::vector<CompiledGradientStop> color_stops;
    std::vector<OpacityStop> opacity_stops;
    Vec2 start{0.0f, 0.0f};
    Vec2 end{1.0f, 0.0f};
    Vec2 center{0.5f, 0.5f};
    f32 radius{0.5f};
    f32 angle{0.0f};
    GradientSpread spread{GradientSpread::Pad};
};

[[nodiscard]] inline CompiledGradient compile_gradient(
    const GradientDefinition& definition)
{
    CompiledGradient compiled;
    compiled.type = definition.type;
    compiled.opacity_stops = definition.opacity_stops;
    compiled.start = definition.start;
    compiled.end = definition.end;
    compiled.center = definition.center;
    compiled.radius = definition.radius;
    compiled.angle = definition.angle;
    compiled.spread = definition.spread;
    compiled.color_stops.reserve(definition.color_stops.size());
    for (const auto& stop : definition.color_stops) {
        compiled.color_stops.push_back(
            {stop.position, stop.color.to_linear()});
    }
    return compiled;
}

// ── Identity field sequence (Cat-3 single seat) ─────────────────────────
//
// The ordered list of GradientDefinition fields that participate in identity
// (cache keys, content fingerprints) is written ONCE, here.  Both identity
// encodings consume this sequence through their own sink:
//   - graph cache-key digest:  render_graph_hashing.hpp::hash_gradient
//   - render-plan content fingerprint:  src/render_plan/fingerprint.cpp
//
// Field selection AND order are part of the published fingerprint contract
// ("chronon3d.render-plan.content-fingerprint.v18"): never reorder or extend
// this sequence without bumping that version token in fingerprint.cpp.
//
// Sink contract: `sink(field)` accepts GradientType, GradientSpread, Vec2,
// f32 and Color values; each container size goes through `sink.count(n)`.
template <typename Sink>
void write_gradient_identity(const GradientDefinition& gradient, Sink& sink) {
    sink(gradient.type);
    sink(gradient.spread);
    sink(gradient.start);
    sink(gradient.end);
    sink(gradient.center);
    sink(gradient.radius);
    sink(gradient.angle);
    sink.count(gradient.color_stops.size());
    for (const auto& stop : gradient.color_stops) {
        sink(stop.position);
        sink(stop.color);
    }
    sink.count(gradient.opacity_stops.size());
    for (const auto& stop : gradient.opacity_stops) {
        sink(stop.position);
        sink(stop.opacity);
    }
}

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

/// Interpolate already-linearized stops.  This is the hot sampling path for
/// compiled gradients and intentionally contains no color-space conversion.
[[nodiscard]] inline Color sample_compiled_color_stops(
    const std::vector<CompiledGradientStop>& stops, f32 t)
{
    if (stops.empty()) return Color{1.0f, 1.0f, 1.0f, 1.0f};
    if (stops.size() == 1) return stops[0].linear_color;

    t = std::clamp(t, 0.0f, 1.0f);
    auto it = std::lower_bound(
        stops.begin(), stops.end(), t,
        [](const CompiledGradientStop& s, f32 val) {
            return s.position < val;
        });
    if (it == stops.begin()) return stops.front().linear_color;
    if (it == stops.end()) return stops.back().linear_color;

    const auto& b = *it;
    const auto& a = *(it - 1);
    const f32 range = b.position - a.position;
    const f32 local = (range > 1e-6f) ? (t - a.position) / range : 0.0f;
    return {
        a.linear_color.r + (b.linear_color.r - a.linear_color.r) * local,
        a.linear_color.g + (b.linear_color.g - a.linear_color.g) * local,
        a.linear_color.b + (b.linear_color.b - a.linear_color.b) * local,
        a.linear_color.a + (b.linear_color.a - a.linear_color.a) * local,
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

[[nodiscard]] inline Color sample_compiled_gradient(
    const CompiledGradient& gradient, f32 t)
{
    const f32 wrapped = detail::apply_spread(t, gradient.spread);
    Color result = detail::sample_compiled_color_stops(
        gradient.color_stops, wrapped);
    if (!gradient.opacity_stops.empty()) {
        result.a *= detail::sample_opacity_stops(
            gradient.opacity_stops, wrapped);
    }
    return result;
}

/// Apply stable 8x8 ordered dithering to a sampled linear-light gradient.
/// This is a renderer quality adjustment, not authored scene state, so it has
/// no effect on plan fingerprints. Coordinates are pixel-space coordinates.
[[nodiscard]] inline Color dither_gradient_sample(Color color, f32 x, f32 y) noexcept {
    constexpr std::array<std::uint8_t, 64> bayer{
         0, 32,  8, 40,  2, 34, 10, 42,
        48, 16, 56, 24, 50, 18, 58, 26,
        12, 44,  4, 36, 14, 46,  6, 38,
        60, 28, 52, 20, 62, 30, 54, 22,
         3, 35, 11, 43,  1, 33,  9, 41,
        51, 19, 59, 27, 49, 17, 57, 25,
        15, 47,  7, 39, 13, 45,  5, 37,
        63, 31, 55, 23, 61, 29, 53, 21};
    const auto ix = static_cast<unsigned>(static_cast<std::int64_t>(std::floor(x))) & 7u;
    const auto iy = static_cast<unsigned>(static_cast<std::int64_t>(std::floor(y))) & 7u;
    const f32 offset = ((static_cast<f32>(bayer[iy * 8u + ix]) + 0.5f) / 64.0f - 0.5f)
        / 255.0f;
    color.r = std::clamp(color.r + offset, 0.0f, 1.0f);
    color.g = std::clamp(color.g + offset, 0.0f, 1.0f);
    color.b = std::clamp(color.b + offset, 0.0f, 1.0f);
    return color;
}

[[nodiscard]] inline Color sample_compiled_gradient_radial(
    const CompiledGradient& gradient, const Vec2& point)
{
    const Vec2 delta = point - gradient.center;
    const f32 dist = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    const f32 t = (gradient.radius > 1e-6f) ? dist / gradient.radius : 0.0f;
    return sample_compiled_gradient(gradient, t);
}

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
