#pragma once

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/rendering/image_lighting.hpp>
#include <chronon3d/rendering/light_context.hpp>
#include <chronon3d/rendering/lighting_rig.hpp>
#include <chronon3d/scene/model/shape/material_2_5d.hpp>
#include <chronon3d/text/text_material.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

namespace chronon3d::rendering::ae_style {

/// 10 curated single-object 2.5D relighting presets for classical sculpture /
/// philosopher bust compositions (1920x1080 default framing).
enum class BustLightingPreset : std::uint8_t {
    HorizontalPointSweep = 0,
    CircularOrbitKey,
    WarmCoolDualOpposition,
    DramaticOverheadSpotlight,
    HighReliefChiseled,
    SilhouetteRimHalo,
    CloseProximityFalloff,
    DiagonalCrossSweep,
    LowAngleUpwardTorch,
    MultiColorCrossSweep,
};

[[nodiscard]] constexpr std::array<BustLightingPreset, 10> all_bust_lighting_presets() noexcept {
    return {
        BustLightingPreset::HorizontalPointSweep,
        BustLightingPreset::CircularOrbitKey,
        BustLightingPreset::WarmCoolDualOpposition,
        BustLightingPreset::DramaticOverheadSpotlight,
        BustLightingPreset::HighReliefChiseled,
        BustLightingPreset::SilhouetteRimHalo,
        BustLightingPreset::CloseProximityFalloff,
        BustLightingPreset::DiagonalCrossSweep,
        BustLightingPreset::LowAngleUpwardTorch,
        BustLightingPreset::MultiColorCrossSweep,
    };
}

[[nodiscard]] constexpr std::string_view to_string(BustLightingPreset preset) noexcept {
    switch (preset) {
        case BustLightingPreset::HorizontalPointSweep:
            return "horizontal_point_sweep";
        case BustLightingPreset::CircularOrbitKey:
            return "circular_orbit_key";
        case BustLightingPreset::WarmCoolDualOpposition:
            return "warm_cool_dual_opposition";
        case BustLightingPreset::DramaticOverheadSpotlight:
            return "dramatic_overhead_spotlight";
        case BustLightingPreset::HighReliefChiseled:
            return "high_relief_chiseled";
        case BustLightingPreset::SilhouetteRimHalo:
            return "silhouette_rim_halo";
        case BustLightingPreset::CloseProximityFalloff:
            return "close_proximity_falloff";
        case BustLightingPreset::DiagonalCrossSweep:
            return "diagonal_cross_sweep";
        case BustLightingPreset::LowAngleUpwardTorch:
            return "low_angle_upward_torch";
        case BustLightingPreset::MultiColorCrossSweep:
            return "multi_color_cross_sweep";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// Text Style Presets (2.5D Glass/Gel Refraction & 3D-like Beveled Extrusion)
// ---------------------------------------------------------------------------

/// 2.5D translucent glass/gel text with optical Snell refraction (IOR 1.45),
/// soft background fusion, and crisp specular bevel highlights.
[[nodiscard]] inline TextMaterial gel_refraction_25d(
    f32 bevel_px = 3.0f,
    f32 azimuth_radians = 0.7853981633974483f,
    f32 elevation = 0.65f) {
    TextMaterial mat = TextMaterial::gel();
    mat.bevel_px = std::clamp(bevel_px, 0.0f, 32.0f);
    mat.bevel_highlight_opacity = 0.42f;
    mat.bevel_shadow_opacity = 0.28f;
    mat.gel_light_azimuth_radians = azimuth_radians;
    mat.gel_light_elevation = std::clamp(elevation, 0.05f, 1.5f);
    return mat;
}

/// 3D extruded mesh text with chamfered bevels (`extrusion_bevel`) and front
/// surface shading (`bevel_px`).
[[nodiscard]] inline TextMaterial extruded_3d_bevel(
    f32 extrusion_depth = 16.0f,
    f32 extrusion_bevel = 3.5f,
    f32 bevel_px = 2.0f) {
    TextMaterial mat = TextMaterial::extruded_3d(
        std::clamp(extrusion_depth, 0.0f, 256.0f),
        std::clamp(extrusion_bevel, 0.0f, 64.0f));
    mat.bevel_px = std::clamp(bevel_px, 0.0f, 32.0f);
    mat.bevel_highlight_opacity = 0.35f;
    mat.bevel_shadow_opacity = 0.32f;
    return mat;
}

/// High-contrast After Effects-style 3D-like beveled title card preset.
[[nodiscard]] inline TextMaterial beveled_motion_3d_like() {
    TextMaterial mat = TextMaterial::extruded_3d(12.0f, 4.0f);
    mat.bevel_px = 4.0f;
    mat.bevel_highlight_opacity = 0.50f;
    mat.bevel_shadow_opacity = 0.40f;
    return mat;
}

// ---------------------------------------------------------------------------
// Single-Object Philosopher Bust 2.5D Relighting Presets
// ---------------------------------------------------------------------------

[[nodiscard]] inline Material2_5D make_bust_material(BustLightingPreset preset) {
    Material2_5D mat{};
    mat.accepts_lights = true;
    mat.casts_shadows = false;
    mat.accepts_shadows = false;
    mat.diffuse = 1.0f;
    mat.ambient_multiplier = 0.35f;
    mat.image_rim_width_px = 5.0f;

    switch (preset) {
        case BustLightingPreset::HorizontalPointSweep:
            mat.image_bump_strength = 0.85f;
            mat.specular = 0.45f;
            mat.roughness = 0.38f;
            mat.image_rim_strength = 0.35f;
            break;
        case BustLightingPreset::CircularOrbitKey:
            mat.image_bump_strength = 0.90f;
            mat.specular = 0.50f;
            mat.roughness = 0.35f;
            mat.image_rim_strength = 0.45f;
            break;
        case BustLightingPreset::WarmCoolDualOpposition:
            mat.image_bump_strength = 0.80f;
            mat.specular = 0.48f;
            mat.roughness = 0.40f;
            mat.image_rim_strength = 0.50f;
            break;
        case BustLightingPreset::DramaticOverheadSpotlight:
            mat.image_bump_strength = 0.95f;
            mat.specular = 0.55f;
            mat.roughness = 0.32f;
            mat.ambient_multiplier = 0.18f;
            mat.image_rim_strength = 0.40f;
            break;
        case BustLightingPreset::HighReliefChiseled:
            mat.image_bump_strength = 1.35f;
            mat.specular = 0.72f;
            mat.roughness = 0.22f;
            mat.ambient_multiplier = 0.22f;
            mat.image_rim_strength = 0.55f;
            break;
        case BustLightingPreset::SilhouetteRimHalo:
            mat.image_bump_strength = 0.70f;
            mat.specular = 0.40f;
            mat.roughness = 0.35f;
            mat.ambient_multiplier = 0.12f;
            mat.image_rim_strength = 0.95f;
            mat.image_rim_width_px = 7.0f;
            break;
        case BustLightingPreset::CloseProximityFalloff:
            mat.image_bump_strength = 0.95f;
            mat.specular = 0.62f;
            mat.roughness = 0.28f;
            mat.ambient_multiplier = 0.20f;
            mat.image_rim_strength = 0.30f;
            break;
        case BustLightingPreset::DiagonalCrossSweep:
            mat.image_bump_strength = 0.88f;
            mat.specular = 0.52f;
            mat.roughness = 0.34f;
            mat.image_rim_strength = 0.42f;
            break;
        case BustLightingPreset::LowAngleUpwardTorch:
            mat.image_bump_strength = 1.05f;
            mat.specular = 0.58f;
            mat.roughness = 0.30f;
            mat.ambient_multiplier = 0.16f;
            mat.image_rim_strength = 0.48f;
            break;
        case BustLightingPreset::MultiColorCrossSweep:
            mat.image_bump_strength = 0.92f;
            mat.specular = 0.60f;
            mat.roughness = 0.28f;
            mat.ambient_multiplier = 0.20f;
            mat.image_rim_strength = 0.65f;
            break;
    }
    return mat;
}

[[nodiscard]] inline ImageLightingParams make_bust_image_params(BustLightingPreset preset) {
    const Material2_5D mat = make_bust_material(preset);
    ImageLightingParams params{};
    params.bump_strength = mat.image_bump_strength;
    params.roughness = mat.roughness.value_or(0.38f);
    params.specular_intensity = std::clamp(mat.specular, 0.0f, 1.0f);
    params.diffuse = mat.diffuse;
    params.ambient_multiplier = mat.ambient_multiplier;
    params.rim_strength = mat.image_rim_strength;
    params.rim_width_px = mat.image_rim_width_px;
    params.rim_power = 2.0f;
    params.light_wrap_strength = mat.image_light_wrap_strength;
    return params;
}

[[nodiscard]] inline LightingRig make_bust_lighting_rig(BustLightingPreset preset) {
    LightingRig rig{};
    rig.key_direction = safe_normalize(Vec3{-0.35f, -0.45f, 0.82f});
    rig.key_color = Color{1.0f, 0.97f, 0.92f, 1.0f};
    rig.key_intensity = 0.20f;
    rig.ambient_color = Color{0.18f, 0.20f, 0.26f, 1.0f};
    rig.ambient_intensity = 0.14f;
    rig.exposure = 1.0f;
    rig.rim.enabled = true;
    rig.rim.color = Color{0.85f, 0.90f, 1.0f, 1.0f};
    rig.rim.intensity = 0.35f;
    rig.rim.power = 2.4f;

    if (preset == BustLightingPreset::SilhouetteRimHalo) {
        rig.ambient_intensity = 0.06f;
        rig.key_intensity = 0.08f;
        rig.rim.intensity = 0.85f;
        rig.rim.color = Color{0.65f, 0.82f, 1.0f, 1.0f};
    } else if (preset == BustLightingPreset::DramaticOverheadSpotlight ||
               preset == BustLightingPreset::LowAngleUpwardTorch) {
        rig.ambient_intensity = 0.08f;
        rig.key_intensity = 0.12f;
    }
    return rig;
}

/// Evaluates animated point/spot light positions for `t_norm` in [0, 1] over
/// a canvas of `canvas_size` pixels (default 1920x1080).
[[nodiscard]] inline std::vector<PointLightSource> sample_bust_sweep_lights(
    BustLightingPreset preset,
    f32 t_norm,
    Vec2 canvas_size = {1920.0f, 1080.0f}) {
    const f32 t = std::clamp(t_norm, 0.0f, 1.0f);
    const f32 cx = canvas_size.x * 0.5f;
    const f32 cy = canvas_size.y * 0.5f;
    const f32 scale_x = canvas_size.x / 1920.0f;
    const f32 scale_y = canvas_size.y / 1080.0f;
    constexpr f32 kTwoPi = 6.283185307179586f;

    auto scaled_color = [](Color c, f32 intensity) {
        return Color{c.r * intensity, c.g * intensity, c.b * intensity, 1.0f};
    };

    std::vector<PointLightSource> lights;
    switch (preset) {
        case BustLightingPreset::HorizontalPointSweep: {
            PointLightSource key{};
            key.kind = PointLightKind::Point;
            key.position = {std::lerp(240.0f, 1680.0f, t) * scale_x, 380.0f * scale_y, 420.0f};
            key.color = scaled_color({1.0f, 0.94f, 0.84f, 1.0f}, 2.4f);
            key.radius = 650.0f * scale_x;
            key.decay = 1.0f;
            lights.push_back(key);
            break;
        }
        case BustLightingPreset::CircularOrbitKey: {
            const f32 angle = t * kTwoPi;
            PointLightSource key{};
            key.kind = PointLightKind::Point;
            key.position = {
                cx + std::cos(angle) * 560.0f * scale_x,
                cy + std::sin(angle) * 340.0f * scale_y,
                380.0f};
            key.color = scaled_color({1.0f, 0.92f, 0.80f, 1.0f}, 2.5f);
            key.radius = 620.0f * scale_x;
            key.decay = 1.0f;
            lights.push_back(key);
            break;
        }
        case BustLightingPreset::WarmCoolDualOpposition: {
            PointLightSource warm{};
            warm.kind = PointLightKind::Point;
            warm.position = {std::lerp(220.0f, 1700.0f, t) * scale_x, 420.0f * scale_y, 380.0f};
            warm.color = scaled_color({1.0f, 0.65f, 0.30f, 1.0f}, 2.3f);
            warm.radius = 600.0f * scale_x;

            PointLightSource cool{};
            cool.kind = PointLightKind::Point;
            cool.position = {std::lerp(1700.0f, 220.0f, t) * scale_x, 660.0f * scale_y, 380.0f};
            cool.color = scaled_color({0.35f, 0.72f, 1.0f, 1.0f}, 2.0f);
            cool.radius = 600.0f * scale_x;
            lights.push_back(warm);
            lights.push_back(cool);
            break;
        }
        case BustLightingPreset::DramaticOverheadSpotlight: {
            PointLightSource spot{};
            spot.kind = PointLightKind::Spot;
            spot.position = {std::lerp(420.0f, 1500.0f, t) * scale_x, 110.0f * scale_y, 520.0f};
            spot.direction = safe_normalize(Vec3{cx - spot.position.x, cy - spot.position.y, -520.0f});
            spot.color = scaled_color({1.0f, 0.96f, 0.88f, 1.0f}, 3.2f);
            spot.radius = 750.0f * scale_x;
            spot.angle = 0.58f;
            spot.penumbra = 0.45f;
            lights.push_back(spot);
            break;
        }
        case BustLightingPreset::HighReliefChiseled: {
            PointLightSource graze{};
            graze.kind = PointLightKind::Point;
            graze.position = {std::lerp(180.0f, 1740.0f, t) * scale_x, 520.0f * scale_y, 240.0f};
            graze.color = scaled_color({1.0f, 0.95f, 0.86f, 1.0f}, 2.8f);
            graze.radius = 520.0f * scale_x;
            graze.falloff = PointLightFalloff::AeInverseSquareClamped;
            lights.push_back(graze);
            break;
        }
        case BustLightingPreset::SilhouetteRimHalo: {
            const f32 angle = t * kTwoPi;
            PointLightSource rim{};
            rim.kind = PointLightKind::Point;
            rim.position = {
                cx + std::cos(angle) * 620.0f * scale_x,
                cy + std::sin(angle) * 420.0f * scale_y,
                180.0f};
            rim.color = scaled_color({0.55f, 0.82f, 1.0f, 1.0f}, 2.9f);
            rim.radius = 680.0f * scale_x;
            lights.push_back(rim);
            break;
        }
        case BustLightingPreset::CloseProximityFalloff: {
            PointLightSource close{};
            close.kind = PointLightKind::Point;
            close.position = {std::lerp(660.0f, 1260.0f, t) * scale_x, 470.0f * scale_y, 190.0f};
            close.color = scaled_color({1.0f, 0.88f, 0.74f, 1.0f}, 3.0f);
            close.radius = 340.0f * scale_x;
            close.falloff = PointLightFalloff::AeInverseSquareClamped;
            lights.push_back(close);
            break;
        }
        case BustLightingPreset::DiagonalCrossSweep: {
            PointLightSource diag{};
            diag.kind = PointLightKind::Point;
            diag.position = {
                std::lerp(240.0f, 1680.0f, t) * scale_x,
                std::lerp(180.0f, 900.0f, t) * scale_y,
                380.0f};
            diag.color = scaled_color({1.0f, 0.93f, 0.82f, 1.0f}, 2.6f);
            diag.radius = 640.0f * scale_x;
            lights.push_back(diag);
            break;
        }
        case BustLightingPreset::LowAngleUpwardTorch: {
            PointLightSource torch{};
            torch.kind = PointLightKind::Point;
            torch.position = {std::lerp(560.0f, 1360.0f, t) * scale_x, 960.0f * scale_y, 310.0f};
            torch.color = scaled_color({1.0f, 0.56f, 0.22f, 1.0f}, 3.1f);
            torch.radius = 620.0f * scale_x;
            lights.push_back(torch);
            break;
        }
        case BustLightingPreset::MultiColorCrossSweep: {
            PointLightSource magenta{};
            magenta.kind = PointLightKind::Point;
            magenta.position = {std::lerp(240.0f, 1680.0f, t) * scale_x, 320.0f * scale_y, 360.0f};
            magenta.color = scaled_color({0.95f, 0.32f, 0.72f, 1.0f}, 2.2f);
            magenta.radius = 600.0f * scale_x;

            PointLightSource cyan{};
            cyan.kind = PointLightKind::Point;
            cyan.position = {std::lerp(1680.0f, 240.0f, t) * scale_x, 760.0f * scale_y, 360.0f};
            cyan.color = scaled_color({0.24f, 0.85f, 1.0f, 1.0f}, 2.2f);
            cyan.radius = 600.0f * scale_x;
            lights.push_back(magenta);
            lights.push_back(cyan);
            break;
        }
    }
    return lights;
}

[[nodiscard]] inline LightContext build_bust_light_context(
    BustLightingPreset preset,
    f32 t_norm,
    Vec2 canvas_size = {1920.0f, 1080.0f}) {
    LightContext ctx{};
    make_bust_lighting_rig(preset).apply_to(ctx);
    ctx.point_lights = sample_bust_sweep_lights(preset, t_norm, canvas_size);
    return ctx;
}

} // namespace chronon3d::rendering::ae_style
