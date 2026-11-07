#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/text/gel_surface.hpp>
#include <cstdint>

#ifdef CHRONON3D_USE_BLEND2D
class BLImage;
#endif

namespace chronon3d {

// Premium material properties for text rendering.
// When enabled, the rasterized text is post-processed to apply gradient fill,
// bevel, top highlight, bottom shade, inner shadow and emissive boost.
struct TextMaterial {
    bool enabled{false};
    // Native MTSDF gel profile. The fill color remains authored by TextPaint;
    // this flag selects the Vulkan distance-field lighting treatment.
    bool gel_surface{false};
    // Build a shared soft dome where GEL glyphs touch or overlap. False keeps
    // the exact hard union while retaining a shared TextRun surface.
    bool gel_soft_fusion{true};
    // Selects real closed 3D text geometry at the TextRun source boundary.
    // This is independent of the screen-space GlossyGelText surface path.
    bool extruded_text{false};
    float extrusion_depth{12.0f};
    // Allows the native Vulkan gel pass to sample and tint the already
    // composited destination locally. Kept enabled by default for compatibility.
    bool gel_transmission_enabled{true};
    // Diagnostic / Phase-A mode: evaluate the composite TextRun distance field
    // and luminous cyan face gradient (core + body + edge) with specular and
    // rim lighting disabled.
    bool gel_core_only{false};
    // Diagnostic output: 0=material, 1=SDF, 2=height, 3=normals,
    // 4=normal validity heat map, 5=gel transmission flag/destination alpha.
    std::uint8_t gel_debug_mode{0};
    // Gel key light in local screen space. Elevation is the normalized Z
    // component of the unit light vector (0=grazing, 1=zenith).
    float gel_light_azimuth_radians{text::gel::kDefaultLightAzimuth};
    float gel_light_elevation{text::gel::kDefaultLightElevation};

    // ── Gradient fill (top → bottom) ──────────────────────────
    // The text fill interpolates linearly from top_color to bottom_color
    // based on the glyph's vertical position within the text block.
    Color top_color{1.0f, 1.0f, 1.0f, 1.0f};
    Color bottom_color{1.0f, 1.0f, 1.0f, 1.0f};
    float gradient_angle{90.0f}; // 90 = vertical (top -> bottom), 0 = horizontal (left -> right)

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

    // ── Inner Shadow ──────────────────────────────────────────
    // Creates a shadow cast inside the glyph boundaries.
    bool inner_shadow_enabled{false};
    Vec2 inner_shadow_offset{0.0f, 2.0f};
    float inner_shadow_blur{4.0f};
    Color inner_shadow_color{0.0f, 0.0f, 0.0f, 0.8f};

    // ── Emissive / brightness boost ───────────────────────────
    // 1.0 = normal, >1 = brighter, <1 = dimmer.
    float emissive{1.0f};

    // ── Presets ─────────────────────────────────────────────────

    /// Full premium look: gradient + bevel + highlights + emissive finish.
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
        return m;
    }

    /// Neon: bright gradient with an emissive finish.
    static TextMaterial neon() {
        TextMaterial m;
        m.enabled = true;
        m.top_color               = {0.3f, 1.0f, 0.9f, 1.0f};
        m.bottom_color            = {0.0f, 0.6f, 0.8f, 1.0f};
        m.bevel_px                = 0.0f;
        m.top_highlight_opacity   = 0.15f;
        m.emissive                = 1.20f;
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
        return m;
    }

    /// Glossy, rounded cyan gel surface evaluated directly from the MTSDF.
    static TextMaterial gel() {
        TextMaterial m;
        m.enabled = true;
        m.gel_surface = true;
        m.emissive = 1.0f;
        return m;
    }

    /// Luminous core + body + edge gradient from the composite MTSDF without
    /// specular or rim reflections (GEL-01 Phase A diagnostic / base core).
    static TextMaterial gel_core() {
        TextMaterial m;
        m.enabled = true;
        m.gel_surface = true;
        m.gel_core_only = true;
        m.emissive = 1.0f;
        return m;
    }

    /// Real TextRun outline mesh with front/back caps and side walls.
    static TextMaterial extruded_3d(float depth = 12.0f) {
        TextMaterial m;
        m.enabled = true;
        m.extruded_text = true;
        m.extrusion_depth = depth;
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

#ifdef CHRONON3D_USE_BLEND2D
struct TextScratchState;

/// Apply the canonical text material to a rasterized text image in-place.
void apply_text_material(BLImage& img, const TextMaterial& material,
                         TextScratchState& scratch);
#endif

} // namespace chronon3d
