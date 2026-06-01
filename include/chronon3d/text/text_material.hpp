#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>

namespace chronon3d {

// Premium material properties for text rendering.
// When enabled, the rasterized text is post-processed to apply gradient fill,
// bevel, top highlight, bottom shade, emissive boost, and optionally
// overrides the node-level glow and shadow with material-specific values.
struct TextMaterial {
    bool enabled{false};

    // ── Gradient fill (top → bottom) ──────────────────────────
    // The text fill interpolates linearly from top_color to bottom_color
    // based on the glyph's vertical position within the text block.
    Color top_color{1.0f, 1.0f, 1.0f, 1.0f};
    Color bottom_color{1.0f, 1.0f, 1.0f, 1.0f};

    // ── Bevel (fake 3D bevel thickness) ───────────────────────
    // Creates a highlighted edge on top-left and a shadowed edge on
    // bottom-right by offsetting the alpha mask and compositing.
    float bevel_px{0.0f};
    float bevel_highlight_opacity{0.35f};
    Color bevel_highlight_color{1.0f, 1.0f, 1.0f, 1.0f};
    float bevel_shadow_opacity{0.25f};

    // ── Top highlight strip ───────────────────────────────────
    // A subtle bright strip near glyph tops.
    float top_highlight_opacity{0.0f};
    float top_highlight_fraction{0.10f}; // fraction of glyph height

    // ── Bottom shade strip ────────────────────────────────────
    // A subtle dark strip near glyph bottoms.
    float bottom_shade_opacity{0.0f};
    float bottom_shade_fraction{0.08f}; // fraction of glyph height

    // ── Emissive / brightness boost ───────────────────────────
    // 1.0 = normal, >1 = brighter, <1 = dimmer.
    float emissive{1.0f};

    // ── Glow override ─────────────────────────────────────────
    // When use_material_glow is true, the node-level glow is replaced
    // with these values.
    bool use_material_glow{false};
    float glow_radius{8.0f};
    float glow_intensity{0.8f};
    Color glow_color{0.0f, 1.0f, 0.8f, 0.8f};

    // ── Shadow override ───────────────────────────────────────
    // When use_material_shadow is true, the node-level shadow is
    // replaced with these values.
    bool use_material_shadow{false};
    Vec2 shadow_offset{0.0f, 6.0f};
    float shadow_blur{8.0f};
    float shadow_opacity{0.5f};
    Color shadow_color{0.0f, 0.0f, 0.0f, 0.5f};

    // ── Presets ─────────────────────────────────────────────────

    /// Full premium look: gradient + bevel + highlights + glow + shadow.
    static TextMaterial premium() {
        TextMaterial m;
        m.enabled = true;
        m.top_color               = {1.0f, 1.0f, 1.0f, 1.0f};
        m.bottom_color            = {0.85f, 0.87f, 0.95f, 1.0f};
        m.bevel_px                = 1.5f;
        m.bevel_highlight_opacity = 0.45f;
        m.bevel_shadow_opacity    = 0.30f;
        m.top_highlight_opacity   = 0.25f;
        m.bottom_shade_opacity    = 0.15f;
        m.emissive                = 1.05f;
        m.use_material_glow       = true;
        m.glow_radius             = 12.0f;
        m.glow_intensity          = 0.60f;
        m.glow_color              = {0.6f, 0.7f, 1.0f, 0.7f};
        m.use_material_shadow     = true;
        m.shadow_offset           = {0.0f, 8.0f};
        m.shadow_blur             = 12.0f;
        m.shadow_opacity          = 0.45f;
        m.shadow_color            = {0.0f, 0.0f, 0.0f, 1.0f};
        return m;
    }

    /// Neon glow: bright gradient + strong glow.
    static TextMaterial neon() {
        TextMaterial m;
        m.enabled = true;
        m.top_color               = {0.3f, 1.0f, 0.9f, 1.0f};
        m.bottom_color            = {0.0f, 0.6f, 0.8f, 1.0f};
        m.bevel_px                = 0.0f;
        m.top_highlight_opacity   = 0.15f;
        m.emissive                = 1.20f;
        m.use_material_glow       = true;
        m.glow_radius             = 20.0f;
        m.glow_intensity          = 1.0f;
        m.glow_color              = {0.0f, 1.0f, 0.8f, 0.8f};
        m.use_material_shadow     = true;
        m.shadow_offset           = {0.0f, 4.0f};
        m.shadow_blur             = 6.0f;
        m.shadow_opacity          = 0.35f;
        m.shadow_color            = {0.0f, 0.0f, 0.0f, 0.6f};
        return m;
    }

    /// Glass: semi-transparent + subtle edge highlight.
    static TextMaterial glass() {
        TextMaterial m;
        m.enabled             = true;
        m.top_color           = {1.0f, 1.0f, 1.0f, 0.85f};
        m.bottom_color        = {0.9f, 0.92f, 1.0f, 0.70f};
        m.bevel_px            = 1.0f;
        m.bevel_highlight_opacity = 0.60f;
        m.top_highlight_opacity   = 0.30f;
        m.emissive            = 1.0f;
        m.use_material_glow   = true;
        m.glow_radius         = 6.0f;
        m.glow_intensity      = 0.35f;
        m.glow_color          = {0.8f, 0.85f, 1.0f, 0.5f};
        return m;
    }

    /// Flat: solid color, no effects.
    static TextMaterial flat() {
        TextMaterial m;
        m.enabled   = true;
        m.top_color = {1.0f, 1.0f, 1.0f, 1.0f};
        m.bottom_color = {1.0f, 1.0f, 1.0f, 1.0f};
        m.emissive  = 1.0f;
        return m;
    }
};

} // namespace chronon3d
