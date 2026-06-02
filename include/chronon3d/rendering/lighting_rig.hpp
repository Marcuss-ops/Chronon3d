#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/rendering/shadow_settings.hpp>
#include <chronon3d/rendering/light_context.hpp>
#include <glm/glm.hpp>

namespace chronon3d::rendering {

// Dedicated rim (Fresnel edge) light, independent of specular.
// Previously rim was derived from mat.specular * 0.18f — this struct gives
// explicit control over rim color, intensity, and falloff power.
struct RimLight {
    bool  enabled{false};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    f32   intensity{0.30f};
    f32   power{2.2f};   // Fresnel falloff exponent (higher = thinner edge)
};

// Designer-friendly lighting configuration.
//
// A LightingRig bundles a key directional light, rim light, ambient tone,
// background fill light, global exposure, and shadow presets into a single
// object that can be applied to a Scene via SceneBuilder::apply_lighting_rig().
//
// Presets are tuned to give professional motion-design looks with zero tweaking.
struct LightingRig {
    // ── Key light (main directional) ──────────────────────────
    Vec3  key_direction{glm::normalize(Vec3(-0.4f, 1.2f, -0.6f))};
    Color key_color{1.0f, 1.0f, 1.0f, 1.0f};
    f32   key_intensity{0.80f};

    // ── Ambient ──────────────────────────────────────────────
    Color ambient_color{0.6f, 0.65f, 0.7f, 1.0f};
    f32   ambient_intensity{0.25f};

    // ── Rim light (Fresnel edge glow, independent of specular) ──
    RimLight rim{};

    // ── Background radial light ───────────────────────────────
    // Tints the background with a soft radial gradient. The radius
    // is normalised (0 = centre, 1 = full frame).
    Color background_light_color{0.1f, 0.12f, 0.25f, 1.0f};
    f32   background_light_radius{0.60f};

    // ── Exposure (global brightness multiplier) ───────────────
    f32   exposure{1.0f};

    // ── Shadow presets ────────────────────────────────────────
    ShadowSettings shadows{};

    // ── Apply this rig to a LightContext (and optionally a RimLight out-param) ──
    void apply_to(LightContext& ctx, RimLight* rim_out = nullptr) const {
        ctx.enabled = true;
        ctx.ambient_enabled = true;
        ctx.directional_enabled = true;

        ctx.direction       = key_direction;
        ctx.directional_color = key_color;
        ctx.diffuse         = key_intensity;

        ctx.ambient_color   = ambient_color;
        ctx.ambient         = ambient_intensity;

        ctx.shadows         = shadows;

        if (rim_out) *rim_out = rim;
    }

    // ── Presets ─────────────────────────────────────────────────

    /// SaaS Blue — clean, professional blue-tinted hero lighting.
    static LightingRig SaaSBlue() {
        LightingRig rig;
        rig.key_direction    = glm::normalize(Vec3(-0.5f, 1.0f, -0.7f));
        rig.key_color        = {0.95f, 0.97f, 1.0f, 1.0f};
        rig.key_intensity    = 0.85f;
        rig.ambient_color    = {0.55f, 0.65f, 0.90f, 1.0f};
        rig.ambient_intensity = 0.22f;
        rig.rim.enabled      = true;
        rig.rim.color        = {0.60f, 0.75f, 1.0f, 1.0f};
        rig.rim.intensity    = 0.35f;
        rig.rim.power        = 2.5f;
        rig.background_light_color = {0.08f, 0.10f, 0.22f, 1.0f};
        rig.background_light_radius = 0.55f;
        rig.exposure         = 1.0f;
        rig.shadows.opacity  = 0.30f;
        rig.shadows.blur_radius = 10.0f;
        rig.shadows.contact_opacity  = 0.25f;
        rig.shadows.contact_blur_radius = 8.0f;
        rig.shadows.ambient_opacity    = 0.06f;
        rig.shadows.ambient_blur_radius = 80.0f;
        return rig;
    }

