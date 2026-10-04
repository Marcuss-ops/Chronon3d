#include "render_plan_effect_preparation.hpp"

#include <chronon3d/effects/effect_param_animation.hpp>
#include <chronon3d/effects/cube_lut.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace chronon3d::render_plan::detail {
std::vector<std::vector<EffectParams>> prepare_layer_effects(
    const RenderPlan& plan, chronon3d::assets::AssetResolver& resolver) {
    std::vector<std::vector<EffectParams>> prepared_effects(plan.layers.size());
    for (std::size_t index = 0; index < plan.layers.size(); ++index) {
        for (const auto& effect : plan.layers[index].effects) {
        switch (effect.kind) {
        case LayerPlan::EffectKindPlan::Lut3D: {
            const auto resolved = resolver.resolve_logical(std::filesystem::path(effect.asset));
            if (!resolved) throw std::runtime_error("3D LUT asset was not prepared: " + effect.asset);
            std::ifstream file(*resolved, std::ios::binary);
            if (!file) throw std::runtime_error("failed to open 3D LUT asset: " + effect.asset);
            std::string contents((std::istreambuf_iterator<char>(file)), {});
            if (contents.size() > 8U * 1024U * 1024U)
            throw std::runtime_error("3D LUT asset exceeds the 8 MiB parser limit: " + effect.asset);
            prepared_effects[index].emplace_back(effects::parse_cube_lut(contents, effect.amount));
        } break;
        case LayerPlan::EffectKindPlan::Echo: {
            prepared_effects[index].emplace_back(EchoParams{
            .echoes = effect.echoes, .decay = effect.decay,
            .time_offset_frames = effect.time_offset_frames});
        } break;
        case LayerPlan::EffectKindPlan::Keying: {
            KeyingParams keying;
            keying.mode = effect.keying_mode == LayerPlan::KeyingModePlan::Luma
            ? KeyingMode::Luma : KeyingMode::Chroma;
            keying.key_color = Color{effect.key_color[0], effect.key_color[1], effect.key_color[2], 1.0f};
            keying.threshold = effect.threshold;
            keying.softness = effect.softness;
            keying.spill = effect.spill;
            keying.clip_black = effect.clip_black;
            keying.clip_white = effect.clip_white;
            keying.choke = effect.choke;
            keying.garbage_x = effect.garbage_rect[0];
            keying.garbage_y = effect.garbage_rect[1];
            keying.garbage_w = effect.garbage_rect[2];
            keying.garbage_h = effect.garbage_rect[3];
            keying.garbage_feather = effect.garbage_feather;
            prepared_effects[index].emplace_back(keying);
        } break;
        case LayerPlan::EffectKindPlan::Threshold: {
            prepared_effects[index].emplace_back(ThresholdParams{
            .threshold = effect.threshold,
            .softness = effect.threshold_softness});
        } break;
        case LayerPlan::EffectKindPlan::ChromaticAberration: {
            prepared_effects[index].emplace_back(ChromaticAberrationParams{
            .red_offset = effect.chromatic_red_offset,
            .blue_offset = effect.chromatic_blue_offset,
            .offset_mode = static_cast<ChromaticOffsetMode>(effect.chromatic_offset_mode),
            .velocity = Vec2{effect.chromatic_velocity[0], effect.chromatic_velocity[1]},
            .center = Vec2{effect.radial_center[0], effect.radial_center[1]},
            .red = RgbChannelTransform{Vec2{effect.chromatic_red_transform[0], effect.chromatic_red_transform[1]},
                Vec2{effect.chromatic_red_transform[2], effect.chromatic_red_transform[3]},
                effect.chromatic_red_transform[4], effect.chromatic_red_transform[5]},
            .green = RgbChannelTransform{Vec2{effect.chromatic_green_transform[0], effect.chromatic_green_transform[1]},
                Vec2{effect.chromatic_green_transform[2], effect.chromatic_green_transform[3]},
                effect.chromatic_green_transform[4], effect.chromatic_green_transform[5]},
            .blue = RgbChannelTransform{Vec2{effect.chromatic_blue_transform[0], effect.chromatic_blue_transform[1]},
                Vec2{effect.chromatic_blue_transform[2], effect.chromatic_blue_transform[3]},
                effect.chromatic_blue_transform[4], effect.chromatic_blue_transform[5]}});
        } break;
        case LayerPlan::EffectKindPlan::Halftone: {
            prepared_effects[index].emplace_back(HalftoneParams{
            .size = effect.halftone_size,
            .angle_degrees = effect.halftone_angle,
            .amount = effect.halftone_amount});
        } break;
        case LayerPlan::EffectKindPlan::DirectionalBlur: {
            prepared_effects[index].emplace_back(DirectionalBlurParams{
            .angle = effect.directional_angle,
            .length = effect.directional_length,
            .samples = effect.directional_samples});
        } break;
        case LayerPlan::EffectKindPlan::RadialBlur: {
            prepared_effects[index].emplace_back(RadialBlurParams{
            .center = Vec2{effect.radial_center[0], effect.radial_center[1]},
            .amount = effect.radial_amount,
            .render_samples = effect.radial_render_samples,
            .preview_samples = effect.radial_preview_samples});
        } break;
        case LayerPlan::EffectKindPlan::GaussianBlur: {
            prepared_effects[index].emplace_back(BlurParams{.radius = effect.radius});
        } break;
        case LayerPlan::EffectKindPlan::DropShadow: {
            prepared_effects[index].emplace_back(DropShadowParams{
            .offset = Vec2{effect.offset[0], effect.offset[1]},
            .color = Color{effect.effect_color[0], effect.effect_color[1],
                       effect.effect_color[2], effect.effect_color[3]},
            .radius = effect.radius});
        } break;
        case LayerPlan::EffectKindPlan::Bloom: {
            prepared_effects[index].emplace_back(BloomParams{
            .threshold = effect.bloom_threshold,
            .radius = effect.radius,
            .intensity = effect.bloom_intensity});
        } break;
        case LayerPlan::EffectKindPlan::Glow: {
            prepared_effects[index].emplace_back(GlowParams{
            .radius = effect.radius,
            .intensity = effect.glow_intensity,
            .color = Color{effect.effect_color[0], effect.effect_color[1],
                       effect.effect_color[2], effect.effect_color[3]}});
        } break;
        case LayerPlan::EffectKindPlan::Vignette: {
            prepared_effects[index].emplace_back(VignetteParams{
            .radius = effect.radius, .softness = effect.softness,
            .amount = effect.amount,
            .color = Color{effect.effect_color[0], effect.effect_color[1],
                       effect.effect_color[2], effect.effect_color[3]}});
        } break;
        case LayerPlan::EffectKindPlan::LightRays: {
            prepared_effects[index].emplace_back(LightRaysParams{
            .origin = Vec2{effect.light_rays_origin[0], effect.light_rays_origin[1]},
            .length = effect.light_rays_length,
            .density = effect.light_rays_density,
            .color = Color{effect.light_rays_color[0], effect.light_rays_color[1],
                       effect.light_rays_color[2], effect.light_rays_color[3]},
            .decay = effect.light_rays_decay});
        } break;
        case LayerPlan::EffectKindPlan::Noise: {
            prepared_effects[index].emplace_back(NoiseParams{
            .amount = effect.amount,
            .seed = effect.seed,
            .animated = effect.animated,
            .color_mode = effect.rgb_noise ? NoiseColorMode::RGB
                               : NoiseColorMode::Monochrome,
            .size = effect.noise_size,
            .shadow_response = effect.grain_shadow_response,
            .midtone_response = effect.grain_midtone_response,
            .highlight_response = effect.grain_highlight_response});
        } break;
        case LayerPlan::EffectKindPlan::FractalNoise: {
            prepared_effects[index].emplace_back(FractalNoiseParams{
            .seed = effect.seed,
            .octaves = effect.octaves,
            .frequency = effect.frequency,
            .amplitude = effect.amplitude,
            .evolution = effect.evolution,
            .contrast = effect.contrast,
            .brightness = effect.brightness,
            .color_mode = effect.rgb_noise ? NoiseColorMode::RGB
                               : NoiseColorMode::Monochrome,
            .field = effect.noise_field});
        } break;
        case LayerPlan::EffectKindPlan::Sharpen: {
            prepared_effects[index].emplace_back(SharpenParams{
            .amount = effect.amount, .radius = effect.radius, .threshold = effect.threshold});
        } break;
        case LayerPlan::EffectKindPlan::TurbulentDisplace: {
            prepared_effects[index].emplace_back(TurbulentDisplaceParams{
            .amount=effect.amount,.size=effect.warp_size,.evolution=effect.warp_evolution,
            .evolution_speed=effect.warp_speed,.complexity=effect.warp_complexity});
        } break;
        case LayerPlan::EffectKindPlan::WaveWarp: {
            prepared_effects[index].emplace_back(WaveWarpParams{
            .type=static_cast<WaveWarpParams::WaveType>(effect.wave_type),
            .direction=Vec2{effect.warp_direction[0],effect.warp_direction[1]},
            .amplitude=effect.amount,.wavelength=effect.warp_wavelength,
            .speed=effect.warp_speed,.phase=effect.warp_phase});
        } break;
        case LayerPlan::EffectKindPlan::Ripple: {
            prepared_effects[index].emplace_back(RippleParams{
            .center=Vec2{effect.warp_center[0],effect.warp_center[1]},
            .radius=effect.warp_radius,.wave_speed=effect.warp_speed,
            .width=effect.warp_width,.wave_height=effect.warp_height,.damping=effect.warp_damping});
        } break;
        case LayerPlan::EffectKindPlan::DisplacementMap: {
            prepared_effects[index].emplace_back(DisplacementMapParams{
            .map_layer=effect.map_layer,.intensity_x=effect.map_intensity_x,
            .intensity_y=effect.map_intensity_y,
            .channel=static_cast<DisplacementMapParams::Channel>(effect.map_channel),
            .field=effect.map_field});
        } break;
        case LayerPlan::EffectKindPlan::CornerPin: {
            prepared_effects[index].emplace_back(CornerPinParams{
            .top_left = Vec2{effect.quad[0], effect.quad[1]},
            .top_right = Vec2{effect.quad[2], effect.quad[3]},
            .bottom_right = Vec2{effect.quad[4], effect.quad[5]},
            .bottom_left = Vec2{effect.quad[6], effect.quad[7]}});
        } break;
        case LayerPlan::EffectKindPlan::Twirl: {
            prepared_effects[index].emplace_back(TwirlParams{
            .center = Vec2{effect.warp_center[0], effect.warp_center[1]},
            .angle_degrees = effect.warp_angle,
            .radius = effect.warp_radius,
            .falloff = effect.warp_falloff});
        } break;
        case LayerPlan::EffectKindPlan::Bulge: {
            prepared_effects[index].emplace_back(BulgeParams{
            .center = Vec2{effect.warp_center[0], effect.warp_center[1]},
            .amount = effect.warp_amount,
            .radius = effect.warp_radius});
        } break;
        case LayerPlan::EffectKindPlan::MeshWarp: {
            MeshWarpParams mesh;
            mesh.cols = static_cast<int>(effect.mesh_cols);
            mesh.rows = static_cast<int>(effect.mesh_rows);
            mesh.smoothness = effect.mesh_smoothness;
            mesh.offsets.reserve(effect.mesh_offsets.size() / 2);
            for (std::size_t i = 0; i + 1 < effect.mesh_offsets.size(); i += 2)
            mesh.offsets.push_back(Vec2{effect.mesh_offsets[i], effect.mesh_offsets[i + 1]});
            prepared_effects[index].emplace_back(std::move(mesh));

        } break;
        case LayerPlan::EffectKindPlan::Catalog: {
            auto params = effects::make_default_animatable_effect_params(effect.canonical_effect_id);
            if (!params) throw std::runtime_error("catalog effect cannot be materialized by the compiler");
            for (const auto& value : effect.canonical_params) {
            if (!effects::set_animatable_effect_param(*params, effect.canonical_effect_id,
                                  value.param, value.value))
                throw std::runtime_error("catalog effect parameter failed to apply during compilation");
            }
            prepared_effects[index].push_back(std::move(*params));
        } break;
        case LayerPlan::EffectKindPlan::Mosaic: {
            prepared_effects[index].emplace_back(MosaicParams{
            .block_size = effect.block_size});
        } break;
        case LayerPlan::EffectKindPlan::LensDistortion: {
            prepared_effects[index].emplace_back(LensDistortionParams{
            .curvature = effect.lens_curvature,
            .center = Vec2{effect.lens_center[0], effect.lens_center[1]},
            .scale = effect.lens_scale});
        } break;
        case LayerPlan::EffectKindPlan::LuminanceResponse: {
            prepared_effects[index].emplace_back(LuminanceResponseParams{
            .threshold = effect.response_threshold,
            .softness = effect.response_softness,
            .radius = effect.response_radius,
            .tint = Color{effect.response_tint[0], effect.response_tint[1], effect.response_tint[2], effect.response_tint[3]},
            .intensity = effect.response_intensity,
            .blend = static_cast<LuminanceBlendMode>(effect.response_blend)});
        } break;
        case LayerPlan::EffectKindPlan::FoldWarp: {
            prepared_effects[index].emplace_back(FoldWarpParams{
            .axis = static_cast<FoldAxis>(effect.fold_axis),
            .position = effect.fold_position,
            .amount = effect.fold_amount,
            .radius = effect.fold_radius,
            .falloff = effect.fold_falloff,
            .perspective = effect.fold_perspective});
        } break;
        case LayerPlan::EffectKindPlan::CreaseShading: {
            prepared_effects[index].emplace_back(CreaseShadingParams{
            .axis = static_cast<FoldAxis>(effect.crease_axis),
            .position = effect.crease_position,
            .width = effect.crease_width,
            .dark_side = effect.crease_dark_side,
            .light_side = effect.crease_light_side,
            .softness = effect.crease_softness});
        } break;
        }
        }
    }
    return prepared_effects;
}

}
