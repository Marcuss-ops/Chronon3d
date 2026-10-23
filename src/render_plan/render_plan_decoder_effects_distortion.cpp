#include "render_plan_decoder_detail.hpp"
#include "render_plan_decoder_effects_functions.hpp"
#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/effects/effect_param_animation.hpp>
#include <chronon3d/effects/effect_ids.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d::render_plan::detail {
void decode_sharpen_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::Sharpen;
                effect.amount = raw_effect.value("amount", 0.0f);
                effect.radius = raw_effect.value("radius", 1.0f);
                effect.threshold = raw_effect.value("threshold", 0.0f);
                if (!std::isfinite(effect.amount) || effect.amount < -4.0f || effect.amount > 4.0f ||
                    !std::isfinite(effect.radius) || effect.radius <= 0.0f || effect.radius > 64.0f ||
                    !std::isfinite(effect.threshold) || effect.threshold < 0.0f || effect.threshold > 1.0f)
                    throw std::runtime_error("Invalid sharpen parameters");
}

void decode_roughen_edges_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::RoughenEdges;
                effect.amount = raw_effect.value("amount", 0.0f);
                effect.roughen_scale = raw_effect.value("scale", 8.0f);
                effect.roughen_complexity = raw_effect.value("complexity", 1U);
                effect.warp_evolution = raw_effect.value("evolution", 0.0f);
                effect.warp_speed = raw_effect.value("evolution_speed", 0.0f);
                effect.roughen_seed = raw_effect.value("seed", 0U);
                effect.roughen_is_animated = effect.warp_speed != 0.0f;
                if (!std::isfinite(effect.amount) || effect.amount < 0.0f || effect.amount > 64.0f ||
                    !std::isfinite(effect.roughen_scale) || effect.roughen_scale <= 0.0f || effect.roughen_scale > 1024.0f ||
                    effect.roughen_complexity < 1 || effect.roughen_complexity > 4 ||
                    !std::isfinite(effect.warp_evolution) || std::abs(effect.warp_evolution) > 100000.0f ||
                    !std::isfinite(effect.warp_speed) || std::abs(effect.warp_speed) > 1000.0f)
                    throw std::runtime_error("Roughen Edges parameters are outside supported finite ranges");
}

void decode_turbulent_displace_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::TurbulentDisplace;
                effect.amount=raw_effect.value("amount",0.1f); effect.warp_size=raw_effect.value("size",0.1f);
                effect.warp_evolution=raw_effect.value("evolution",0.0f); effect.warp_speed=raw_effect.value("evolution_speed",0.0f);
                effect.warp_complexity=raw_effect.value("complexity",2U);
                if(!std::isfinite(effect.amount)||effect.amount<0||effect.amount>1||!std::isfinite(effect.warp_size)||effect.warp_size<=0||
                   !std::isfinite(effect.warp_evolution)||!std::isfinite(effect.warp_speed)||effect.warp_complexity<1||effect.warp_complexity>16)
                    throw std::runtime_error("Invalid turbulent displace parameters");
}

void decode_wave_warp_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind=LayerPlan::EffectKindPlan::WaveWarp; effect.amount=raw_effect.value("amplitude",0.05f);
                effect.warp_wavelength=raw_effect.value("wavelength",0.2f); effect.warp_speed=raw_effect.value("speed",1.0f); effect.warp_phase=raw_effect.value("phase",0.0f);
                if(raw_effect.contains("direction")) { const auto& v=raw_effect.at("direction"); if(!v.is_array()||v.size()!=2) throw std::runtime_error("wave direction requires two values"); effect.warp_direction={v[0].get<float>(),v[1].get<float>()}; }
                const auto type=raw_effect.value("wave_type",std::string{"sine"});
                if(type=="triangle") effect.wave_type=1; else if(type=="square") effect.wave_type=2; else if(type=="saw") effect.wave_type=3; else if(type!="sine") throw std::runtime_error("Invalid wave type");
                if(!std::isfinite(effect.amount)||!std::isfinite(effect.warp_wavelength)||effect.warp_wavelength<=0||!std::isfinite(effect.warp_speed)||!std::isfinite(effect.warp_phase)||!std::isfinite(effect.warp_direction[0])||!std::isfinite(effect.warp_direction[1])) throw std::runtime_error("Invalid wave warp parameters");
}

