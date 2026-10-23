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
void decode_catalog_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::Catalog;
                effect.canonical_effect_id = raw_effect.at("effect_id").get<std::string>();
                if (!chronon3d::effects::is_canonical_effect_id(effect.canonical_effect_id))
                    throw std::runtime_error("catalog effect uses an unknown canonical effect_id");
                if (effect.canonical_effect_id == chronon3d::effects::ids::ColorLut3D ||
                    effect.canonical_effect_id == chronon3d::effects::ids::DistortDisplacementMap)
                    throw std::runtime_error("catalog effect requires external asset/layer inputs; use its dedicated effect type");
                if (!chronon3d::effects::make_default_animatable_effect_params(effect.canonical_effect_id))
                    throw std::runtime_error("catalog effect has no materializable parameter type");
                if (raw_effect.contains("params")) {
                    for (const auto& raw_param : raw_effect.at("params")) {
                        const auto param = raw_param.at("param").get<std::string>();
                        const float sample = raw_param.at("value").get<float>();
                        const auto* row = chronon3d::effects::find_animatable_effect_param(
                            effect.canonical_effect_id, param);
                        if (!row || !std::isfinite(sample) || sample < row->minimum || sample > row->maximum)
                            throw std::runtime_error("catalog effect parameter is unknown or outside its declared range: " + param);
                        if (std::any_of(effect.canonical_params.begin(), effect.canonical_params.end(),
                                [&param](const auto& existing) { return existing.param == param; }))
                            throw std::runtime_error("duplicate catalog effect parameter: " + param);
                        effect.canonical_params.push_back({param, sample});
                    }
                }
}

void decode_lut_3d_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::Lut3D;
                effect.asset = raw_effect.at("asset").get<std::string>();
                effect.amount = raw_effect.value("amount", 1.0f);
                if (effect.asset.empty() || effect.asset.size() > 4096 ||
                    !std::isfinite(effect.amount) || effect.amount < 0.0f || effect.amount > 1.0f)
                    throw std::runtime_error("3D LUT effect requires an asset and amount in [0, 1]");
}

void decode_echo_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::Echo;
                effect.echoes = raw_effect.value("echoes", 3u);
                effect.decay = raw_effect.value("decay", 0.5f);
                effect.time_offset_frames = raw_effect.value("time_offset_frames", 1u);
                if (effect.echoes == 0 || effect.echoes > 8 || !std::isfinite(effect.decay) ||
                    effect.decay < 0.0f || effect.decay > 1.0f ||
                    effect.time_offset_frames == 0 || effect.time_offset_frames > 120)
                    throw std::runtime_error("Echo requires echoes in [1, 8], decay in [0, 1], and time_offset_frames in [1, 120]");
}

void decode_keying_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::Keying;
                const auto mode = raw_effect.value("mode", std::string("chroma"));
                if (mode == "luma") effect.keying_mode = LayerPlan::KeyingModePlan::Luma;
                else if (mode == "chroma") effect.keying_mode = LayerPlan::KeyingModePlan::Chroma;
                else throw std::runtime_error("Keying mode must be 'luma' or 'chroma'");
                if (raw_effect.contains("key_color")) {
                    const auto& color = raw_effect.at("key_color");
                    if (!color.is_array() || (color.size() != 3 && color.size() != 4))
                        throw std::runtime_error("Keying key_color must contain RGB or RGBA");
                    for (std::size_t i = 0; i < 3; ++i) {
                        effect.key_color[i] = color.at(i).get<float>();
                        if (!std::isfinite(effect.key_color[i]) || effect.key_color[i] < 0.0f || effect.key_color[i] > 1.0f)
                            throw std::runtime_error("Keying key_color components must be in [0, 1]");
                    }
                }
                effect.threshold = raw_effect.value("threshold", 0.20f);
                effect.softness = raw_effect.value("softness", 0.08f);
                effect.spill = raw_effect.value("spill", 0.75f);
                effect.clip_black = raw_effect.value("clip_black", 0.0f);
                effect.clip_white = raw_effect.value("clip_white", 1.0f);
                effect.choke = raw_effect.value("choke", 0.0f);
                if (raw_effect.contains("garbage_rect")) {
                    const auto& rect = raw_effect.at("garbage_rect");
                    if (!rect.is_array() || rect.size() != 4)
                        throw std::runtime_error("Keying garbage_rect must contain [x, y, w, h]");
                    for (std::size_t i = 0; i < 4; ++i) {
                        effect.garbage_rect[i] = rect.at(i).get<float>();
                        if (!std::isfinite(effect.garbage_rect[i]) ||
                            effect.garbage_rect[i] < 0.0f || effect.garbage_rect[i] > 1.0f)
                            throw std::runtime_error("Keying garbage_rect components must be in [0, 1]");
                    }
                }
                effect.garbage_feather = raw_effect.value("garbage_feather", 0.0f);
                if (!std::isfinite(effect.threshold) || effect.threshold < 0.0f || effect.threshold > 1.0f ||
                    !std::isfinite(effect.softness) || effect.softness < 0.0f || effect.softness > 1.0f ||
                    !std::isfinite(effect.spill) || effect.spill < 0.0f || effect.spill > 1.0f)
                    throw std::runtime_error("Keying threshold, softness, and spill must be in [0, 1]");
                if (!std::isfinite(effect.clip_black) || !std::isfinite(effect.clip_white) ||
                    effect.clip_black < 0.0f || effect.clip_white > 1.0f ||
                    effect.clip_white <= effect.clip_black)
                    throw std::runtime_error("Keying clip_black/clip_white must satisfy 0 <= black < white <= 1");
                if (!std::isfinite(effect.choke) || effect.choke < 0.0f || effect.choke >= 1.0f)
                    throw std::runtime_error("Keying choke must be in [0, 1)");
                if (!std::isfinite(effect.garbage_feather) || effect.garbage_feather < 0.0f ||
                    effect.garbage_feather > 0.5f)
                    throw std::runtime_error("Keying garbage_feather must be in [0, 0.5]");
}

