#pragma once

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>

namespace chronon3d {

// Premium material for 2.5D projected cards.
// Transforms a flat projected quad into a "3.5D" card with visible thickness,
// top→bottom gradient, edge highlight, and rim light.
struct Card3DMaterial {
    bool  enabled{true};

    // ── Geometry ───────────────────────────────────────────────────────────
    f32 thickness_px{14.0f};       // side face depth in screen pixels
    f32 corner_radius{28.0f};      // corner rounding (applied via rounded rect, not mesh)

    // ── Front face gradient ────────────────────────────────────────────────
    Color front_top_color{1.0f, 1.0f, 1.0f, 1.0f};
    Color front_bottom_color{0.85f, 0.87f, 0.92f, 1.0f};

    // ── Side faces ─────────────────────────────────────────────────────────
    Color side_color{0.65f, 0.67f, 0.72f, 1.0f};

    // ── Edge highlight ─────────────────────────────────────────────────────
    f32 edge_highlight_intensity{0.35f};
    Color edge_highlight_color{1.0f, 1.0f, 1.0f, 1.0f};

    // ── Rim light ──────────────────────────────────────────────────────────
    f32 rim_light_intensity{0.45f};
    f32 rim_light_power{2.2f};
    Color rim_light_color{1.0f, 1.0f, 1.0f, 1.0f};

    // ── Light direction (world-space, used for side face shading) ──────────
    Vec3 light_direction{0.577f, -0.577f, 0.577f}; // upper-right-front

    // ── Shadow interaction ─────────────────────────────────────────────────
    bool cast_shadow{true};
    bool receive_shadow{true};
    bool casts_shadows{true};
    bool accepts_shadows{true};

    // ── Presets ────────────────────────────────────────────────────────────
    static Card3DMaterial glass() {
        Card3DMaterial m;
        m.front_top_color    = {0.92f, 0.94f, 0.98f, 0.85f};
        m.front_bottom_color = {0.78f, 0.82f, 0.90f, 0.85f};
        m.side_color         = {0.55f, 0.60f, 0.70f, 0.70f};
        m.edge_highlight_intensity = 0.50f;
        m.rim_light_intensity      = 0.60f;
        return m;
    }

    static Card3DMaterial dark() {
        Card3DMaterial m;
        m.front_top_color    = {0.18f, 0.20f, 0.28f, 1.0f};
        m.front_bottom_color = {0.12f, 0.14f, 0.20f, 1.0f};
        m.side_color         = {0.08f, 0.10f, 0.15f, 1.0f};
        m.edge_highlight_intensity = 0.60f;
        m.edge_highlight_color     = {0.60f, 0.70f, 1.0f, 1.0f};
        m.rim_light_intensity      = 0.55f;
        m.rim_light_color          = {0.50f, 0.60f, 1.0f, 1.0f};
        return m;
    }

    static Card3DMaterial neon() {
        Card3DMaterial m;
        m.front_top_color    = {0.15f, 0.05f, 0.30f, 1.0f};
        m.front_bottom_color = {0.08f, 0.02f, 0.18f, 1.0f};
        m.side_color         = {0.05f, 0.01f, 0.12f, 1.0f};
        m.edge_highlight_intensity = 0.70f;
        m.edge_highlight_color     = {0.80f, 0.40f, 1.0f, 1.0f};
        m.rim_light_intensity      = 0.70f;
        m.rim_light_color          = {0.70f, 0.30f, 1.0f, 1.0f};
        return m;
    }

    static Card3DMaterial warm() {
        Card3DMaterial m;
        m.front_top_color    = {0.95f, 0.88f, 0.78f, 1.0f};
        m.front_bottom_color = {0.82f, 0.72f, 0.62f, 1.0f};
        m.side_color         = {0.65f, 0.55f, 0.45f, 1.0f};
        m.edge_highlight_intensity = 0.40f;
        m.edge_highlight_color     = {1.0f, 0.95f, 0.85f, 1.0f};
        return m;
    }

    /// Flat card — no thickness, just the shape (useful for non-3D layouts).
    static Card3DMaterial flat() {
        Card3DMaterial m;
        m.thickness_px         = 0.0f;
        m.edge_highlight_intensity = 0.0f;
        m.rim_light_intensity      = 0.0f;
        m.cast_shadow          = false;
        return m;
    }
};

} // namespace chronon3d