    /// Purple Neon — vibrant, high-contrast purple/cyan for tech/futuristic looks.
    static LightingRig PurpleNeon() {
        LightingRig rig;
        rig.key_direction    = glm::normalize(Vec3(-0.3f, 1.4f, -0.8f));
        rig.key_color        = {0.80f, 0.70f, 1.0f, 1.0f};
        rig.key_intensity    = 0.90f;
        rig.ambient_color    = {0.35f, 0.10f, 0.45f, 1.0f};
        rig.ambient_intensity = 0.18f;
        rig.rim.enabled      = true;
        rig.rim.color        = {0.40f, 0.90f, 1.0f, 1.0f};
        rig.rim.intensity    = 0.50f;
        rig.rim.power        = 2.0f;
        rig.background_light_color = {0.05f, 0.02f, 0.12f, 1.0f};
        rig.background_light_radius = 0.50f;
        rig.exposure         = 1.10f;
        rig.shadows.opacity  = 0.35f;
        rig.shadows.blur_radius     = 12.0f;
        rig.shadows.contact_opacity  = 0.30f;
        rig.shadows.contact_blur_radius = 10.0f;
        rig.shadows.ambient_opacity    = 0.08f;
        rig.shadows.ambient_blur_radius = 90.0f;
        return rig;
    }

    /// Apple Dark — premium, moody dark theme (inspired by Apple keynote floors).
    static LightingRig AppleDark() {
        LightingRig rig;
        rig.key_direction    = glm::normalize(Vec3(-0.6f, 0.8f, -0.5f));
        rig.key_color        = {0.98f, 0.96f, 0.92f, 1.0f};
        rig.key_intensity    = 0.70f;
        rig.ambient_color    = {0.20f, 0.20f, 0.22f, 1.0f};
        rig.ambient_intensity = 0.15f;
        rig.rim.enabled      = true;
        rig.rim.color        = {0.55f, 0.65f, 0.85f, 1.0f};
        rig.rim.intensity    = 0.25f;
        rig.rim.power        = 3.0f;
        rig.background_light_color = {0.02f, 0.02f, 0.04f, 1.0f};
        rig.background_light_radius = 0.40f;
        rig.exposure         = 1.0f;
        rig.shadows.opacity  = 0.40f;
        rig.shadows.blur_radius     = 14.0f;
        rig.shadows.contact_opacity  = 0.35f;
        rig.shadows.contact_blur_radius = 10.0f;
        rig.shadows.ambient_opacity    = 0.10f;
        rig.shadows.ambient_blur_radius = 100.0f;
        return rig;
    }

    /// Clean White — bright, balanced studio lighting for clean product shots.
    static LightingRig CleanWhite() {
        LightingRig rig;
        rig.key_direction    = glm::normalize(Vec3(-0.3f, 0.9f, -0.8f));
        rig.key_color        = {1.0f, 1.0f, 1.0f, 1.0f};
        rig.key_intensity    = 0.78f;
        rig.ambient_color    = {0.85f, 0.87f, 0.90f, 1.0f};
        rig.ambient_intensity = 0.30f;
        rig.rim.enabled      = true;
        rig.rim.color        = {0.90f, 0.92f, 1.0f, 1.0f};
        rig.rim.intensity    = 0.20f;
        rig.rim.power        = 2.8f;
        rig.background_light_color = {0.15f, 0.15f, 0.18f, 1.0f};
        rig.background_light_radius = 0.65f;
        rig.exposure         = 1.0f;
        rig.shadows.opacity  = 0.25f;
        rig.shadows.blur_radius     = 8.0f;
        rig.shadows.contact_opacity  = 0.20f;
        rig.shadows.contact_blur_radius = 6.0f;
        rig.shadows.ambient_opacity    = 0.04f;
        rig.shadows.ambient_blur_radius = 60.0f;
        return rig;
    }
};

} // namespace chronon3d::rendering
