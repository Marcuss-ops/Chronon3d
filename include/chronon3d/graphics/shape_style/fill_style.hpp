#pragma once

// ── FillStyle — variant colour-source type ────────────────────────────────
//
// FillStyle is a discriminated union: solid colour OR gradient.
// StrokeStyle has been extracted into stroke_style.hpp (FASE 9 Step 1).
//
// References chronon3d::graphics::GradientDefinition as the single source
// of truth for gradient data, keeping colour stops, geometry, spread mode and
// opacity stops in one canonical model.
//
// Provides a bridge to the legacy scene-layer Fill so that the existing
// render pipeline can consume FillStyle-authored content without modification.

#include <chronon3d/graphics/gradient.hpp>
#include <chronon3d/graphics/shape_style/stroke_style.hpp>  // FASE 9 — StrokeStyle extracted
#include <chronon3d/scene/model/shape/fill.hpp>
#include <optional>
#include <vector>

namespace chronon3d::graphics {

// ── FillStyle ────────────────────────────────────────────────────────

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

    // ── Bridge to scene Fill, retaining the canonical gradient ────────

    Fill to_fill() const;
};


// ══════════════════════════════════════════════════════════════════════
//  Inline implementations
// ══════════════════════════════════════════════════════════════════════

inline Fill FillStyle::to_fill() const {
    if (!enabled) return {false, FillType::Solid, solid_color, {}};
    if (!gradient) return Fill::solid_color(solid_color);

    Fill fill;
    fill.enabled = true;
    fill.solid = solid_color;
    fill.gradient = *gradient;
    switch (gradient->type) {
        case GradientType::Linear: fill.type = FillType::LinearGradient; break;
        case GradientType::Radial: fill.type = FillType::RadialGradient; break;
        case GradientType::Conic: fill.type = FillType::ConicGradient; break;
    }
    return fill;
}


} // namespace chronon3d::graphics

// Lerp / interpolation helpers extracted to fill_style_lerp.hpp (FASE 9 Step 2).
// Include that header when you need FillStyle/StrokeStyle animation lerp.
#include <chronon3d/graphics/shape_style/fill_style_lerp.hpp>
