#pragma once

// ── StrokeStyle — gradient-aware stroke type ─────────────────────────────
//
// Extracted from fill_style.hpp (FASE 9 Step 1).
// A stroke with optional gradient fill.  Extends the capabilities of the
// legacy ShapeStroke / PathStroke types with gradient colour sources.
// Use .to_shape_stroke() / .to_path_stroke() to bridge to the renderer.

#include <chronon3d/graphics/gradient.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/scene/model/shape/path.hpp>
#include <optional>
#include <vector>

namespace chronon3d::graphics {

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

} // namespace chronon3d::graphics
