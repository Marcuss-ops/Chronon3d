#include <chronon3d/render_graph/core/render_graph_hashing.hpp>

namespace chronon3d::graph {

namespace {

// V1 freezes the historical byte digest. Production caches use V2, whose
// record cases below visit members semantically and never read struct padding.
u64 hash_effect_stack_impl(const EffectStack& effects, bool semantic_v2) {
    u64 seed = semantic_v2 ? hash_string("chronon.effect-stack.v2") : 0;
    for (const auto& e : effects) {
        if (!e.enabled) {
            continue;
        }
        seed = hash_combine(seed, hash_string(e.descriptor.id));
        seed = hash_combine(seed, hash_value(e.posterize_fps.has_value()));
        if (e.posterize_fps) seed = hash_combine(seed, hash_value(*e.posterize_fps));
        seed = hash_combine(seed,hash_value(e.time_duration_seconds));
        seed = hash_combine(seed,hash_value(e.composition_fps));
        seed = hash_combine(seed,hash_value(e.time_curve.has_value()));
        if(e.time_curve) {
            const auto& base=e.time_curve->easing_curve();
            seed=hash_combine(seed,hash_value(base.preset));
            if(base.cubic) { seed=hash_combine(seed,hash_value(base.cubic->x1)); seed=hash_combine(seed,hash_value(base.cubic->y1)); seed=hash_combine(seed,hash_value(base.cubic->x2)); seed=hash_combine(seed,hash_value(base.cubic->y2)); }
            if(const auto* curve=e.time_curve->animation_curve()) {
                seed=hash_combine(seed,hash_value(curve->default_value()));
                for(const auto& key:curve->keyframes()) {
                    seed=hash_combine(seed,hash_value(key.frame.integral()));
                    seed=hash_combine(seed,hash_value(key.value));
                    seed=hash_combine(seed,hash_value(key.easing.preset));
                    if(key.easing.cubic) { seed=hash_combine(seed,hash_value(key.easing.cubic->x1)); seed=hash_combine(seed,hash_value(key.easing.cubic->y1)); seed=hash_combine(seed,hash_value(key.easing.cubic->x2)); seed=hash_combine(seed,hash_value(key.easing.cubic->y2)); }
                }
            }
        }
        using enum effects::EffectType;
        switch (e.effect_type) {
        case Blur: {
            if (auto* p = std::get_if<BlurParams>(&e.params)) {
                const auto digest = semantic_v2 ? hash_value(p->radius) : hash_bytes(p, sizeof(*p));
                seed = hash_combine(seed, digest);
            }
            break;
        }
        case Tint: {
            if (auto* p = std::get_if<TintParams>(&e.params)) {
                auto digest = semantic_v2 ? hash_color(p->color) : hash_bytes(p, sizeof(*p));
                if (semantic_v2) digest = hash_combine(digest, hash_value(p->amount));
                seed = hash_combine(seed, digest);
            }
            break;
        }
        case Brightness: {
            if (auto* p = std::get_if<BrightnessParams>(&e.params))
                seed = hash_combine(seed, semantic_v2 ? hash_value(p->value) : hash_bytes(p, sizeof(*p)));
            break;
        }
        case Contrast: {
            if (auto* p = std::get_if<ContrastParams>(&e.params))
                seed = hash_combine(seed, semantic_v2 ? hash_value(p->value) : hash_bytes(p, sizeof(*p)));
            break;
        }
        case DropShadow: {
            if (auto* p = std::get_if<DropShadowParams>(&e.params)) {
                auto digest = semantic_v2 ? hash_vec2(p->offset) : hash_bytes(p, sizeof(*p));
                if (semantic_v2) {
                    digest = hash_combine(digest, hash_color(p->color));
                    digest = hash_combine(digest, hash_value(p->radius));
                }
                seed = hash_combine(seed, digest);
            }
            break;
        }
        case Glow: {
            if (auto* p = std::get_if<GlowParams>(&e.params))
                seed = hash_combine(seed, hash_glow_params(*p));
            break;
        }
        case Bloom: {
            if (auto* p = std::get_if<BloomParams>(&e.params)) {
                auto digest = semantic_v2 ? hash_value(p->threshold) : hash_bytes(p, sizeof(*p));
                if (semantic_v2) {
                    digest = hash_combine(digest, hash_value(p->radius));
                    digest = hash_combine(digest, hash_value(p->intensity));
                }
                seed = hash_combine(seed, digest);
            }
            break;
        }
        case Fake3DWave: {
            if (auto* p = std::get_if<Fake3DWaveParams>(&e.params)) {
                u64 digest{0};
                if (semantic_v2) {
                    digest = hash_value(p->amplitude_px);
                    digest = hash_combine(digest, hash_value(p->frequency));
                    digest = hash_combine(digest, hash_value(p->speed));
                    digest = hash_combine(digest, hash_value(p->depth_px));
                    digest = hash_combine(digest, hash_value(p->phase));
                    digest = hash_combine(digest, hash_value(p->slices));
                    digest = hash_combine(digest, hash_value(p->axis));
                    digest = hash_combine(digest, hash_value(p->perspective));
                    digest = hash_combine(digest, hash_value(p->highlight));
                    digest = hash_combine(digest, hash_value(p->side_darkening));
                    digest = hash_combine(digest, hash_value(p->shadow_enabled));
                    digest = hash_combine(digest, hash_color(p->shadow_color));
                    digest = hash_combine(digest, hash_vec2(p->shadow_offset));
                    digest = hash_combine(digest, hash_value(p->shadow_blur));
                    digest = hash_combine(digest, hash_value(p->expand_bounds));
                } else {
                    digest = hash_bytes(p, sizeof(*p));
                }
                seed = hash_combine(seed, digest);
            }
            break;
        }
        case Saturation: {
            if (auto* p = std::get_if<SaturationParams>(&e.params))
                seed = hash_combine(seed, semantic_v2 ? hash_value(p->value) : hash_bytes(p, sizeof(*p)));
            break;
        }
        case HueRotate: {
            if (auto* p = std::get_if<HueRotateParams>(&e.params))
                seed = hash_combine(seed, semantic_v2 ? hash_value(p->degrees) : hash_bytes(p, sizeof(*p)));
            break;
        }
        case Invert: {
            if (auto* p = std::get_if<InvertParams>(&e.params))
                seed = hash_combine(seed, semantic_v2 ? hash_value(p->amount) : hash_bytes(p, sizeof(*p)));
            break;
        }
        case Vignette: {
            if (auto* p = std::get_if<VignetteParams>(&e.params)) {
                auto digest = semantic_v2 ? hash_value(p->radius) : hash_bytes(p, sizeof(*p));
                if (semantic_v2) {
                    digest = hash_combine(digest, hash_value(p->softness));
                    digest = hash_combine(digest, hash_value(p->amount));
                    digest = hash_combine(digest, hash_color(p->color));
                }
                seed = hash_combine(seed, digest);
            }
            break;
        }
        case Noise: {
            if (auto* p = std::get_if<NoiseParams>(&e.params)) {
                seed = hash_combine(seed, hash_value(p->amount));
                seed = hash_combine(seed, hash_value(p->seed));
                seed = hash_combine(seed, hash_value(p->animated));
                seed = hash_combine(seed, hash_value(p->color_mode));
                seed = hash_combine(seed, hash_value(p->size));
                seed = hash_combine(seed, hash_value(p->shadow_response));
                seed = hash_combine(seed, hash_value(p->midtone_response));
                seed = hash_combine(seed, hash_value(p->highlight_response));
            }
            break;
        }
        case FractalNoise: {
            if (auto* p = std::get_if<FractalNoiseParams>(&e.params)) {
                seed = hash_combine(seed, hash_value(p->seed));
                seed = hash_combine(seed, hash_value(p->octaves));
                seed = hash_combine(seed, hash_value(p->frequency));
                seed = hash_combine(seed, hash_value(p->amplitude));
                seed = hash_combine(seed, hash_value(p->evolution));
                seed = hash_combine(seed, hash_value(p->contrast));
                seed = hash_combine(seed, hash_value(p->brightness));
                seed = hash_combine(seed, hash_value(p->color_mode));
                seed = hash_combine(seed, hash_value(p->field.has_value()));
                if (p->field)
                    seed = hash_combine(seed, hash_value(semantic_v2
                        ? graphics::field2d_fingerprint_v2(*p->field)
                        : graphics::field2d_fingerprint(*p->field)));
            }
            break;
        }
        case Lut3D: {
            if (auto* p = std::get_if<Lut3DParams>(&e.params)) {
                seed = hash_combine(seed, hash_value(p->dimension));
                seed = hash_combine(seed, hash_value(p->amount));
                seed = hash_combine(seed, hash_bytes(p->domain_min.data(), sizeof(p->domain_min)));
                seed = hash_combine(seed, hash_bytes(p->domain_max.data(), sizeof(p->domain_max)));
                // The LUT buffer is immutable and shared. Its plan asset's
                // content digest is part of the render-plan job fingerprint;
                // pointer identity is the O(1) runtime invalidation token.
                seed = hash_combine(seed, hash_value(p->values.get()));
            }
            break;
        }
        case Echo: {
            if (auto* p = std::get_if<EchoParams>(&e.params)) {
                seed = hash_combine(seed, hash_value(p->echoes));
                seed = hash_combine(seed, hash_value(p->decay));
                seed = hash_combine(seed, hash_value(p->time_offset_frames));
            }
            break;
        }
        case Keying: {
            if (auto* p = std::get_if<KeyingParams>(&e.params)) {
                seed = hash_combine(seed, hash_value(p->mode));
                seed = hash_combine(seed, hash_value(p->key_color.r));
                seed = hash_combine(seed, hash_value(p->key_color.g));
                seed = hash_combine(seed, hash_value(p->key_color.b));
                seed = hash_combine(seed, hash_value(p->threshold));
                seed = hash_combine(seed, hash_value(p->softness));
                seed = hash_combine(seed, hash_value(p->spill));
                seed = hash_combine(seed, hash_value(p->clip_black));
                seed = hash_combine(seed, hash_value(p->clip_white));
                seed = hash_combine(seed, hash_value(p->choke));
                seed = hash_combine(seed, hash_value(p->garbage_x));
                seed = hash_combine(seed, hash_value(p->garbage_y));
                seed = hash_combine(seed, hash_value(p->garbage_w));
                seed = hash_combine(seed, hash_value(p->garbage_h));
                seed = hash_combine(seed, hash_value(p->garbage_feather));
            }
            break;
        }
        case Threshold: {
            if (auto* p = std::get_if<ThresholdParams>(&e.params)) {
                seed = hash_combine(seed, hash_value(p->threshold));
                seed = hash_combine(seed, hash_value(p->softness));
            }
            break;
        }
        case CornerPin: {
            if (auto* p = std::get_if<CornerPinParams>(&e.params)) {
                for (const Vec2 corner : {p->top_left, p->top_right,
                                          p->bottom_right, p->bottom_left}) {
                    seed = hash_combine(seed, hash_value(corner.x));
                    seed = hash_combine(seed, hash_value(corner.y));
                }
            }
            break;
        }
        case Twirl: {
            if (auto* p = std::get_if<TwirlParams>(&e.params)) {
                seed = hash_combine(seed, hash_value(p->center.x));
                seed = hash_combine(seed, hash_value(p->center.y));
                seed = hash_combine(seed, hash_value(p->angle_degrees));
                seed = hash_combine(seed, hash_value(p->radius));
                seed = hash_combine(seed, hash_value(p->falloff));
            }
            break;
        }
        case Bulge: {
            if (auto* p = std::get_if<BulgeParams>(&e.params)) {
                seed = hash_combine(seed, hash_value(p->center.x));
                seed = hash_combine(seed, hash_value(p->center.y));
                seed = hash_combine(seed, hash_value(p->amount));
                seed = hash_combine(seed, hash_value(p->radius));
            }
            break;
        }
        case MeshWarp: {
            if (auto* p = std::get_if<MeshWarpParams>(&e.params)) {
                seed = hash_combine(seed, hash_value(p->cols));
                seed = hash_combine(seed, hash_value(p->rows));
                seed = hash_combine(seed, hash_value(p->smoothness));
                for (const Vec2 offset : p->offsets) {
                    seed = hash_combine(seed, hash_value(offset.x));
                    seed = hash_combine(seed, hash_value(offset.y));
                }
            }
            break;
        }
        case Sharpen: {
            if (auto* p=std::get_if<SharpenParams>(&e.params)) {
                seed=hash_combine(seed,hash_value(p->amount)); seed=hash_combine(seed,hash_value(p->radius));
                seed=hash_combine(seed,hash_value(p->threshold));
            } break;
        }
        case RoughenEdges: {
            if (auto* p = std::get_if<RoughenEdgesParams>(&e.params)) {
                seed = hash_combine(seed, hash_value(p->amount));
                seed = hash_combine(seed, hash_value(p->scale));
                seed = hash_combine(seed, hash_value(p->complexity));
                seed = hash_combine(seed, hash_value(p->evolution));
                seed = hash_combine(seed, hash_value(p->seed));
            }
            break;
        }
        case TurbulentDisplace: {
            if(auto* p=std::get_if<TurbulentDisplaceParams>(&e.params)) {
                seed=hash_combine(seed,hash_value(p->amount)); seed=hash_combine(seed,hash_value(p->size));
                seed=hash_combine(seed,hash_value(p->evolution)); seed=hash_combine(seed,hash_value(p->evolution_speed));
                seed=hash_combine(seed,hash_value(p->complexity)); seed=hash_combine(seed,hash_value(p->edge_mode));
            } break;
        }
        case WaveWarp: {
            if(auto* p=std::get_if<WaveWarpParams>(&e.params)) {
                seed=hash_combine(seed,hash_value(p->type)); seed=hash_combine(seed,hash_value(p->direction.x));
                seed=hash_combine(seed,hash_value(p->direction.y)); seed=hash_combine(seed,hash_value(p->amplitude));
                seed=hash_combine(seed,hash_value(p->wavelength)); seed=hash_combine(seed,hash_value(p->speed));
                seed=hash_combine(seed,hash_value(p->phase)); seed=hash_combine(seed,hash_value(p->edge_mode));
            } break;
        }
        case Ripple: {
            if(auto* p=std::get_if<RippleParams>(&e.params)) {
                seed=hash_combine(seed,hash_value(p->center.x)); seed=hash_combine(seed,hash_value(p->center.y));
                seed=hash_combine(seed,hash_value(p->radius)); seed=hash_combine(seed,hash_value(p->wave_speed));
                seed=hash_combine(seed,hash_value(p->width)); seed=hash_combine(seed,hash_value(p->wave_height));
                seed=hash_combine(seed,hash_value(p->damping)); seed=hash_combine(seed,hash_value(p->edge_mode));
            } break;
        }
        case DisplacementMap: {
            if(auto* p=std::get_if<DisplacementMapParams>(&e.params)) {
                seed=hash_combine(seed,hash_string(p->map_layer));
                seed=hash_combine(seed,hash_value(p->intensity_x)); seed=hash_combine(seed,hash_value(p->intensity_y));
                seed=hash_combine(seed,hash_value(p->channel)); seed=hash_combine(seed,hash_value(p->edge_mode));
                seed=hash_combine(seed,hash_value(p->field.has_value()));
                if (p->field)
                    seed=hash_combine(seed,hash_value(semantic_v2
                        ? graphics::field2d_fingerprint_v2(*p->field)
                        : graphics::field2d_fingerprint(*p->field)));
            } break;
        }
        case Mosaic: {
            if (auto* p = std::get_if<MosaicParams>(&e.params))
                seed = hash_combine(seed, hash_value(p->block_size));
            break;
        }
        case ChromaticAberration: {
            if (auto* p = std::get_if<ChromaticAberrationParams>(&e.params)) {
                seed = hash_combine(seed, hash_value(p->red_offset));
                seed = hash_combine(seed, hash_value(p->blue_offset));
                seed = hash_combine(seed, hash_value(static_cast<int>(p->offset_mode)));
                seed = hash_combine(seed, hash_vec2(p->velocity));
                seed = hash_combine(seed, hash_vec2(p->center));
                const auto add_channel_transform = [&seed](const RgbChannelTransform& channel) {
                    seed = hash_combine(seed, hash_vec2(channel.translation));
                    seed = hash_combine(seed, hash_vec2(channel.scale));
                    seed = hash_combine(seed, hash_value(channel.rotation_degrees));
                    seed = hash_combine(seed, hash_value(channel.opacity));
                };
                add_channel_transform(p->red);
                add_channel_transform(p->green);
                add_channel_transform(p->blue);
            }
            break;
        }
        case Halftone: {
            if (auto* p = std::get_if<HalftoneParams>(&e.params)) {
                seed = hash_combine(seed, hash_value(p->size));
                seed = hash_combine(seed, hash_value(p->angle_degrees));
                seed = hash_combine(seed, hash_value(p->amount));
            }
            break;
        }
        case LensDistortion: {
            if (auto* p = std::get_if<LensDistortionParams>(&e.params)) {
                seed = hash_combine(seed, hash_value(p->curvature));
                seed = hash_combine(seed, hash_vec2(p->center));
                seed = hash_combine(seed, hash_value(p->scale));
                seed = hash_combine(seed, hash_value(static_cast<int>(p->edge_mode)));
            }
            break;
        }
        case LuminanceResponse: {
            if (auto* p = std::get_if<LuminanceResponseParams>(&e.params)) {
                seed = hash_combine(seed, hash_value(p->threshold));
                seed = hash_combine(seed, hash_value(p->softness));
                seed = hash_combine(seed, hash_value(p->radius));
                seed = hash_combine(seed, hash_color(p->tint));
                seed = hash_combine(seed, hash_value(p->intensity));
                seed = hash_combine(seed, hash_value(static_cast<int>(p->blend)));
            }
            break;
        }
        case FoldWarp: {
            if (auto* p = std::get_if<FoldWarpParams>(&e.params)) {
                seed = hash_combine(seed, hash_value(static_cast<int>(p->axis)));
                seed = hash_combine(seed, hash_value(p->position));
                seed = hash_combine(seed, hash_value(p->amount));
                seed = hash_combine(seed, hash_value(p->radius));
                seed = hash_combine(seed, hash_value(p->falloff));
                seed = hash_combine(seed, hash_value(p->perspective));
                seed = hash_combine(seed, hash_value(static_cast<int>(p->edge_mode)));
            }
            break;
        }
        case CreaseShading: {
            if (auto* p = std::get_if<CreaseShadingParams>(&e.params)) {
                seed = hash_combine(seed, hash_value(static_cast<int>(p->axis)));
                seed = hash_combine(seed, hash_value(p->position));
                seed = hash_combine(seed, hash_value(p->width));
                seed = hash_combine(seed, hash_value(p->dark_side));
                seed = hash_combine(seed, hash_value(p->light_side));
                seed = hash_combine(seed, hash_value(p->softness));
            }
            break;
        }
        case Scanlines: {
            if (auto* p = std::get_if<ScanlinesParams>(&e.params)) {
                seed = hash_combine(seed, hash_value(static_cast<int>(p->axis)));
                seed = hash_combine(seed, hash_value(p->spacing_px));
                seed = hash_combine(seed, hash_value(p->amount));
                seed = hash_combine(seed, hash_value(p->phase));
            }
            break;
        }
        case RetroTv: {
            if (auto* p = std::get_if<RetroTvParams>(&e.params)) {
                seed = hash_combine(seed, hash_value(p->jitter_px));
                seed = hash_combine(seed, hash_value(p->jitter_speed));
                seed = hash_combine(seed, hash_value(p->smear_px));
                seed = hash_combine(seed, hash_value(p->bleed_strength));
                seed = hash_combine(seed, hash_value(p->luma_noise));
                seed = hash_combine(seed, hash_bytes(&p->seed, sizeof(p->seed)));
            }
            break;
        }
        case Halation: {
            if (auto* p = std::get_if<HalationParams>(&e.params)) {
                seed = hash_combine(seed, hash_value(p->threshold));
                seed = hash_combine(seed, hash_value(p->softness));
                seed = hash_combine(seed, hash_value(p->radius_px));
                seed = hash_combine(seed, hash_value(p->intensity));
                seed = hash_combine(seed, hash_color(p->tint));
            }
            break;
        }
        case AnamorphicStreak: {
            if (auto* p = std::get_if<AnamorphicStreakParams>(&e.params)) {
                seed = hash_combine(seed, hash_value(p->threshold));
                seed = hash_combine(seed, hash_value(p->softness));
                seed = hash_combine(seed, hash_value(p->streak_length_px));
                seed = hash_combine(seed, hash_value(p->intensity));
                seed = hash_combine(seed, hash_color(p->tint));
            }
            break;
        }
        case ColorMatrix: {
            if (auto* p = std::get_if<ColorMatrixParams>(&e.params))
                for (const float value : p->values)
                    seed = hash_combine(seed, hash_value(value));
            break;
        }
        case Posterize: {
            if (auto* p = std::get_if<PosterizeParams>(&e.params))
                seed = hash_combine(seed, hash_value(p->levels));
            break;
        }
        case LinearWipe: {
            if (auto* p = std::get_if<LinearWipeParams>(&e.params)) {
                seed = hash_combine(seed, hash_value(p->progress));
                seed = hash_combine(seed, hash_value(p->angle_degrees));
                seed = hash_combine(seed, hash_value(p->feather));
            }
            break;
        }
        case MinMax: {
            if (auto* p = std::get_if<MinMaxParams>(&e.params)) {
                seed = hash_combine(seed, hash_value(p->radius));
                seed = hash_combine(seed, hash_value(p->maximum));
            }
            break;
        }
        case StretchWarp: {
            if (auto* p = std::get_if<StretchWarpParams>(&e.params)) {
                seed = hash_combine(seed, hash_value(p->position));
                seed = hash_combine(seed, hash_value(p->stretch));
                seed = hash_combine(seed, hash_value(p->angle_degrees));
            }
            break;
        }
        case LensFlare: {
            if (auto* p = std::get_if<LensFlareParams>(&e.params)) {
                seed = hash_combine(seed, hash_vec2(p->center));
                for (float value : {p->threshold, p->intensity, p->radius, p->halo, p->ghost, p->streak})
                    seed = hash_combine(seed, hash_value(value));
            }
            break;
        }
        case ColorMap: {
            if (auto* p = std::get_if<ColorMapParams>(&e.params)) {
                for (const float value : p->stops)
                    seed = hash_combine(seed, hash_value(value));
                seed = hash_combine(seed, hash_value(p->amount));
            }
            break;
        }
        case Unknown:
            break;
        }
        for (const auto& track : e.parameter_tracks) {
            seed = hash_combine(seed, hash_string(track.param));
            seed = hash_combine(seed, hash_value(track.keyframes.size()));
            for (const auto& key : track.keyframes) {
                seed = hash_combine(seed, hash_value(key.frame));
                seed = hash_combine(seed, hash_value(key.value));
                seed = hash_combine(seed, hash_value(key.hold));
            }
        }
    }
    return seed;
}

} // namespace

[[nodiscard]] u64 hash_effect_stack(const EffectStack& effects) {
    return hash_effect_stack_impl(effects, false);
}

[[nodiscard]] u64 hash_effect_stack_v2(const EffectStack& effects) {
    return hash_effect_stack_impl(effects, true);
}


} // namespace chronon3d::graph
