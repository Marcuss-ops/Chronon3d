#include "fingerprint_detail.hpp"

namespace chronon3d::render_plan::fingerprint_detail {

void add_layer_effects_and_masks(CanonicalPlanWriter& out, const LayerPlan& layer) {
    if (layer.particle_emitter) {
        const auto& particles = *layer.particle_emitter;
        out.add_uint64(particles.count);
        out.add_uint64(particles.seed);
        out.add_float(particles.emitter_size.x);
        out.add_float(particles.emitter_size.y);
        out.add_float(particles.velocity_min.x);
        out.add_float(particles.velocity_min.y);
        out.add_float(particles.velocity_max.x);
        out.add_float(particles.velocity_max.y);
        out.add_float(particles.lifetime_min);
        out.add_float(particles.lifetime_max);
        out.add_float(particles.size_min);
        out.add_float(particles.size_max);
        out.add_float(particles.gravity);
        out.add_float(particles.turbulence);
        out.add_uint64(static_cast<std::uint64_t>(particles.shape));
        out.add_string(particles.path_id); out.add_string(particles.sprite_asset);
        out.add_float(particles.sprite_scale_min); out.add_float(particles.sprite_scale_max);
        out.add_float(particles.rotation_min); out.add_float(particles.rotation_max);
        out.add_float(particles.angular_velocity_min); out.add_float(particles.angular_velocity_max);
        out.add_enum(particles.blend_mode);
        if (particles.environment || particles.depth_min != 0.0f ||
            particles.depth_max != 1.0f || particles.bokeh) {
            out.add_bool(true);
            out.add_bool(particles.environment);
            out.add_float(particles.depth_min);
            out.add_float(particles.depth_max);
            out.add_bool(particles.bokeh);
        }
        out.add_size(particles.path_points.size());
        for (const auto point : particles.path_points) { out.add_float(point.x); out.add_float(point.y); }
        add_gradient(out, particles.color_ramp);
    }
    if (layer.light) {
        out.add_float(layer.light->radius);
        for (const auto component : layer.light->color) out.add_float(component);
        out.add_float(layer.light->intensity);
        out.add_enum(layer.light->kind);
        for (const auto component : layer.light->direction) out.add_float(component);
        out.add_float(layer.light->range);
        out.add_float(layer.light->decay);
        out.add_float(layer.light->angle);
        out.add_float(layer.light->penumbra);
        out.add_bool(layer.light->cast_shadow);
        out.add_float(layer.light->shadow_radius);
        out.add_float(layer.light->shadow_bias);
    }
    if (layer.path_mask) {
        out.add_bool(true);
        out.add_string(layer.path_mask->type);
        out.add_string(layer.path_mask->mode);
        for (const auto value : layer.path_mask->position) out.add_float(value);
        for (const auto value : layer.path_mask->size) out.add_float(value);
        out.add_float(layer.path_mask->radius);
        out.add_float(layer.path_mask->feather);
        out.add_float(layer.path_mask->expansion);
        out.add_float(layer.path_mask->opacity);
        add_optional(out, layer.path_mask->field,
            [](auto& writer, const graphics::Field2D& field) { add_field2d(writer, field); });
        add_optional(out, layer.path_mask->feather_track,
            [](auto& writer, const AnimationTrackPlan& track) { add_track(writer, track); });
        add_optional(out, layer.path_mask->expansion_track,
            [](auto& writer, const AnimationTrackPlan& track) { add_track(writer, track); });
        add_optional(out, layer.path_mask->opacity_track,
            [](auto& writer, const AnimationTrackPlan& track) { add_track(writer, track); });
        out.add_bool(layer.path_mask->inverted);
        out.add_size(layer.path_mask->path.size());
        for (const auto& command : layer.path_mask->path) {
            out.add_enum(command.type);
            for (const auto value : command.point) out.add_float(value);
            for (const auto value : command.control1) out.add_float(value);
            for (const auto value : command.control2) out.add_float(value);
        }
        out.add_size(layer.path_mask->morph_target.size());
        for (const auto& command : layer.path_mask->morph_target) {
            out.add_enum(command.type);
            for (const auto value : command.point) out.add_float(value);
            for (const auto value : command.control1) out.add_float(value);
            for (const auto value : command.control2) out.add_float(value);
        }
        add_optional(out, layer.path_mask->morph_animation,
            [](auto& writer, const AnimationTrackPlan& animation) { add_track(writer, animation); });
    }
    out.add_size(layer.masks.size());
    for (const auto& mask : layer.masks) {
        out.add_string(mask.type);
        out.add_string(mask.mode);
        out.add_string(mask.text_source);
        for (const auto value : mask.position) out.add_float(value);
        for (const auto value : mask.size) out.add_float(value);
        out.add_float(mask.radius);
        out.add_float(mask.feather);
        out.add_float(mask.expansion);
        out.add_float(mask.opacity);
        add_optional(out, mask.field,
            [](auto& writer, const graphics::Field2D& field) { add_field2d(writer, field); });
        add_optional(out, mask.feather_track,
            [](auto& writer, const AnimationTrackPlan& track) { add_track(writer, track); });
        add_optional(out, mask.expansion_track,
            [](auto& writer, const AnimationTrackPlan& track) { add_track(writer, track); });
        add_optional(out, mask.opacity_track,
            [](auto& writer, const AnimationTrackPlan& track) { add_track(writer, track); });
        out.add_bool(mask.inverted);
        const auto add_mask_commands = [&](const auto& commands) {
            out.add_size(commands.size());
            for (const auto& command : commands) {
                out.add_enum(command.type);
                for (const auto value : command.point) out.add_float(value);
                for (const auto value : command.control1) out.add_float(value);
                for (const auto value : command.control2) out.add_float(value);
            }
        };
        add_mask_commands(mask.path);
        add_mask_commands(mask.morph_target);
        add_optional(out, mask.morph_animation,
            [](auto& writer, const AnimationTrackPlan& animation) { add_track(writer, animation); });
    }
    if (!layer.effects.empty()) {
        out.add_bool(true);
        out.add_size(layer.effects.size());
        for (const auto& effect : layer.effects) {
            out.add_enum(effect.kind);
            switch (effect.kind) {
            case LayerPlan::EffectKindPlan::Lut3D: {
                out.add_string(effect.asset);
                out.add_float(effect.amount);
            } break;
            case LayerPlan::EffectKindPlan::Echo: {
                out.add_size(effect.echoes);
                out.add_float(effect.decay);
                out.add_size(effect.time_offset_frames);
            } break;
            case LayerPlan::EffectKindPlan::Keying: {
                out.add_enum(effect.keying_mode);
                for (const auto value : effect.key_color) out.add_float(value);
                out.add_float(effect.threshold);
                out.add_float(effect.softness);
                out.add_float(effect.spill);
                out.add_float(effect.clip_black);
                out.add_float(effect.clip_white);
                out.add_float(effect.choke);
                for (const auto value : effect.garbage_rect) out.add_float(value);
                out.add_float(effect.garbage_feather);
            } break;
            case LayerPlan::EffectKindPlan::Threshold: {
                out.add_float(effect.threshold);
                out.add_float(effect.threshold_softness);
            } break;
            case LayerPlan::EffectKindPlan::GaussianBlur: {
                out.add_float(effect.radius);
            } break;
            case LayerPlan::EffectKindPlan::DropShadow: {
                out.add_float(effect.radius);
                for (const auto value : effect.offset) out.add_float(value);
                for (const auto value : effect.effect_color) out.add_float(value);
            } break;
            case LayerPlan::EffectKindPlan::Bloom: {
                out.add_float(effect.bloom_threshold);
                out.add_float(effect.radius);
                out.add_float(effect.bloom_intensity);
            } break;
            case LayerPlan::EffectKindPlan::Glow: {
                out.add_float(effect.radius);
                out.add_float(effect.glow_intensity);
                for (const auto value : effect.effect_color) out.add_float(value);
            } break;
            case LayerPlan::EffectKindPlan::Vignette: {
                out.add_float(effect.radius);
                out.add_float(effect.softness);
                out.add_float(effect.amount);
                for (const auto value : effect.effect_color) out.add_float(value);
            } break;
            case LayerPlan::EffectKindPlan::Catalog: {
                out.add_string(effect.canonical_effect_id);
                out.add_size(effect.canonical_params.size());
                for (const auto& value : effect.canonical_params) {
                    out.add_string(value.param);
                    out.add_float(value.value);
                }
            } break;
            case LayerPlan::EffectKindPlan::LightRays: {
                for (const auto value : effect.light_rays_origin) out.add_float(value);
                out.add_float(effect.light_rays_length);
                out.add_float(effect.light_rays_density);
                for (const auto value : effect.light_rays_color) out.add_float(value);
                out.add_float(effect.light_rays_decay);
            } break;
            case LayerPlan::EffectKindPlan::Noise: {
                out.add_float(effect.amount);
                out.add_uint64(effect.seed);
                out.add_bool(effect.animated);
                out.add_bool(effect.rgb_noise);
                out.add_float(effect.noise_size);
                out.add_float(effect.grain_shadow_response);
                out.add_float(effect.grain_midtone_response);
                out.add_float(effect.grain_highlight_response);
            } break;
            case LayerPlan::EffectKindPlan::FractalNoise: {
                out.add_uint64(effect.seed);
                out.add_uint64(effect.octaves);
                out.add_float(effect.frequency);
                out.add_float(effect.amplitude);
                out.add_float(effect.evolution);
                out.add_float(effect.contrast);
                out.add_float(effect.brightness);
                out.add_bool(effect.rgb_noise);
                add_optional(out, effect.noise_field,
                    [](auto& writer, const graphics::Field2D& field) { add_field2d(writer, field); });
            } break;
            case LayerPlan::EffectKindPlan::CornerPin: {
                for (const auto value : effect.quad) out.add_float(value);
            } break;
            case LayerPlan::EffectKindPlan::Twirl: {
                for (const auto value : effect.warp_center) out.add_float(value);
                out.add_float(effect.warp_angle);
                out.add_float(effect.warp_radius);
                out.add_float(effect.warp_falloff);
            } break;
            case LayerPlan::EffectKindPlan::Bulge: {
                for (const auto value : effect.warp_center) out.add_float(value);
                out.add_float(effect.warp_amount);
                out.add_float(effect.warp_radius);
            } break;
            case LayerPlan::EffectKindPlan::MeshWarp: {
                out.add_size(effect.mesh_cols);
                out.add_size(effect.mesh_rows);
                out.add_float(effect.mesh_smoothness);
                out.add_size(effect.mesh_offsets.size());
                for (const auto value : effect.mesh_offsets) out.add_float(value);
            } break;
            case LayerPlan::EffectKindPlan::Sharpen: {
                out.add_float(effect.amount); out.add_float(effect.radius); out.add_float(effect.threshold);
            } break;
            case LayerPlan::EffectKindPlan::RoughenEdges: {
                out.add_float(effect.amount); out.add_float(effect.roughen_scale);
                out.add_uint64(effect.roughen_complexity); out.add_float(effect.warp_evolution);
                out.add_float(effect.warp_speed); out.add_uint64(effect.roughen_seed);
            } break;
            case LayerPlan::EffectKindPlan::TurbulentDisplace: {
                out.add_float(effect.amount); out.add_float(effect.warp_size); out.add_float(effect.warp_evolution);
                out.add_float(effect.warp_speed); out.add_uint64(effect.warp_complexity);
            } break;
            case LayerPlan::EffectKindPlan::WaveWarp: {
                out.add_uint64(effect.wave_type); for (auto v : effect.warp_direction) out.add_float(v);
                out.add_float(effect.amount); out.add_float(effect.warp_wavelength); out.add_float(effect.warp_speed); out.add_float(effect.warp_phase);
            } break;
            case LayerPlan::EffectKindPlan::Ripple: {
                for(auto v:effect.warp_center) out.add_float(v); out.add_float(effect.warp_radius); out.add_float(effect.warp_speed);
                out.add_float(effect.warp_width); out.add_float(effect.warp_height); out.add_float(effect.warp_damping);
            } break;
            case LayerPlan::EffectKindPlan::DisplacementMap: {
                out.add_string(effect.map_layer); out.add_float(effect.map_intensity_x);
                out.add_float(effect.map_intensity_y); out.add_uint64(effect.map_channel);
                add_optional(out, effect.map_field,
                    [](auto& writer, const graphics::Field2D& field) { add_field2d(writer, field); });
            } break;
            case LayerPlan::EffectKindPlan::ChromaticAberration: {
                out.add_float(effect.chromatic_red_offset);
                out.add_float(effect.chromatic_blue_offset);
                out.add_size(static_cast<std::size_t>(effect.chromatic_offset_mode));
                out.add_float(effect.chromatic_velocity[0]);
                out.add_float(effect.chromatic_velocity[1]);
                for (const auto value : effect.chromatic_red_transform) out.add_float(value);
                for (const auto value : effect.chromatic_green_transform) out.add_float(value);
                for (const auto value : effect.chromatic_blue_transform) out.add_float(value);
            } break;
            case LayerPlan::EffectKindPlan::Halftone: {
                out.add_size(effect.halftone_size);
                out.add_float(effect.halftone_angle);
                out.add_float(effect.halftone_amount);
            } break;
            case LayerPlan::EffectKindPlan::DirectionalBlur: {
                out.add_float(effect.directional_angle);
                out.add_float(effect.directional_length);
                out.add_size(static_cast<std::size_t>(effect.directional_samples));
            } break;
            case LayerPlan::EffectKindPlan::RadialBlur: {
                out.add_float(effect.radial_center[0]);
                out.add_float(effect.radial_center[1]);
                out.add_float(effect.radial_amount);
                out.add_size(static_cast<std::size_t>(effect.radial_render_samples));
                out.add_size(static_cast<std::size_t>(effect.radial_preview_samples));
            } break;
            case LayerPlan::EffectKindPlan::Mosaic: {
                out.add_size(effect.block_size);
            } break;
            case LayerPlan::EffectKindPlan::LensDistortion: {
                out.add_float(effect.lens_curvature);
                out.add_float(effect.lens_center[0]);
                out.add_float(effect.lens_center[1]);
                out.add_float(effect.lens_scale);
            } break;
            case LayerPlan::EffectKindPlan::LuminanceResponse: {
                out.add_float(effect.response_threshold);
                out.add_float(effect.response_softness);
                out.add_float(effect.response_radius);
                for (std::size_t c = 0; c < 4; ++c) out.add_float(effect.response_tint[c]);
                out.add_float(effect.response_intensity);
                out.add_size(static_cast<std::size_t>(effect.response_blend));
            } break;
            case LayerPlan::EffectKindPlan::FoldWarp: {
                out.add_size(static_cast<std::size_t>(effect.fold_axis));
                out.add_float(effect.fold_position);
                out.add_float(effect.fold_amount);
                out.add_float(effect.fold_radius);
                out.add_float(effect.fold_falloff);
                out.add_float(effect.fold_perspective);
            } break;
            case LayerPlan::EffectKindPlan::CreaseShading: {
                out.add_size(static_cast<std::size_t>(effect.crease_axis));
                out.add_float(effect.crease_position);
                out.add_float(effect.crease_width);
                out.add_float(effect.crease_dark_side);
                out.add_float(effect.crease_light_side);
                out.add_float(effect.crease_softness);
            } break;
            case LayerPlan::EffectKindPlan::Scanlines: {
                out.add_size(static_cast<std::size_t>(effect.scanlines_axis));
                out.add_float(effect.scanlines_spacing);
                out.add_float(effect.scanlines_amount);
                out.add_float(effect.scanlines_phase);
            } break;
            case LayerPlan::EffectKindPlan::RetroTv: {
                out.add_float(effect.retro_jitter_px);
                out.add_float(effect.retro_jitter_speed);
                out.add_float(effect.retro_smear_px);
                out.add_float(effect.retro_bleed);
                out.add_float(effect.retro_noise);
                out.add_uint64(effect.retro_seed);
            } break;
            case LayerPlan::EffectKindPlan::Halation: {
                out.add_float(effect.halation_threshold);
                out.add_float(effect.halation_softness);
                out.add_float(effect.halation_radius);
                out.add_float(effect.halation_intensity);
                for (std::size_t c = 0; c < 4; ++c) out.add_float(effect.halation_tint[c]);
            } break;
            case LayerPlan::EffectKindPlan::AnamorphicStreak: {
                out.add_float(effect.streak_threshold);
                out.add_float(effect.streak_softness);
                out.add_float(effect.streak_length);
                out.add_float(effect.streak_intensity);
                for (std::size_t c = 0; c < 4; ++c) out.add_float(effect.streak_tint[c]);
            } break;
            }
            add_optional(out,effect.posterize_fps,[](auto& w,float v){w.add_float(v);});
            add_optional(out,effect.time_curve,[](auto& w,const AnimationTrackPlan& track){add_track(w,track);});
        }
    }
    if (layer.backdrop_filter) {
        out.add_bool(true);
        out.add_float(layer.backdrop_filter->blur);
        out.add_float(layer.backdrop_filter->brightness);
        out.add_float(layer.backdrop_filter->saturation);
        out.add_float(layer.backdrop_filter->refraction_strength);
        if (layer.backdrop_filter->refraction_field) {
            out.add_bool(true);
            out.add_uint64(graphics::field2d_fingerprint(
                *layer.backdrop_filter->refraction_field));
        }
        out.add_float(layer.backdrop_filter->rim_strength);
        out.add_float(layer.backdrop_filter->rim_width);
        out.add_float(layer.backdrop_filter->chromatic_dispersion);
        out.add_float(layer.backdrop_filter->grain);
        out.add_float(layer.backdrop_filter->opacity);
    }
    out.add_size(layer.effect_param_tracks.size());
    for (const auto& track : layer.effect_param_tracks) {
        out.add_string(track.effect_id);
        out.add_string(track.param);
        add_track(out, track.track);
    }
}

} // namespace chronon3d::render_plan::fingerprint_detail
