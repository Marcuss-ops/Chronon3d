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

    // ── Query helpers ────────────────────────────────────────────────

    [[nodiscard]] bool is_solid()   const noexcept { return !gradient.has_value(); }
    [[nodiscard]] bool is_gradient() const noexcept { return  gradient.has_value(); }

    // ── Bridge to legacy Fill ────────────────────────────────────────

    /// Convert FillStyle to the legacy Fill struct for the existing render
    /// pipeline.  Handles Solid → FillType::Solid, Linear → LinearGradient,
    /// Radial → RadialGradient.  Opacity stops from GradientDefinition are
    /// flattened into the colour stop alpha during conversion.
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
    }
    // Unreachable (both enum values handled), but compilers may warn.
    return Fill::solid_color(solid_color);
}

inline ShapeStroke StrokeStyle::to_shape_stroke() const {
    ShapeStroke s;
    s.enabled   = enabled;
    s.color     = color;
    s.width     = width;
    s.alignment = alignment;
    return s;
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
    return s;
}

} // namespace chronon3d::graphics