void decode_ripple_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind=LayerPlan::EffectKindPlan::Ripple; effect.warp_radius=raw_effect.value("radius",0.25f);
                effect.warp_speed=raw_effect.value("wave_speed",1.5f); effect.warp_width=raw_effect.value("width",0.1f);
                effect.warp_height=raw_effect.value("wave_height",0.05f); effect.warp_damping=raw_effect.value("damping",0.5f);
                if(raw_effect.contains("center")) { const auto& v=raw_effect.at("center"); if(!v.is_array()||v.size()!=2) throw std::runtime_error("ripple center requires two values"); effect.warp_center={v[0].get<float>(),v[1].get<float>()}; }
                if(!std::isfinite(effect.warp_radius)||effect.warp_radius<=0||!std::isfinite(effect.warp_speed)||!std::isfinite(effect.warp_width)||effect.warp_width<=0||!std::isfinite(effect.warp_height)||!std::isfinite(effect.warp_damping)||effect.warp_damping<0||!std::isfinite(effect.warp_center[0])||!std::isfinite(effect.warp_center[1])) throw std::runtime_error("Invalid ripple parameters");
}

void decode_displacement_map_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind=LayerPlan::EffectKindPlan::DisplacementMap;
                if (raw_effect.contains("map_layer"))
                    effect.map_layer=raw_effect.at("map_layer").get<std::string>();
                if (raw_effect.contains("field"))
                    effect.map_field = decode_field2d_plan(raw_effect.at("field"));
                effect.map_intensity_x=raw_effect.value("intensity_x",0.05f);
                effect.map_intensity_y=raw_effect.value("intensity_y",0.05f);
                const auto channel=raw_effect.value("map_channel",std::string{"luma"});
                if(channel=="red") effect.map_channel=1; else if(channel=="green") effect.map_channel=2;
                else if(channel=="blue") effect.map_channel=3; else if(channel=="alpha") effect.map_channel=4;
                else if(channel!="luma") throw std::runtime_error("Unsupported displacement map channel");
                if((effect.map_layer.empty() == !effect.map_field.has_value())||
                   !std::isfinite(effect.map_intensity_x)||!std::isfinite(effect.map_intensity_y)||
                   std::abs(effect.map_intensity_x)>4.0f||std::abs(effect.map_intensity_y)>4.0f)
                    throw std::runtime_error("Invalid displacement map parameters");
}

void decode_corner_pin_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::CornerPin;
                const auto read_quad = [&](const char* key, std::size_t index) {
                    if (!raw_effect.contains(key)) return;
                    const auto& point = raw_effect.at(key);
                    if (!point.is_array() || point.size() != 2)
                        throw std::runtime_error(std::string("Corner pin ") + key + " requires two values");
                    effect.quad[index] = point[0].get<float>();
                    effect.quad[index + 1] = point[1].get<float>();
                };
                read_quad("top_left", 0);
                read_quad("top_right", 2);
                read_quad("bottom_right", 4);
                read_quad("bottom_left", 6);
                for (const float value : effect.quad)
                    if (!std::isfinite(value) || std::abs(value) > 4.0f)
                        throw std::runtime_error("Corner pin coordinates must be finite and within [-4, 4]");
}

void decode_twirl_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::Twirl;
                if (raw_effect.contains("center")) {
                    const auto& center = raw_effect.at("center");
                    if (!center.is_array() || center.size() != 2)
                        throw std::runtime_error("Twirl center requires two values");
                    effect.warp_center = {center[0].get<float>(), center[1].get<float>()};
                }
                effect.warp_angle = raw_effect.value("angle_degrees", 90.0f);
                effect.warp_radius = raw_effect.value("radius", 0.5f);
                effect.warp_falloff = raw_effect.value("falloff", 1.0f);
                if (!std::isfinite(effect.warp_center[0]) || !std::isfinite(effect.warp_center[1]) ||
                    !std::isfinite(effect.warp_angle) || std::abs(effect.warp_angle) > 3600.0f ||
                    !std::isfinite(effect.warp_radius) || effect.warp_radius <= 0.0f ||
                    effect.warp_radius > 2.0f ||
                    !std::isfinite(effect.warp_falloff) || effect.warp_falloff <= 0.0f ||
                    effect.warp_falloff > 4.0f)
                    throw std::runtime_error("Invalid twirl parameters");
}

