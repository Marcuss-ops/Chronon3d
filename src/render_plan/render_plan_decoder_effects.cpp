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
        case K::Scanlines: return StylizeScanlines;
        case K::RetroTv: return StylizeRetroTv;
        case K::Halation: return LightHalation;
        case K::AnamorphicStreak: return LightAnamorphicStreak;
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
        {"scanlines", K::Scanlines},
        {"retro_tv", K::RetroTv},
        {"halation", K::Halation},
        {"anamorphic_streak", K::AnamorphicStreak},
    };
    const auto found = kinds.find(name);
    if (found == kinds.end()) throw std::runtime_error("unsupported layer effect type");
    return found->second;
}


struct EffectDecoderEntry {
    LayerPlan::EffectKindPlan kind;
    void (*decode)(LayerPlan::EffectPlan&, const nlohmann::json&);
};

constexpr EffectDecoderEntry kEffectDecoders[] = {
    {LayerPlan::EffectKindPlan::Catalog, &decode_catalog_effect},
    {LayerPlan::EffectKindPlan::Lut3D, &decode_lut_3d_effect},
    {LayerPlan::EffectKindPlan::Echo, &decode_echo_effect},
    {LayerPlan::EffectKindPlan::Keying, &decode_keying_effect},
    {LayerPlan::EffectKindPlan::Threshold, &decode_threshold_effect},
    {LayerPlan::EffectKindPlan::Mosaic, &decode_mosaic_effect},
    {LayerPlan::EffectKindPlan::ChromaticAberration, &decode_chromatic_aberration_effect},
    {LayerPlan::EffectKindPlan::Halftone, &decode_halftone_effect},
    {LayerPlan::EffectKindPlan::DirectionalBlur, &decode_directional_blur_effect},
    {LayerPlan::EffectKindPlan::RadialBlur, &decode_radial_blur_effect},
    {LayerPlan::EffectKindPlan::GaussianBlur, &decode_gaussian_blur_effect},
    {LayerPlan::EffectKindPlan::DropShadow, &decode_drop_shadow_effect},
    {LayerPlan::EffectKindPlan::Bloom, &decode_bloom_effect},
    {LayerPlan::EffectKindPlan::Glow, &decode_glow_effect},
    {LayerPlan::EffectKindPlan::Vignette, &decode_vignette_effect},
    {LayerPlan::EffectKindPlan::LightRays, &decode_light_rays_effect},
    {LayerPlan::EffectKindPlan::Noise, &decode_noise_effect},
    {LayerPlan::EffectKindPlan::FractalNoise, &decode_fractal_noise_effect},
    {LayerPlan::EffectKindPlan::Sharpen, &decode_sharpen_effect},
    {LayerPlan::EffectKindPlan::RoughenEdges, &decode_roughen_edges_effect},
    {LayerPlan::EffectKindPlan::TurbulentDisplace, &decode_turbulent_displace_effect},
    {LayerPlan::EffectKindPlan::WaveWarp, &decode_wave_warp_effect},
    {LayerPlan::EffectKindPlan::Ripple, &decode_ripple_effect},
    {LayerPlan::EffectKindPlan::DisplacementMap, &decode_displacement_map_effect},
    {LayerPlan::EffectKindPlan::CornerPin, &decode_corner_pin_effect},
    {LayerPlan::EffectKindPlan::Twirl, &decode_twirl_effect},
    {LayerPlan::EffectKindPlan::Bulge, &decode_bulge_effect},
    {LayerPlan::EffectKindPlan::MeshWarp, &decode_mesh_warp_effect},
    {LayerPlan::EffectKindPlan::LensDistortion, &decode_lens_distortion_effect},
    {LayerPlan::EffectKindPlan::LuminanceResponse, &decode_luminance_response_effect},
    {LayerPlan::EffectKindPlan::FoldWarp, &decode_fold_warp_effect},
    {LayerPlan::EffectKindPlan::CreaseShading, &decode_crease_shading_effect},
    {LayerPlan::EffectKindPlan::Scanlines, &decode_scanlines_effect},
    {LayerPlan::EffectKindPlan::RetroTv, &decode_retro_tv_effect},
    {LayerPlan::EffectKindPlan::Halation, &decode_halation_effect},
    {LayerPlan::EffectKindPlan::AnamorphicStreak, &decode_anamorphic_streak_effect},
};

