#pragma once
#include "render_plan_decoder_detail.hpp"
namespace chronon3d::render_plan::detail {
void decode_catalog_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_lut_3d_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_echo_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_keying_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_threshold_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_mosaic_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_chromatic_aberration_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_halftone_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_directional_blur_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_radial_blur_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_gaussian_blur_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_drop_shadow_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_bloom_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_glow_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_vignette_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_light_rays_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_noise_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_fractal_noise_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_sharpen_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_roughen_edges_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_turbulent_displace_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_wave_warp_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_ripple_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_displacement_map_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_corner_pin_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_twirl_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_bulge_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_mesh_warp_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_lens_distortion_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_luminance_response_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_fold_warp_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_crease_shading_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_scanlines_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_retro_tv_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_halation_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
void decode_anamorphic_streak_effect(LayerPlan::EffectPlan&, const nlohmann::json&);
}