void decode_bulge_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::Bulge;
                if (raw_effect.contains("center")) {
                    const auto& center = raw_effect.at("center");
                    if (!center.is_array() || center.size() != 2)
                        throw std::runtime_error("Bulge center requires two values");
                    effect.warp_center = {center[0].get<float>(), center[1].get<float>()};
                }
                effect.warp_amount = raw_effect.value("amount", 0.5f);
                effect.warp_radius = raw_effect.value("radius", 0.5f);
                if (!std::isfinite(effect.warp_center[0]) || !std::isfinite(effect.warp_center[1]) ||
                    !std::isfinite(effect.warp_amount) || effect.warp_amount < -1.0f ||
                    effect.warp_amount > 1.0f ||
                    !std::isfinite(effect.warp_radius) || effect.warp_radius <= 0.0f ||
                    effect.warp_radius > 2.0f)
                    throw std::runtime_error("Invalid bulge parameters");
}

void decode_mesh_warp_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::MeshWarp;
                effect.mesh_cols = raw_effect.value("cols", 2u);
                effect.mesh_rows = raw_effect.value("rows", 2u);
                effect.mesh_smoothness = raw_effect.value("smoothness", 0.0f);
                if (effect.mesh_cols < 2 || effect.mesh_cols > 32 ||
                    effect.mesh_rows < 2 || effect.mesh_rows > 32 ||
                    !std::isfinite(effect.mesh_smoothness) || effect.mesh_smoothness < 0.0f ||
                    effect.mesh_smoothness > 1.0f)
                    throw std::runtime_error("Invalid mesh-warp grid dimensions");
                const auto& offsets = raw_effect.at("offsets");
                const std::size_t expected =
                    static_cast<std::size_t>(effect.mesh_cols) * effect.mesh_rows * 2;
                if (!offsets.is_array() || offsets.size() != expected || expected > 2048)
                    throw std::runtime_error("Mesh warp offsets must contain cols*rows x,y pairs");
                effect.mesh_offsets.reserve(expected);
                for (const auto& value : offsets) {
                    const float component = value.get<float>();
                    if (!std::isfinite(component) || std::abs(component) > 4.0f)
                        throw std::runtime_error("Mesh warp offsets must be finite and within [-4, 4]");
                    effect.mesh_offsets.push_back(component);
                }

}

void decode_lens_distortion_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::LensDistortion;
                effect.lens_curvature = raw_effect.value("curvature", 0.15f);
                effect.lens_scale = raw_effect.value("scale", 1.0f);
                if (raw_effect.contains("center")) {
                    const auto& center = raw_effect.at("center");
                    if (center.is_array() && center.size() == 2) {
                        effect.lens_center[0] = center[0].get<float>();
                        effect.lens_center[1] = center[1].get<float>();
                    }
                }
                if (!std::isfinite(effect.lens_curvature) || effect.lens_curvature < -1.0f || effect.lens_curvature > 1.0f ||
                    !std::isfinite(effect.lens_scale) || effect.lens_scale < 0.1f || effect.lens_scale > 4.0f ||
                    !std::isfinite(effect.lens_center[0]) || !std::isfinite(effect.lens_center[1]))
                    throw std::runtime_error("Lens distortion parameters are non-finite or outside supported range");
}

void decode_luminance_response_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::LuminanceResponse;
                effect.response_threshold = raw_effect.value("threshold", 0.70f);
                effect.response_softness = raw_effect.value("softness", 0.20f);
                effect.response_radius = raw_effect.value("radius", 18.0f);
                effect.response_intensity = raw_effect.value("intensity", 1.0f);
                if (raw_effect.contains("tint")) {
                    const auto& tint = raw_effect.at("tint");
                    if (tint.is_array() && tint.size() == 4) {
                        for (std::size_t c = 0; c < 4; ++c) effect.response_tint[c] = tint[c].get<float>();
                    }
                }
                const auto blend_str = raw_effect.value("blend", "screen");
                effect.response_blend = (blend_str == "add") ? 1 : ((blend_str == "over") ? 2 : 0);
                if (!std::isfinite(effect.response_threshold) || effect.response_threshold < 0.0f || effect.response_threshold > 1.0f ||
                    !std::isfinite(effect.response_softness) || effect.response_softness < 0.0f || effect.response_softness > 1.0f ||
                    !std::isfinite(effect.response_radius) || effect.response_radius < 0.0f || effect.response_radius > 256.0f ||
                    !std::isfinite(effect.response_intensity) || effect.response_intensity < 0.0f || effect.response_intensity > 8.0f)
                    throw std::runtime_error("Luminance response parameters are non-finite or outside supported range");
}

