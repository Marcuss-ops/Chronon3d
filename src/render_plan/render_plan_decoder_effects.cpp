#include "render_plan_decoder_detail.hpp"
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
#include <unordered_map>
#include <utility>
#include <vector>

namespace chronon3d::render_plan::detail {
std::string_view effect_id_for_plan_kind(LayerPlan::EffectKindPlan kind) {
    using K = LayerPlan::EffectKindPlan;
    using namespace chronon3d::effects::ids;
    switch (kind) {
        case K::Lut3D: return ColorLut3D;
        case K::Echo: return TemporalEcho;
        case K::Keying: return MatteKeying;
        case K::Threshold: return StylizeThreshold;
        case K::Mosaic: return StylizeMosaic;
        case K::GaussianBlur: return BlurGaussian;
        case K::DropShadow: return LightDropShadow;
        case K::Bloom: return LightBloom;
        case K::Glow: return LightGlow;
        case K::Noise: return GenerateNoise;
        case K::FractalNoise: return GenerateFractalNoise;
        case K::CornerPin: return DistortCornerPin;
        case K::Twirl: return DistortTwirl;
        case K::Bulge: return DistortBulge;
        case K::MeshWarp: return DistortMeshWarp;
        case K::Sharpen: return StylizeSharpen;
        case K::RoughenEdges: return StylizeRoughenEdges;
        case K::TurbulentDisplace: return DistortTurbulentDisplace;
        case K::WaveWarp: return DistortWaveWarp;
        case K::Ripple: return DistortRipple;
        case K::DisplacementMap: return DistortDisplacementMap;
        case K::LightRays: return GenerateLightRays;
        case K::ChromaticAberration: return StylizeChromaticAberration;
        case K::Halftone: return StylizeHalftone;
        case K::DirectionalBlur: return DistortDirectionalBlur;
        case K::RadialBlur: return DistortRadialBlur;
        case K::Vignette: return ColorVignette;
        case K::Catalog: return {};
        case K::LensDistortion: return DistortLensDistortion;
        case K::LuminanceResponse: return LightLuminanceResponse;
        case K::FoldWarp: return DistortFoldWarp;
        case K::CreaseShading: return LightCreaseShading;
        case K::NoiseSampler: return GenerateNoiseSampler;
        case K::BandedGrain: return StylizeBandedGrain;
    }
    return {};
}

LayerPlan::EffectKindPlan decode_effect_kind(std::string_view name) {
    using K = LayerPlan::EffectKindPlan;
    static const std::unordered_map<std::string_view, K> kinds{
        {"lut_3d", K::Lut3D}, {"echo", K::Echo}, {"keying", K::Keying},
        {"threshold", K::Threshold}, {"mosaic", K::Mosaic},
        {"gaussian_blur", K::GaussianBlur}, {"drop_shadow", K::DropShadow},
        {"bloom", K::Bloom}, {"glow", K::Glow}, {"noise", K::Noise},
        {"fractal_noise", K::FractalNoise}, {"corner_pin", K::CornerPin},
        {"twirl", K::Twirl}, {"bulge", K::Bulge}, {"mesh_warp", K::MeshWarp},
        {"sharpen", K::Sharpen}, {"roughen_edges", K::RoughenEdges},
        {"turbulent_displace", K::TurbulentDisplace},
        {"wave_warp", K::WaveWarp}, {"ripple", K::Ripple},
        {"displacement_map", K::DisplacementMap}, {"light_rays", K::LightRays},
        {"chromatic_aberration", K::ChromaticAberration}, {"chromatic", K::ChromaticAberration},
        {"rgb_channel_transform", K::ChromaticAberration},
        {"halftone", K::Halftone}, {"directional_blur", K::DirectionalBlur},
        {"radial_blur", K::RadialBlur}, {"vignette", K::Vignette},
        {"catalog", K::Catalog},
        {"lens_distortion", K::LensDistortion},
        {"luminance_response", K::LuminanceResponse},
        {"fold_warp", K::FoldWarp},
        {"crease_shading", K::CreaseShading},
        {"noise_sampler", K::NoiseSampler},
        {"banded_grain", K::BandedGrain},
    };
    const auto found = kinds.find(name);
    if (found == kinds.end()) throw std::runtime_error("unsupported layer effect type");
    return found->second;
}

void decode_layer_effects(LayerPlan& layer, const nlohmann::json& value) {
    if (value.contains("effects")) {
        const auto& effects = value.at("effects");
        if (!effects.is_array() || effects.size() > 64)
            throw std::runtime_error("layer.effects must be an array of at most 64 entries");
        for (const auto& raw_effect : effects) {
            LayerPlan::EffectPlan effect;
            const auto kind_name = raw_effect.value("type", std::string{});
            const auto kind = decode_effect_kind(kind_name);
            if (raw_effect.contains("time")) {
                const auto& time=raw_effect.at("time");
                if(!time.is_object()) throw std::runtime_error("effect time must be an object");
                if(time.contains("posterize_fps")) {
                    effect.posterize_fps=time.at("posterize_fps").get<float>();
                    if(!std::isfinite(*effect.posterize_fps)||*effect.posterize_fps<=0.0f||*effect.posterize_fps>1000.0f)
                        throw std::runtime_error("effect time posterize_fps must be finite and in (0, 1000]");
                }
                if(time.contains("curve")) {
                    const auto& raw_curve=time.at("curve");
                    if(!raw_curve.is_array()||raw_curve.size()<2) throw std::runtime_error("effect time curve must contain at least two keyframes");
                    AnimationTrackPlan curve; curve.property="effect_time";
                    curve.easing=time.value("easing",std::string{"linear"});
                    curve.bezier_curve=decode_bezier_curve(time,curve.easing,"effect time");
                    for(const auto& raw_key:raw_curve) {
                        AnimationKeyframePlan key;
                        key.frame=Frame{raw_key.at("frame").get<std::int64_t>()};
                        const float v=raw_key.at("value").get<float>();
                        if(key.frame.integral()<0||key.frame.integral()>1000000||!std::isfinite(v)||v<0.0f||v>1.0f)
                            throw std::runtime_error("effect time curve keyframes require bounded frame values and values in [0, 1]");
                        key.value={v}; curve.keyframes.push_back(std::move(key));
                    }
                    std::sort(curve.keyframes.begin(),curve.keyframes.end(),[](const auto& a,const auto& b){return a.frame<b.frame;});
                    for(std::size_t i=1;i<curve.keyframes.size();++i)
                        if(curve.keyframes[i-1].frame==curve.keyframes[i].frame) throw std::runtime_error("effect time curve frames must be unique");
                    effect.time_curve=std::move(curve);
                } else if(time.contains("easing")||time.contains("bezier_curve"))
                    throw std::runtime_error("effect time easing requires a curve");
                for(auto it=time.begin();it!=time.end();++it)
                    if(it.key()!="posterize_fps"&&it.key()!="curve"&&it.key()!="easing"&&it.key()!="bezier_curve")
                        throw std::runtime_error("unsupported effect time option: "+it.key());
            }
            switch (kind) {
            case LayerPlan::EffectKindPlan::Catalog: {
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
            } break;
            case LayerPlan::EffectKindPlan::Lut3D: {
                effect.kind = LayerPlan::EffectKindPlan::Lut3D;
                effect.asset = raw_effect.at("asset").get<std::string>();
                effect.amount = raw_effect.value("amount", 1.0f);
                if (effect.asset.empty() || effect.asset.size() > 4096 ||
                    !std::isfinite(effect.amount) || effect.amount < 0.0f || effect.amount > 1.0f)
                    throw std::runtime_error("3D LUT effect requires an asset and amount in [0, 1]");
            } break;
            case LayerPlan::EffectKindPlan::Echo: {
                effect.kind = LayerPlan::EffectKindPlan::Echo;
                effect.echoes = raw_effect.value("echoes", 3u);
                effect.decay = raw_effect.value("decay", 0.5f);
                effect.time_offset_frames = raw_effect.value("time_offset_frames", 1u);
                if (effect.echoes == 0 || effect.echoes > 8 || !std::isfinite(effect.decay) ||
                    effect.decay < 0.0f || effect.decay > 1.0f ||
                    effect.time_offset_frames == 0 || effect.time_offset_frames > 120)
                    throw std::runtime_error("Echo requires echoes in [1, 8], decay in [0, 1], and time_offset_frames in [1, 120]");
            } break;
            case LayerPlan::EffectKindPlan::Keying: {
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
            } break;
            case LayerPlan::EffectKindPlan::Threshold: {
                effect.kind = LayerPlan::EffectKindPlan::Threshold;
                effect.threshold = raw_effect.value("threshold", 0.5f);
                effect.threshold_softness = raw_effect.value("softness", 0.0f);
                if (!std::isfinite(effect.threshold) || effect.threshold < 0.0f || effect.threshold > 1.0f ||
                    !std::isfinite(effect.threshold_softness) || effect.threshold_softness < 0.0f || effect.threshold_softness > 1.0f)
                    throw std::runtime_error("Threshold and softness must be in [0, 1]");
            } break;
            case LayerPlan::EffectKindPlan::Mosaic: {
                effect.kind = LayerPlan::EffectKindPlan::Mosaic;
                effect.block_size = raw_effect.value("block_size", 8u);
                if (effect.block_size == 0 || effect.block_size > 512)
                    throw std::runtime_error("Mosaic block_size must be in [1, 512]");
            } break;
            case LayerPlan::EffectKindPlan::ChromaticAberration: {
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
            } break;
            case LayerPlan::EffectKindPlan::Halftone: {
                effect.kind = LayerPlan::EffectKindPlan::Halftone;
                effect.halftone_size = raw_effect.value("size", 8u);
                effect.halftone_angle = raw_effect.value("angle", 0.0f);
                effect.halftone_amount = raw_effect.value("amount", 1.0f);
                if (effect.halftone_size == 0 || effect.halftone_size > 128 ||
                    !std::isfinite(effect.halftone_angle) ||
                    !std::isfinite(effect.halftone_amount) ||
                    effect.halftone_amount < 0.0f || effect.halftone_amount > 1.0f)
                    throw std::runtime_error("Halftone requires size in [1, 128], finite angle, and amount in [0, 1]");
            } break;
            case LayerPlan::EffectKindPlan::DirectionalBlur: {
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
            } break;
            case LayerPlan::EffectKindPlan::RadialBlur: {
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
            } break;
            case LayerPlan::EffectKindPlan::GaussianBlur: {
                effect.kind = LayerPlan::EffectKindPlan::GaussianBlur;
                effect.radius = raw_effect.value("radius", 8.0f);
                if (!std::isfinite(effect.radius) || effect.radius < 0.0f || effect.radius > 256.0f)
                    throw std::runtime_error("Gaussian blur radius must be in [0, 256]");
            } break;
            case LayerPlan::EffectKindPlan::DropShadow: {
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
            } break;
            case LayerPlan::EffectKindPlan::Bloom: {
                effect.kind = LayerPlan::EffectKindPlan::Bloom;
                effect.bloom_threshold = raw_effect.value("threshold", 0.80f);
                effect.radius = raw_effect.value("radius", 24.0f);
                effect.bloom_intensity = raw_effect.value("intensity", 0.60f);
                if (!std::isfinite(effect.bloom_threshold) || effect.bloom_threshold < 0.0f || effect.bloom_threshold > 1.0f ||
                    !std::isfinite(effect.radius) || effect.radius < 0.0f || effect.radius > 256.0f ||
                    !std::isfinite(effect.bloom_intensity) || effect.bloom_intensity < 0.0f || effect.bloom_intensity > 4.0f)
                    throw std::runtime_error("Bloom requires threshold in [0, 1], radius in [0, 256], and intensity in [0, 4]");
            } break;
            case LayerPlan::EffectKindPlan::Glow: {
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
            } break;
            case LayerPlan::EffectKindPlan::Vignette: {
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
            } break;
            case LayerPlan::EffectKindPlan::LightRays: {
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
            } break;
            case LayerPlan::EffectKindPlan::Noise: {
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
            } break;
            case LayerPlan::EffectKindPlan::FractalNoise: {
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
            } break;
            case LayerPlan::EffectKindPlan::Sharpen: {
                effect.kind = LayerPlan::EffectKindPlan::Sharpen;
                effect.amount = raw_effect.value("amount", 0.0f);
                effect.radius = raw_effect.value("radius", 1.0f);
                effect.threshold = raw_effect.value("threshold", 0.0f);
                if (!std::isfinite(effect.amount) || effect.amount < -4.0f || effect.amount > 4.0f ||
                    !std::isfinite(effect.radius) || effect.radius <= 0.0f || effect.radius > 64.0f ||
                    !std::isfinite(effect.threshold) || effect.threshold < 0.0f || effect.threshold > 1.0f)
                    throw std::runtime_error("Invalid sharpen parameters");
            } break;
            case LayerPlan::EffectKindPlan::RoughenEdges: {
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
            } break;
            case LayerPlan::EffectKindPlan::TurbulentDisplace: {
                effect.kind = LayerPlan::EffectKindPlan::TurbulentDisplace;
                effect.amount=raw_effect.value("amount",0.1f); effect.warp_size=raw_effect.value("size",0.1f);
                effect.warp_evolution=raw_effect.value("evolution",0.0f); effect.warp_speed=raw_effect.value("evolution_speed",0.0f);
                effect.warp_complexity=raw_effect.value("complexity",2U);
                if(!std::isfinite(effect.amount)||effect.amount<0||effect.amount>1||!std::isfinite(effect.warp_size)||effect.warp_size<=0||
                   !std::isfinite(effect.warp_evolution)||!std::isfinite(effect.warp_speed)||effect.warp_complexity<1||effect.warp_complexity>16)
                    throw std::runtime_error("Invalid turbulent displace parameters");
            } break;
            case LayerPlan::EffectKindPlan::WaveWarp: {
                effect.kind=LayerPlan::EffectKindPlan::WaveWarp; effect.amount=raw_effect.value("amplitude",0.05f);
                effect.warp_wavelength=raw_effect.value("wavelength",0.2f); effect.warp_speed=raw_effect.value("speed",1.0f); effect.warp_phase=raw_effect.value("phase",0.0f);
                if(raw_effect.contains("direction")) { const auto& v=raw_effect.at("direction"); if(!v.is_array()||v.size()!=2) throw std::runtime_error("wave direction requires two values"); effect.warp_direction={v[0].get<float>(),v[1].get<float>()}; }
                const auto type=raw_effect.value("wave_type",std::string{"sine"});
                if(type=="triangle") effect.wave_type=1; else if(type=="square") effect.wave_type=2; else if(type=="saw") effect.wave_type=3; else if(type!="sine") throw std::runtime_error("Invalid wave type");
                if(!std::isfinite(effect.amount)||!std::isfinite(effect.warp_wavelength)||effect.warp_wavelength<=0||!std::isfinite(effect.warp_speed)||!std::isfinite(effect.warp_phase)||!std::isfinite(effect.warp_direction[0])||!std::isfinite(effect.warp_direction[1])) throw std::runtime_error("Invalid wave warp parameters");
            } break;
            case LayerPlan::EffectKindPlan::Ripple: {
                effect.kind=LayerPlan::EffectKindPlan::Ripple; effect.warp_radius=raw_effect.value("radius",0.25f);
                effect.warp_speed=raw_effect.value("wave_speed",1.5f); effect.warp_width=raw_effect.value("width",0.1f);
                effect.warp_height=raw_effect.value("wave_height",0.05f); effect.warp_damping=raw_effect.value("damping",0.5f);
                if(raw_effect.contains("center")) { const auto& v=raw_effect.at("center"); if(!v.is_array()||v.size()!=2) throw std::runtime_error("ripple center requires two values"); effect.warp_center={v[0].get<float>(),v[1].get<float>()}; }
                if(!std::isfinite(effect.warp_radius)||effect.warp_radius<=0||!std::isfinite(effect.warp_speed)||!std::isfinite(effect.warp_width)||effect.warp_width<=0||!std::isfinite(effect.warp_height)||!std::isfinite(effect.warp_damping)||effect.warp_damping<0||!std::isfinite(effect.warp_center[0])||!std::isfinite(effect.warp_center[1])) throw std::runtime_error("Invalid ripple parameters");
            } break;
            case LayerPlan::EffectKindPlan::DisplacementMap: {
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
            } break;
            case LayerPlan::EffectKindPlan::CornerPin: {
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
            } break;
            case LayerPlan::EffectKindPlan::Twirl: {
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
            } break;
            case LayerPlan::EffectKindPlan::Bulge: {
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
            } break;
            case LayerPlan::EffectKindPlan::MeshWarp: {
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

            } break;
            case LayerPlan::EffectKindPlan::LensDistortion: {
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
            } break;
            case LayerPlan::EffectKindPlan::LuminanceResponse: {
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
            } break;
            case LayerPlan::EffectKindPlan::FoldWarp: {
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
            } break;
            case LayerPlan::EffectKindPlan::CreaseShading: {
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
            } break;
            case LayerPlan::EffectKindPlan::NoiseSampler: {
                effect.kind = LayerPlan::EffectKindPlan::NoiseSampler;
                effect.seed = raw_effect.value("seed", 0u);
                const auto type_str = raw_effect.value("noise_type", std::string{"simplex"});
                if (type_str == "worley" || type_str == "cellular") effect.noise_sampler_type = 1;
                else if (type_str == "ridged") effect.noise_sampler_type = 2;
                else if (type_str == "value") effect.noise_sampler_type = 3;
                else if (type_str == "blue") effect.noise_sampler_type = 4;
                else effect.noise_sampler_type = 0; // simplex
                effect.octaves = raw_effect.value("octaves", 4u);
                effect.frequency = raw_effect.value("frequency", 0.015f);
                effect.noise_lacunarity = raw_effect.value("lacunarity", 2.0f);
                effect.noise_gain = raw_effect.value("gain", 0.5f);
                effect.evolution = raw_effect.value("evolution", 0.0f);
                effect.contrast = raw_effect.value("contrast", 1.0f);
                effect.brightness = raw_effect.value("brightness", 0.0f);
                effect.noise_domain_warp = raw_effect.value("domain_warp", 0.0f);
                effect.noise_remap_min = raw_effect.value("remap_min", 0.0f);
                effect.noise_remap_max = raw_effect.value("remap_max", 1.0f);
                effect.noise_quantize_steps = raw_effect.value("quantize_steps", 0u);
                effect.rgb_noise = raw_effect.value("color_mode", std::string{"monochrome"}) == "rgb";
                if (!std::isfinite(effect.frequency) || effect.frequency <= 0.0f ||
                    !std::isfinite(effect.contrast) || !std::isfinite(effect.brightness) ||
                    !std::isfinite(effect.noise_domain_warp))
                    throw std::runtime_error("Invalid noise sampler parameters");
            } break;
            case LayerPlan::EffectKindPlan::BandedGrain: {
                effect.kind = LayerPlan::EffectKindPlan::BandedGrain;
                effect.seed = raw_effect.value("seed", 0u);
                effect.amount = raw_effect.value("intensity", 1.0f);
                effect.warp_speed = raw_effect.value("speed", 0.0f);
                effect.rgb_noise = raw_effect.value("color_mode", std::string{"monochrome"}) == "rgb";
                effect.grain_shadow_response = raw_effect.value("shadow_response", 1.0f);
                effect.grain_midtone_response = raw_effect.value("midtone_response", 1.0f);
                effect.grain_highlight_response = raw_effect.value("highlight_response", 1.0f);
                if (raw_effect.contains("bands")) {
                    for (const auto& raw_band : raw_effect.at("bands")) {
                        LayerPlan::EffectPlan::GrainBandPlan band;
                        const auto scale_val = raw_band.value("scale", 1.0f);
                        band.scale = scale_val;
                        band.amplitude = raw_band.value("amplitude", 0.05f);
                        band.octave = raw_band.value("octave", 0u);
                        effect.grain_bands.push_back(band);
                    }
                }
                if (!std::isfinite(effect.amount) || effect.amount < 0.0f ||
                    !std::isfinite(effect.warp_speed))
                    throw std::runtime_error("Invalid banded grain parameters");
            } break;
            }
            layer.effects.push_back(std::move(effect));
        }
    }
}

}