void decode_threshold_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::Threshold;
                effect.threshold = raw_effect.value("threshold", 0.5f);
                effect.threshold_softness = raw_effect.value("softness", 0.0f);
                if (!std::isfinite(effect.threshold) || effect.threshold < 0.0f || effect.threshold > 1.0f ||
                    !std::isfinite(effect.threshold_softness) || effect.threshold_softness < 0.0f || effect.threshold_softness > 1.0f)
                    throw std::runtime_error("Threshold and softness must be in [0, 1]");
}

void decode_mosaic_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::Mosaic;
                effect.block_size = raw_effect.value("block_size", 8u);
                if (effect.block_size == 0 || effect.block_size > 512)
                    throw std::runtime_error("Mosaic block_size must be in [1, 512]");
}

void decode_chromatic_aberration_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::ChromaticAberration;
                effect.chromatic_red_offset = raw_effect.value("red_offset", 2.0f);
                effect.chromatic_blue_offset = raw_effect.value("blue_offset", -2.0f);
                if (!std::isfinite(effect.chromatic_red_offset) ||
                    !std::isfinite(effect.chromatic_blue_offset) ||
                    std::abs(effect.chromatic_red_offset) > 128.0f ||
                    std::abs(effect.chromatic_blue_offset) > 128.0f)
                    throw std::runtime_error("Chromatic Aberration offsets must be finite and within [-128, 128]");
                const auto decode_channel_transform = [&raw_effect](const char* key,
                        std::array<float, 6>& output) {
                    if (!raw_effect.contains(key)) return;
                    const auto& raw = raw_effect.at(key);
                    if (!raw.is_object())
                        throw std::runtime_error(std::string("Chromatic channel ") + key + " must be an object");
                    const auto read_pair = [&raw, &output, key](const char* field, std::size_t x, std::size_t y,
                                                       float minimum, float maximum) {
                        if (!raw.contains(field)) return;
                        const auto& value = raw.at(field);
                        if (!value.is_array() || value.size() != 2)
                            throw std::runtime_error(std::string("Chromatic channel ") + key + "." + field + " must be [x,y]");
                        output[x] = value[0].get<float>();
                        output[y] = value[1].get<float>();
                        if (!std::isfinite(output[x]) || !std::isfinite(output[y]) ||
                            output[x] < minimum || output[x] > maximum ||
                            output[y] < minimum || output[y] > maximum)
                            throw std::runtime_error(std::string("Chromatic channel ") + key + "." + field + " is out of bounds");
                    };
                    read_pair("translation", 0, 1, -2048.0f, 2048.0f);
                    read_pair("scale", 2, 3, 0.01f, 100.0f);
                    if (raw.contains("rotation")) {
                        output[4] = raw.at("rotation").get<float>();
                        if (!std::isfinite(output[4]) || std::abs(output[4]) > 3600.0f)
                            throw std::runtime_error(std::string("Chromatic channel ") + key + ".rotation is out of bounds");
                    }
                    if (raw.contains("opacity")) {
                        output[5] = raw.at("opacity").get<float>();
                        if (!std::isfinite(output[5]) || output[5] < 0.0f || output[5] > 1.0f)
                            throw std::runtime_error(std::string("Chromatic channel ") + key + ".opacity must be in [0,1]");
                    }
                    for (auto it = raw.begin(); it != raw.end(); ++it)
                        if (it.key() != "translation" && it.key() != "scale" &&
                            it.key() != "rotation" && it.key() != "opacity")
                            throw std::runtime_error(std::string("Unsupported Chromatic channel option: ") + it.key());
                };
                decode_channel_transform("red_transform", effect.chromatic_red_transform);
                decode_channel_transform("green_transform", effect.chromatic_green_transform);
                decode_channel_transform("blue_transform", effect.chromatic_blue_transform);
}