const EffectDecoderEntry* find_effect_decoder(LayerPlan::EffectKindPlan kind) {
    for (const auto& entry : kEffectDecoders)
        if (entry.kind == kind) return &entry;
    return nullptr;
}
void decode_effect_time(LayerPlan::EffectPlan& effect, const nlohmann::json& time) {
    if (!time.is_object())
        throw std::runtime_error("effect time must be an object");

    if (time.contains("posterize_fps")) {
        effect.posterize_fps = time.at("posterize_fps").get<float>();
        if (!std::isfinite(*effect.posterize_fps) ||
            *effect.posterize_fps <= 0.0f || *effect.posterize_fps > 1000.0f)
            throw std::runtime_error(
                "effect time posterize_fps must be finite and in (0, 1000]");
    }

    if (time.contains("curve")) {
        const auto& raw_curve = time.at("curve");
        if (!raw_curve.is_array() || raw_curve.size() < 2)
            throw std::runtime_error("effect time curve must contain at least two keyframes");

        AnimationTrackPlan curve;
        curve.property = "effect_time";
        curve.easing = time.value("easing", std::string{"linear"});
        curve.bezier_curve = decode_bezier_curve(time, curve.easing, "effect time");
        for (const auto& raw_key : raw_curve) {
            AnimationKeyframePlan key;
            key.frame = Frame{raw_key.at("frame").get<std::int64_t>()};
            const float value = raw_key.at("value").get<float>();
            if (key.frame.integral() < 0 || key.frame.integral() > 1000000 ||
                !std::isfinite(value) || value < 0.0f || value > 1.0f)
                throw std::runtime_error(
                    "effect time curve keyframes require bounded frame values and values in [0, 1]");
            key.value = {value};
            curve.keyframes.push_back(std::move(key));
        }
        std::sort(curve.keyframes.begin(), curve.keyframes.end(),
            [](const auto& left, const auto& right) { return left.frame < right.frame; });
        for (std::size_t index = 1; index < curve.keyframes.size(); ++index)
            if (curve.keyframes[index - 1].frame == curve.keyframes[index].frame)
                throw std::runtime_error("effect time curve frames must be unique");
        effect.time_curve = std::move(curve);
    } else if (time.contains("easing") || time.contains("bezier_curve")) {
        throw std::runtime_error("effect time easing requires a curve");
    }

    for (auto it = time.begin(); it != time.end(); ++it)
        if (it.key() != "posterize_fps" && it.key() != "curve" &&
            it.key() != "easing" && it.key() != "bezier_curve")
            throw std::runtime_error("unsupported effect time option: " + it.key());
}

void decode_layer_effects(LayerPlan& layer, const nlohmann::json& value) {
    if (!value.contains("effects")) return;

    const auto& effects = value.at("effects");
    if (!effects.is_array() || effects.size() > 64)
        throw std::runtime_error("layer.effects must be an array of at most 64 entries");

    for (const auto& raw_effect : effects) {
        LayerPlan::EffectPlan effect;
        const auto kind = decode_effect_kind(raw_effect.value("type", std::string{}));
        if (raw_effect.contains("time"))
            decode_effect_time(effect, raw_effect.at("time"));

        const auto* decoder = find_effect_decoder(kind);
        if (!decoder) throw std::runtime_error("unsupported layer effect type");
        decoder->decode(effect, raw_effect);
        layer.effects.push_back(std::move(effect));
    }
}

}
