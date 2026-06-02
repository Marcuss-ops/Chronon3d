#pragma once

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/color.hpp>

namespace chronon3d {

// Fake 3D card material — gives a projected card the appearance of a
// physical object with thickness, edge highlights, and rim lighting.
//
// Rather than using a real mesh, the card is rendered as:
//   1. Front face (the main projected quad)
//   2. Right side face (fake extrusion using thickness_px)
//   3. Bottom side face (fake extrusion using thickness_px)
//   4. Thin edge highlight strip along exposed edges
//   5. Rim light contribution (Fresnel-style edge glow)
//
// This approach keeps rendering cheap (no mesh, no meshoptimizer)
// while producing a convincing 3D look suitable for motion graphics.
struct Card3DMaterial {
    bool  enabled{false};

    // ── Thickness (fake extrusion depth in screen pixels) ────────
    f32   thickness_px{14.0f};

    // ── Face colors ──────────────────────────────────────────────
    // The front face is typically the rounded_rect fill; these
    // tint the side quads drawn to simulate extrusion.
    Color front_top_color{1.0f, 1.0f, 1.0f, 1.0f};
    Color front_bottom_color{0.92f, 0.93f, 0.97f, 1.0f};
    Color side_color{0.55f, 0.55f, 0.62f, 1.0f};

    // ── Edge highlight ───────────────────────────────────────────
    // A thin bright strip along the top and left exposed edges
    // of the card, simulating a chamfer catching light.
    f32   edge_highlight_opacity{0.35f};
    Color edge_highlight_color{1.0f, 1.0f, 1.0f, 1.0f};

    // ── Rim light (Fresnel edge glow) ────────────────────────────
    // Adds a coloured glow around the card edges that responds to
    // the lighting rig's rim light direction.
    f32   rim_intensity{0.45f};
    f32   rim_power{2.2f};

    // ── Corner radius ────────────────────────────────────────────
    // Rounded corners for the side quads.  Matches the front face
    // rounded_rect radius for visual consistency.
    f32   corner_radius{28.0f};

    // ── Shadow interaction ───────────────────────────────────────
    bool  cast_shadow{true};
    bool  receive_shadow{true};

    // ── Presets ──────────────────────────────────────────────────

    /// Glass card — translucent with subtle edge glow.
    static Card3DMaterial glass_card() {
        Card3DMaterial m;
        m.enabled              = true;
        m.thickness_px         = 10.0f;
        m.front_top_color      = {0.92f, 0.94f, 1.0f, 0.82f};
        m.front_bottom_color   = {0.82f, 0.85f, 0.95f, 0.75f};
        m.side_color           = {0.50f, 0.55f, 0.70f, 0.60f};
        m.edge_highlight_opacity = 0.50f;
        m.edge_highlight_color = {0.95f, 0.97f, 1.0f, 1.0f};
        m.rim_intensity        = 0.30f;
        m.rim_power            = 2.8f;
        m.corner_radius        = 20.0f;
        return m;
    }

    /// Dark card — matte dark with crisp white edge.
    static Card3DMaterial dark_card() {
        Card3DMaterial m;
        m.enabled              = true;
        m.thickness_px         = 12.0f;
        m.front_top_color      = {0.08f, 0.08f, 0.14f, 0.95f};
        m.front_bottom_color   = {0.05f, 0.05f, 0.10f, 0.95f};
        m.side_color           = {0.04f, 0.04f, 0.08f, 0.90f};
        m.edge_highlight_opacity = 0.25f;
        m.edge_highlight_color = {0.40f, 0.50f, 0.80f, 1.0f};
        m.rim_intensity        = 0.50f;
        m.rim_power            = 2.0f;
        m.corner_radius        = 24.0f;
        return m;
    }

    /// Premium card — rich gradient with strong rim.
    static Card3DMaterial premium_card() {
        Card3DMaterial m;
        m.enabled              = true;
        m.thickness_px         = 14.0f;
        m.front_top_color      = {0.10f, 0.10f, 0.20f, 0.92f};
        m.front_bottom_color   = {0.06f, 0.06f, 0.12f, 0.92f};
        m.side_color           = {0.04f, 0.04f, 0.08f, 0.88f};
        m.edge_highlight_opacity = 0.40f;
        m.edge_highlight_color = {0.50f, 0.65f, 1.0f, 1.0f};
        m.rim_intensity        = 0.55f;
        m.rim_power            = 2.2f;
        m.corner_radius        = 28.0f;
        return m;
    }

    /// Flat card — no thickness, just the shape (useful for non-3D layouts).
    static Card3DMaterial flat() {
        Card3DMaterial m;
        m.enabled              = true;
        m.thickness_px         = 0.0f;
        m.edge_highlight_opacity = 0.0f;
        m.rim_intensity        = 0.0f;
        m.cast_shadow          = false;
        return m;
    }
};

} // namespace chronon3d