void decode_halftone_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::Halftone;
                effect.halftone_size = raw_effect.value("size", 8u);
                effect.halftone_angle = raw_effect.value("angle", 0.0f);
                effect.halftone_amount = raw_effect.value("amount", 1.0f);
                if (effect.halftone_size == 0 || effect.halftone_size > 128 ||
                    !std::isfinite(effect.halftone_angle) ||
                    !std::isfinite(effect.halftone_amount) ||
                    effect.halftone_amount < 0.0f || effect.halftone_amount > 1.0f)
                    throw std::runtime_error("Halftone requires size in [1, 128], finite angle, and amount in [0, 1]");
}

void decode_directional_blur_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::DirectionalBlur;
                effect.directional_angle = raw_effect.value("angle", 0.0f);
                effect.directional_length = raw_effect.value("length", 0.0f);
                effect.directional_samples = raw_effect.value("samples", 0);
                if (!std::isfinite(effect.directional_angle) ||
                    std::abs(effect.directional_angle) > 3600.0f ||
                    !std::isfinite(effect.directional_length) ||
                    effect.directional_length < 0.0f || effect.directional_length > 2048.0f ||
                    effect.directional_samples < 0 || effect.directional_samples > 256)
                    throw std::runtime_error("Directional Blur parameters are outside supported bounds");
}

void decode_radial_blur_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::RadialBlur;
                if (raw_effect.contains("center")) {
                    const auto& center = raw_effect.at("center");
                    if (!center.is_array() || center.size() != 2)
                        throw std::runtime_error("Radial Blur center requires [x, y]");
                    effect.radial_center = {center[0].get<float>(), center[1].get<float>()};
                }
                effect.radial_amount = raw_effect.value("amount", 0.0f);
                effect.radial_render_samples = raw_effect.value("render_samples", 8);
                effect.radial_preview_samples = raw_effect.value("preview_samples", 4);
                if (!std::isfinite(effect.radial_center[0]) || effect.radial_center[0] < 0.0f ||
                    effect.radial_center[0] > 1.0f || !std::isfinite(effect.radial_center[1]) ||
                    effect.radial_center[1] < 0.0f || effect.radial_center[1] > 1.0f ||
                    !std::isfinite(effect.radial_amount) || effect.radial_amount < 0.0f ||
                    effect.radial_amount > 10.0f || effect.radial_render_samples < 2 ||
                    effect.radial_render_samples > 256 || effect.radial_preview_samples < 2 ||
                    effect.radial_preview_samples > 128)
                    throw std::runtime_error("Radial Blur parameters are outside supported bounds");
}

void decode_gaussian_blur_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::GaussianBlur;
                effect.radius = raw_effect.value("radius", 8.0f);
                if (!std::isfinite(effect.radius) || effect.radius < 0.0f || effect.radius > 256.0f)
                    throw std::runtime_error("Gaussian blur radius must be in [0, 256]");
}

void decode_drop_shadow_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::DropShadow;
                effect.radius = raw_effect.value("radius", 12.0f);
                if (raw_effect.contains("offset")) {
                    const auto& offset = raw_effect.at("offset");
                    if (!offset.is_array() || offset.size() != 2)
                        throw std::runtime_error("Drop shadow offset must contain [x, y]");
                    for (std::size_t i = 0; i < 2; ++i) {
                        effect.offset[i] = offset.at(i).get<float>();
                        if (!std::isfinite(effect.offset[i]) || std::abs(effect.offset[i]) > 10000.0f)
                            throw std::runtime_error("Drop shadow offset values must be finite and within [-10000, 10000]");
                    }
                }
                if (raw_effect.contains("color")) {
                    const auto& color = raw_effect.at("color");
                    if (!color.is_array() || color.size() != 4)
                        throw std::runtime_error("Drop shadow color must contain [r, g, b, a]");
                    for (std::size_t i = 0; i < 4; ++i) {
                        effect.effect_color[i] = color.at(i).get<float>();
                        if (!std::isfinite(effect.effect_color[i]) || effect.effect_color[i] < 0.0f || effect.effect_color[i] > 1.0f)
                            throw std::runtime_error("Drop shadow color components must be in [0, 1]");
                    }
                }
                if (!std::isfinite(effect.radius) || effect.radius < 0.0f || effect.radius > 256.0f)
                    throw std::runtime_error("Drop shadow radius must be in [0, 256]");
}

