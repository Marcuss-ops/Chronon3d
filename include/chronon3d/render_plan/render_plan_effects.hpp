#pragma once

#include <chronon3d/render_plan/render_plan_types.hpp>

namespace chronon3d::render_plan {
enum class EffectKindPlan : std::uint8_t { Lut3D, Echo, Keying, Threshold, Mosaic, GaussianBlur, DropShadow, Bloom, Glow, Noise, FractalNoise, CornerPin, Twirl, Bulge, MeshWarp, Sharpen, RoughenEdges, TurbulentDisplace, WaveWarp, Ripple, DisplacementMap, LightRays, ChromaticAberration, Halftone, DirectionalBlur, RadialBlur, Vignette, Catalog, LensDistortion, LuminanceResponse, FoldWarp, CreaseShading, Scanlines, RetroTv, Halation, AnamorphicStreak };
enum class KeyingModePlan : std::uint8_t { Luma, Chroma };
struct EffectPlan {
    EffectKindPlan kind{EffectKindPlan::Lut3D};
    std::string canonical_effect_id;
    struct ParamValue { std::string param; float value{0.0f}; };
    std::vector<ParamValue> canonical_params;
    std::string asset;
    float amount{1.0f};
    float roughen_scale{8.0f};
    std::uint32_t roughen_complexity{1};
    std::uint32_t roughen_seed{0};
    bool roughen_is_animated{false};
    std::uint32_t echoes{3};
    float decay{0.5f};
    std::uint32_t time_offset_frames{1};
    KeyingModePlan keying_mode{KeyingModePlan::Chroma};
    std::array<float, 3> key_color{0.0f, 1.0f, 0.0f};
    float threshold{0.20f};
    float softness{0.08f};
    float spill{0.75f};
    // Screen-matte refinement + garbage matte (see KeyingParams).
    float clip_black{0.0f};
    float clip_white{1.0f};
    float choke{0.0f};
    std::array<float, 4> garbage_rect{0.0f, 0.0f, 0.0f, 0.0f};
    float garbage_feather{0.0f};
    float threshold_softness{0.0f};
    std::uint32_t block_size{8};
    float chromatic_red_offset{2.0f};
    float chromatic_blue_offset{-2.0f};
    std::uint8_t chromatic_offset_mode{0}; // 0 = Constant, 1 = Radial, 2 = Velocity
    std::array<float, 2> chromatic_velocity{0.0f, 0.0f};
    // translation x/y, scale x/y, rotation degrees, opacity
    std::array<float, 6> chromatic_red_transform{0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    std::array<float, 6> chromatic_green_transform{0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    std::array<float, 6> chromatic_blue_transform{0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    std::uint32_t halftone_size{8};
    float halftone_angle{0.0f};
    float halftone_amount{1.0f};
    float directional_angle{0.0f};
    float directional_length{0.0f};
    int directional_samples{0};
    std::array<float, 2> radial_center{0.5f, 0.5f};
    float radial_amount{0.0f};
    int radial_render_samples{8};
    int radial_preview_samples{4};
    float radius{0.0f};
    std::array<float, 2> offset{0.0f, 8.0f};
    std::array<float, 4> effect_color{0.0f, 0.0f, 0.0f, 0.35f};
    float bloom_threshold{0.80f};
    float bloom_intensity{0.60f};
    float glow_intensity{0.25f};
    std::array<float, 2> light_rays_origin{0.5f, 0.5f};
    float light_rays_length{320.0f};
    float light_rays_density{0.75f};
    std::array<float, 4> light_rays_color{1.0f, 0.86f, 0.55f, 1.0f};
    float light_rays_decay{0.92f};
    std::uint32_t seed{0};
    bool animated{false};
    bool rgb_noise{false};
    float noise_size{1.0f};
    float grain_shadow_response{1.0f};
    float grain_midtone_response{1.0f};
    float grain_highlight_response{1.0f};
    std::uint32_t octaves{4};
    std::optional<Field2DPlan> noise_field;
    float frequency{0.015f};
    float amplitude{1.0f};
    float evolution{0.0f};
    float contrast{1.0f};
    float brightness{0.0f};
    float warp_size{0.1f}, warp_evolution{0.0f}, warp_speed{0.0f}, warp_wavelength{0.2f}, warp_phase{0.0f}, warp_width{0.1f}, warp_height{0.05f}, warp_damping{0.5f};
    std::uint32_t warp_complexity{2};
    std::array<float, 2> warp_direction{1.0f, 0.0f};
    std::uint8_t wave_type{0};
    std::optional<float> posterize_fps;
    std::optional<AnimationTrackPlan> time_curve;
    std::string map_layer;
    std::optional<Field2DPlan> map_field;
    float map_intensity_x{0.05f}, map_intensity_y{0.05f};
    std::uint8_t map_channel{0};
    // Warp family — quad corners (TL, TR, BR, BL as x,y pairs), radial
    // controls and the mesh-warp control grid.
    std::array<float, 8> quad{0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    std::array<float, 2> warp_center{0.5f, 0.5f};
    float warp_radius{0.5f};
    float warp_falloff{1.0f};
    float warp_angle{90.0f};
    float warp_amount{0.5f};
    std::uint32_t mesh_cols{2};
    std::uint32_t mesh_rows{2};
    float mesh_smoothness{0.0f};
    std::vector<float> mesh_offsets{}; // cols*rows x,y pairs

    // Lens distortion
    float lens_curvature{0.15f};
    std::array<float, 2> lens_center{0.5f, 0.5f};
    float lens_scale{1.0f};

    // Fold warp & Crease shading
    std::uint8_t fold_axis{0};
    float fold_position{0.5f};
    float fold_amount{0.25f};
    float fold_radius{0.20f};
    float fold_falloff{1.0f};
    float fold_perspective{0.05f};

    std::uint8_t crease_axis{0};
    float crease_position{0.5f};
    float crease_width{24.0f};
    float crease_dark_side{0.35f};
    float crease_light_side{0.20f};
    float crease_softness{0.50f};

    // Luminance response
    float response_threshold{0.70f};
    float response_softness{0.20f};
    float response_radius{18.0f};
    std::array<float, 4> response_tint{1.0f, 0.35f, 0.15f, 1.0f};
    float response_intensity{1.0f};
    std::uint8_t response_blend{0}; // 0 = Screen, 1 = Add, 2 = Over

    // Scanlines
    std::uint8_t scanlines_axis{0};
    float scanlines_spacing{2.0f};
    float scanlines_amount{0.08f};
    float scanlines_phase{0.0f};

    // Retro TV
    float retro_jitter_px{3.0f};
    float retro_jitter_speed{20.0f};
    float retro_smear_px{8.0f};
    float retro_bleed{0.35f};
    float retro_noise{0.06f};
    std::uint32_t retro_seed{0};

    // Film halation
    float halation_threshold{0.85f};
    float halation_softness{0.15f};
    float halation_radius{12.0f};
    float halation_intensity{0.50f};
    std::array<float, 4> halation_tint{1.0f, 0.25f, 0.05f, 1.0f};

    // Anamorphic streak
    float streak_threshold{0.85f};
    float streak_softness{0.10f};
    float streak_length{128.0f};
    float streak_intensity{0.35f};
    std::array<float, 4> streak_tint{0.55f, 0.75f, 1.0f, 1.0f};
};
struct EffectParamTrackPlan {
    std::string effect_id;
    std::string param;
    AnimationTrackPlan track;
};

}  // namespace chronon3d::render_plan