void decode_fold_warp_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::FoldWarp;
                const auto axis_str = raw_effect.value("axis", "vertical");
                effect.fold_axis = (axis_str == "horizontal") ? 1 : 0;
                effect.fold_position = raw_effect.value("position", 0.5f);
                effect.fold_amount = raw_effect.value("amount", 0.25f);
                effect.fold_radius = raw_effect.value("radius", 0.20f);
                effect.fold_falloff = raw_effect.value("falloff", 1.0f);
                effect.fold_perspective = raw_effect.value("perspective", 0.05f);
                if (!std::isfinite(effect.fold_position) || effect.fold_position < 0.0f || effect.fold_position > 1.0f ||
                    !std::isfinite(effect.fold_amount) || effect.fold_amount < -1.0f || effect.fold_amount > 1.0f ||
                    !std::isfinite(effect.fold_radius) || effect.fold_radius < 0.01f || effect.fold_radius > 1.0f ||
                    !std::isfinite(effect.fold_falloff) || effect.fold_falloff < 0.1f || effect.fold_falloff > 4.0f ||
                    !std::isfinite(effect.fold_perspective) || effect.fold_perspective < 0.0f || effect.fold_perspective > 0.5f)
                    throw std::runtime_error("Fold warp parameters are non-finite or outside supported range");
}

void decode_crease_shading_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::CreaseShading;
                const auto axis_str = raw_effect.value("axis", "vertical");
                effect.crease_axis = (axis_str == "horizontal") ? 1 : 0;
                effect.crease_position = raw_effect.value("position", 0.5f);
                effect.crease_width = raw_effect.value("width", 24.0f);
                effect.crease_dark_side = raw_effect.value("dark_side", 0.35f);
                effect.crease_light_side = raw_effect.value("light_side", 0.20f);
                effect.crease_softness = raw_effect.value("softness", 0.50f);
                if (!std::isfinite(effect.crease_position) || effect.crease_position < 0.0f || effect.crease_position > 1.0f ||
                    !std::isfinite(effect.crease_width) || effect.crease_width < 1.0f || effect.crease_width > 256.0f ||
                    !std::isfinite(effect.crease_dark_side) || effect.crease_dark_side < 0.0f || effect.crease_dark_side > 1.0f ||
                    !std::isfinite(effect.crease_light_side) || effect.crease_light_side < 0.0f || effect.crease_light_side > 1.0f ||
                    !std::isfinite(effect.crease_softness) || effect.crease_softness < 0.0f || effect.crease_softness > 1.0f)
                    throw std::runtime_error("Crease shading parameters are non-finite or outside supported range");
}

void decode_scanlines_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::Scanlines;
                const auto axis_str = raw_effect.value("axis", "horizontal");
                effect.scanlines_axis = (axis_str == "vertical") ? 1 : 0;
                effect.scanlines_spacing = raw_effect.value("spacing_px", 2.0f);
                effect.scanlines_amount = raw_effect.value("amount", 0.08f);
                effect.scanlines_phase = raw_effect.value("phase", 0.0f);
                if (!std::isfinite(effect.scanlines_spacing) || effect.scanlines_spacing < 1.0f || effect.scanlines_spacing > 64.0f ||
                    !std::isfinite(effect.scanlines_amount) || effect.scanlines_amount < 0.0f || effect.scanlines_amount > 1.0f ||
                    !std::isfinite(effect.scanlines_phase) || effect.scanlines_phase < 0.0f || effect.scanlines_phase >= 1.0f)
                    throw std::runtime_error("Scanline parameters are non-finite or outside supported range");
}