void decode_bloom_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::Bloom;
                effect.bloom_threshold = raw_effect.value("threshold", 0.80f);
                effect.radius = raw_effect.value("radius", 24.0f);
                effect.bloom_intensity = raw_effect.value("intensity", 0.60f);
                if (!std::isfinite(effect.bloom_threshold) || effect.bloom_threshold < 0.0f || effect.bloom_threshold > 1.0f ||
                    !std::isfinite(effect.radius) || effect.radius < 0.0f || effect.radius > 256.0f ||
                    !std::isfinite(effect.bloom_intensity) || effect.bloom_intensity < 0.0f || effect.bloom_intensity > 4.0f)
                    throw std::runtime_error("Bloom requires threshold in [0, 1], radius in [0, 256], and intensity in [0, 4]");
}

void decode_glow_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::Glow;
                effect.radius = raw_effect.value("radius", 24.0f);
                effect.glow_intensity = raw_effect.value("intensity", 0.25f);
                if (raw_effect.contains("color")) {
                    const auto& color = raw_effect.at("color");
                    if (!color.is_array() || color.size() != 4)
                        throw std::runtime_error("Glow color must contain [r, g, b, a]");
                    for (std::size_t i = 0; i < 4; ++i) {
                        effect.effect_color[i] = color.at(i).get<float>();
                        if (!std::isfinite(effect.effect_color[i]) || effect.effect_color[i] < 0.0f || effect.effect_color[i] > 1.0f)
                            throw std::runtime_error("Glow color components must be in [0, 1]");
                    }
                }
                if (!std::isfinite(effect.radius) || effect.radius < 0.0f || effect.radius > 256.0f ||
                    !std::isfinite(effect.glow_intensity) || effect.glow_intensity < 0.0f || effect.glow_intensity > 4.0f)
                    throw std::runtime_error("Glow requires radius in [0, 256] and intensity in [0, 4]");
}

void decode_vignette_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::Vignette;
                effect.effect_color = {0.0f, 0.0f, 0.0f, 1.0f};
                effect.radius = raw_effect.value("radius", 0.5f);
                effect.softness = raw_effect.value("softness", 0.5f);
                effect.amount = raw_effect.value("amount", 1.0f);
                if (raw_effect.contains("color")) {
                    const auto& color = raw_effect.at("color");
                    if (!color.is_array() || color.size() != 4)
                        throw std::runtime_error("Vignette color must contain [r, g, b, a]");
                    for (std::size_t i = 0; i < 4; ++i) {
                        effect.effect_color[i] = color.at(i).get<float>();
                        if (!std::isfinite(effect.effect_color[i]) || effect.effect_color[i] < 0.0f || effect.effect_color[i] > 1.0f)
                            throw std::runtime_error("Vignette color components must be in [0, 1]");
                    }
                }
                if (!std::isfinite(effect.radius) || effect.radius < 0.0f || effect.radius > 1.0f ||
                    !std::isfinite(effect.softness) || effect.softness < 0.0f || effect.softness > 1.0f ||
                    !std::isfinite(effect.amount) || effect.amount < 0.0f || effect.amount > 1.0f)
                    throw std::runtime_error("Vignette radius, softness, and amount must be in [0, 1]");
}

void decode_light_rays_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::LightRays;
                if (raw_effect.contains("origin")) {
                    const auto& origin = raw_effect.at("origin");
                    if (!origin.is_array() || origin.size() != 2)
                        throw std::runtime_error("Light rays origin must contain [x, y]");
                    effect.light_rays_origin = {origin[0].get<float>(), origin[1].get<float>()};
                }
                effect.light_rays_length = raw_effect.value("length", 320.0f);
                effect.light_rays_density = raw_effect.value("density", 0.75f);
                effect.light_rays_decay = raw_effect.value("decay", 0.92f);
                if (raw_effect.contains("color")) {
                    const auto& color = raw_effect.at("color");
                    if (!color.is_array() || color.size() != 4)
                        throw std::runtime_error("Light rays color must contain RGBA");
                    for (std::size_t i = 0; i < 4; ++i)
                        effect.light_rays_color[i] = color[i].get<float>();
                }
                if (!std::isfinite(effect.light_rays_origin[0]) ||
                    !std::isfinite(effect.light_rays_origin[1]) ||
                    !std::isfinite(effect.light_rays_length) || effect.light_rays_length < 0.0f ||
                    effect.light_rays_length > 100000.0f ||
                    effect.light_rays_origin[0] < 0.0f || effect.light_rays_origin[0] > 1.0f ||
                    effect.light_rays_origin[1] < 0.0f || effect.light_rays_origin[1] > 1.0f ||
                    !std::isfinite(effect.light_rays_density) || effect.light_rays_density < 0.0f ||
                    effect.light_rays_density > 8.0f ||
                    !std::isfinite(effect.light_rays_decay) || effect.light_rays_decay < 0.0f ||
                    effect.light_rays_decay > 1.0f ||
                    std::any_of(effect.light_rays_color.begin(), effect.light_rays_color.end(),
                        [](float component) {
                            return !std::isfinite(component) || component < 0.0f || component > 1.0f;
                        }))
                    throw std::runtime_error("Light rays parameters are outside supported finite ranges");
}