void decode_retro_tv_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::RetroTv;
                effect.retro_jitter_px = raw_effect.value("jitter_px", 3.0f);
                effect.retro_jitter_speed = raw_effect.value("jitter_speed", 20.0f);
                effect.retro_smear_px = raw_effect.value("smear_px", 8.0f);
                effect.retro_bleed = raw_effect.value("bleed_strength", 0.35f);
                effect.retro_noise = raw_effect.value("luma_noise", 0.06f);
                effect.retro_seed = raw_effect.value("seed", 0u);
                if (!std::isfinite(effect.retro_jitter_px) || effect.retro_jitter_px < 0.0f || effect.retro_jitter_px > 32.0f ||
                    !std::isfinite(effect.retro_jitter_speed) || effect.retro_jitter_speed < 0.0f || effect.retro_jitter_speed > 120.0f ||
                    !std::isfinite(effect.retro_smear_px) || effect.retro_smear_px < 0.0f || effect.retro_smear_px > 32.0f ||
                    !std::isfinite(effect.retro_bleed) || effect.retro_bleed < 0.0f || effect.retro_bleed > 1.0f ||
                    !std::isfinite(effect.retro_noise) || effect.retro_noise < 0.0f || effect.retro_noise > 1.0f)
                    throw std::runtime_error("Retro TV parameters are non-finite or outside supported range");
}

void decode_halation_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::Halation;
                effect.halation_threshold = raw_effect.value("threshold", 0.85f);
                effect.halation_softness = raw_effect.value("softness", 0.15f);
                effect.halation_radius = raw_effect.value("radius_px", 12.0f);
                effect.halation_intensity = raw_effect.value("intensity", 0.50f);
                if (raw_effect.contains("tint")) {
                    const auto& tint = raw_effect.at("tint");
                    if (tint.is_array() && tint.size() == 4) {
                        for (std::size_t c = 0; c < 4; ++c) effect.halation_tint[c] = tint[c].get<float>();
                    }
                }
                if (!std::isfinite(effect.halation_threshold) || effect.halation_threshold < 0.0f || effect.halation_threshold > 1.0f ||
                    !std::isfinite(effect.halation_softness) || effect.halation_softness < 0.0f || effect.halation_softness > 1.0f ||
                    !std::isfinite(effect.halation_radius) || effect.halation_radius < 0.0f || effect.halation_radius > 64.0f ||
                    !std::isfinite(effect.halation_intensity) || effect.halation_intensity < 0.0f || effect.halation_intensity > 8.0f ||
                    !std::isfinite(effect.halation_tint[0]) || !std::isfinite(effect.halation_tint[1]) ||
                    !std::isfinite(effect.halation_tint[2]) || !std::isfinite(effect.halation_tint[3]))
                    throw std::runtime_error("Film halation parameters are non-finite or outside supported range");
}

void decode_anamorphic_streak_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::AnamorphicStreak;
                effect.streak_threshold = raw_effect.value("threshold", 0.85f);
                effect.streak_softness = raw_effect.value("softness", 0.10f);
                effect.streak_length = raw_effect.value("streak_length_px", 128.0f);
                effect.streak_intensity = raw_effect.value("intensity", 0.35f);
                if (raw_effect.contains("tint")) {
                    const auto& tint = raw_effect.at("tint");
                    if (tint.is_array() && tint.size() == 4) {
                        for (std::size_t c = 0; c < 4; ++c) effect.streak_tint[c] = tint[c].get<float>();
                    }
                }
                if (!std::isfinite(effect.streak_threshold) || effect.streak_threshold < 0.0f || effect.streak_threshold > 1.0f ||
                    !std::isfinite(effect.streak_softness) || effect.streak_softness < 0.0f || effect.streak_softness > 1.0f ||
                    !std::isfinite(effect.streak_length) || effect.streak_length < 0.0f || effect.streak_length > 512.0f ||
                    !std::isfinite(effect.streak_intensity) || effect.streak_intensity < 0.0f || effect.streak_intensity > 8.0f ||
                    !std::isfinite(effect.streak_tint[0]) || !std::isfinite(effect.streak_tint[1]) ||
                    !std::isfinite(effect.streak_tint[2]) || !std::isfinite(effect.streak_tint[3]))
                    throw std::runtime_error("Anamorphic streak parameters are non-finite or outside supported range");
}

// One row per EffectKindPlan.  Adding an effect is a decoder function plus
// one row here; the fingerprint/preparation switches still enumerate kinds
// independently and remain the compile-time coverage gate.

} // namespace chronon3d::render_plan::detail