void decode_noise_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::Noise;
                effect.amount = raw_effect.value("amount", 0.08f);
                effect.seed = raw_effect.value("seed", std::uint32_t{0});
                effect.animated = raw_effect.value("animated", false);
                effect.noise_size = raw_effect.value("size", 1.0f);
                effect.grain_shadow_response = raw_effect.value("shadow_response", 1.0f);
                effect.grain_midtone_response = raw_effect.value("midtone_response", 1.0f);
                effect.grain_highlight_response = raw_effect.value("highlight_response", 1.0f);
                const auto mode = raw_effect.value("color_mode", std::string{"monochrome"});
                if (mode != "monochrome" && mode != "rgb")
                    throw std::runtime_error("Noise color_mode must be monochrome or rgb");
                effect.rgb_noise = mode == "rgb";
                if (!std::isfinite(effect.amount) || effect.amount < 0.0f || effect.amount > 1.0f ||
                    !std::isfinite(effect.noise_size) || effect.noise_size <= 0.0f || effect.noise_size > 256.0f ||
                    !std::isfinite(effect.grain_shadow_response) || effect.grain_shadow_response < 0.0f || effect.grain_shadow_response > 4.0f ||
                    !std::isfinite(effect.grain_midtone_response) || effect.grain_midtone_response < 0.0f || effect.grain_midtone_response > 4.0f ||
                    !std::isfinite(effect.grain_highlight_response) || effect.grain_highlight_response < 0.0f || effect.grain_highlight_response > 4.0f)
                    throw std::runtime_error("Noise amount, size, or tonal response is outside its supported range");
}

void decode_fractal_noise_effect(LayerPlan::EffectPlan& effect,
                            const nlohmann::json& raw_effect) {
                effect.kind = LayerPlan::EffectKindPlan::FractalNoise;
                if (raw_effect.contains("field")) {
                    if (raw_effect.contains("seed") || raw_effect.contains("octaves") ||
                        raw_effect.contains("frequency"))
                        throw std::runtime_error("Fractal Noise field cannot be combined with legacy seed, octaves, or frequency");
                    effect.noise_field = decode_field2d_plan(raw_effect.at("field"));
                }
                effect.seed = raw_effect.value("seed", std::uint32_t{0});
                effect.octaves = raw_effect.value("octaves", std::uint32_t{4});
                effect.frequency = raw_effect.value("frequency", 0.015f);
                effect.amplitude = raw_effect.value("amplitude", 1.0f);
                effect.evolution = raw_effect.value("evolution", 0.0f);
                effect.contrast = raw_effect.value("contrast", 1.0f);
                effect.brightness = raw_effect.value("brightness", 0.0f);
                const auto mode = raw_effect.value("color_mode", std::string{"monochrome"});
                if (mode != "monochrome" && mode != "rgb")
                    throw std::runtime_error("Fractal Noise color_mode must be monochrome or rgb");
                effect.rgb_noise = mode == "rgb";
                if (effect.octaves == 0 || effect.octaves > 16 ||
                    !std::isfinite(effect.frequency) || effect.frequency <= 0.0f || effect.frequency > 10.0f ||
                    !std::isfinite(effect.amplitude) || effect.amplitude < 0.0f || effect.amplitude > 4.0f ||
                    !std::isfinite(effect.evolution) || std::abs(effect.evolution) > 10000.0f ||
                    !std::isfinite(effect.contrast) || effect.contrast < 0.0f || effect.contrast > 16.0f ||
                    !std::isfinite(effect.brightness) || std::abs(effect.brightness) > 16.0f)
                    throw std::runtime_error("Fractal Noise parameters are outside supported finite ranges");
}


} // namespace chronon3d::render_plan::detail
