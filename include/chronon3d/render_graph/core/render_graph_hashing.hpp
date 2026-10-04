#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <chronon3d/scene/model/shape/fill.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/scene/model/layer/mask.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/geometry/mesh.hpp>
#include <chronon3d/scene/model/core/transition.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/text/text_run_hash.hpp>
#include <chronon3d/core/hash/hash_builder.hpp>
#include <chronon3d/graphics/gradient.hpp>
#include <xxhash.h>

#include <bit>
#include <type_traits>
#include <string_view>
// <variant> is transitively included via effect_stack.hpp → effect_params.hpp

namespace chronon3d::graph {

// Cat-3 — the digest primitive (mixing formula + XXH64 byte/string
// digests) has ONE seat: core/hash/hash_builder.hpp.  These graph-domain
// names delegate to it so every cache-key digest shares one implementation;
// the formula is unchanged, so digest values are bit-identical to the
// historical inline versions.
[[nodiscard]] inline u64 hash_combine(u64 seed, u64 value) {
    return core::hash::hash_combine(seed, value);
}

[[nodiscard]] inline u64 hash_bytes(const void* data, usize size) {
    return core::hash::hash_bytes(data, size);
}

[[nodiscard]] inline u64 hash_string(std::string_view value) {
    return core::hash::hash_string(value);
}

template <typename T>
[[nodiscard]] inline u64 hash_value(const T& value) {
    return hash_bytes(&value, sizeof(value));
}

[[nodiscard]] inline u64 hash_vec2(const Vec2& value) {
    return hash_bytes(&value, sizeof(value));
}

[[nodiscard]] inline u64 hash_vec3(const Vec3& value) {
    return hash_bytes(&value, sizeof(value));
}

[[nodiscard]] inline u64 hash_color(const Color& color) {
    return hash_bytes(&color, sizeof(color));
}

[[nodiscard]] inline u64 hash_transform(const Transform& transform) {
    u64 h = hash_vec3(transform.position);
    h = hash_combine(h, hash_vec3(transform.anchor));
    h = hash_combine(h, hash_vec3(transform.scale));
    h = hash_combine(h, hash_bytes(&transform.rotation, sizeof(transform.rotation)));
    h = hash_combine(h, hash_bytes(&transform.opacity, sizeof(transform.opacity)));
    return h;
}

[[nodiscard]] inline u64 hash_surface_policy(SurfacePolicy policy) {
    return hash_bytes(&policy, sizeof(policy));
}

[[nodiscard]] inline u64 hash_transform_policy(TransformPolicy policy) {
    return hash_bytes(&policy, sizeof(policy));
}

[[nodiscard]] inline u64 hash_video_source(const video::VideoSource& source) {
    u64 seed = hash_string(source.path);
    seed = hash_combine(seed, hash_bytes(&source.source_start, sizeof(source.source_start)));
    seed = hash_combine(seed, hash_bytes(&source.duration, sizeof(source.duration)));
    seed = hash_combine(seed, hash_bytes(&source.source_fps, sizeof(source.source_fps)));
    seed = hash_combine(seed, hash_bytes(&source.speed, sizeof(source.speed)));
    const u64 loop = static_cast<u64>(source.loop_mode);
    seed = hash_combine(seed, hash_bytes(&loop, sizeof(loop)));
    seed = hash_combine(seed, hash_vec2(source.size));
    seed = hash_combine(seed, hash_string(source.ocio_config_path));
    seed = hash_combine(seed, hash_string(source.ocio_input_space));
    seed = hash_combine(seed, hash_string(source.ocio_working_space));
    return seed;
}

namespace detail {

// Digest sink for graphics::write_gradient_identity — the Cat-3 single seat
// for the gradient identity field selection/order (graphics/gradient.hpp).
// Only the encoding lives here; the digest is bit-identical to the
// historical inline hash_gradient: the first field seeds the digest
// directly, every later field folds in with hash_combine.
struct GradientIdentityHashSink {
    u64 seed{0};

    template <typename T>
    void operator()(const T& value) {
        if constexpr (std::is_enum_v<T>) {
            fold(hash_value(static_cast<int>(value)));
        } else if constexpr (std::is_same_v<T, Color>) {
            fold(hash_color(value));
        } else if constexpr (std::is_same_v<T, Vec2>) {
            fold(hash_vec2(value));
        } else {
            fold(hash_value(value));
        }
    }

    void count(usize size) { fold(hash_value(size)); }

private:
    void fold(u64 value) {
        seed = m_first ? value : hash_combine(seed, value);
        m_first = false;
    }

    bool m_first{true};
};

} // namespace detail

[[nodiscard]] inline u64 hash_gradient(const graphics::GradientDefinition& gradient) {
    detail::GradientIdentityHashSink sink;
    graphics::write_gradient_identity(gradient, sink);
    return sink.seed;
}

[[nodiscard]] inline u64 hash_fill(const Fill& f) {
    u64 seed = hash_bytes(&f.type, sizeof(f.type));
    seed = hash_combine(seed, hash_value(f.enabled));
    seed = hash_combine(seed, hash_color(f.solid));
    seed = hash_combine(seed, hash_gradient(f.gradient));
    seed = hash_combine(seed, hash_value(f.field.has_value()));
    if (f.field)
        seed = hash_combine(seed, hash_value(graphics::field2d_fingerprint(*f.field)));
    if (f.gradient_mesh) {
        seed = hash_combine(seed, hash_value(true));
        seed = hash_combine(seed, hash_value(f.gradient_mesh->nodes.size()));
        for (const auto& node : f.gradient_mesh->nodes) {
            seed = hash_combine(seed, hash_vec2(node.position));
            seed = hash_combine(seed, hash_color(node.color));
            seed = hash_combine(seed, hash_value(node.weight));
        }
    }
    return seed;
}

[[nodiscard]] inline u64 hash_blur_params(const BlurParams& p) {
    return hash_value(p.radius);
}

[[nodiscard]] inline u64 hash_tint_params(const TintParams& p) {
    u64 seed = hash_color(p.color);
    return hash_combine(seed, hash_value(p.amount));
}

[[nodiscard]] inline u64 hash_brightness_params(const BrightnessParams& p) {
    return hash_value(p.value);
}

[[nodiscard]] inline u64 hash_contrast_params(const ContrastParams& p) {
    return hash_value(p.value);
}

[[nodiscard]] inline u64 hash_drop_shadow_params(const DropShadowParams& p) {
    u64 seed = hash_vec2(p.offset);
    seed = hash_combine(seed, hash_color(p.color));
    return hash_combine(seed, hash_value(p.radius));
}

[[nodiscard]] inline u64 hash_glow_params(const GlowParams& p) {
    u64 seed = hash_value(p.radius);
    seed = hash_combine(seed, hash_value(p.intensity));
    seed = hash_combine(seed, hash_color(p.color));
    return seed;
}

[[nodiscard]] inline u64 hash_bloom_params(const BloomParams& p) {
    u64 seed = hash_value(p.threshold);
    seed = hash_combine(seed, hash_value(p.radius));
    return hash_combine(seed, hash_value(p.intensity));
}

[[nodiscard]] inline u64 hash_effect_stack(const EffectStack& effects) {
    static thread_local int depth = 0;
    if (depth > 50) return 0; // Prevent stack overflow
    depth++;
    struct DepthGuard { ~DepthGuard() { depth--; } } guard;
    u64 seed = 0;
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
            if (auto* p = std::get_if<BlurParams>(&e.params))
                seed = hash_combine(seed, hash_bytes(p, sizeof(*p)));
            break;
        }
        case Tint: {
            if (auto* p = std::get_if<TintParams>(&e.params))
                seed = hash_combine(seed, hash_bytes(p, sizeof(*p)));
            break;
        }
        case Brightness: {
            if (auto* p = std::get_if<BrightnessParams>(&e.params))
                seed = hash_combine(seed, hash_bytes(p, sizeof(*p)));
            break;
        }
        case Contrast: {
            if (auto* p = std::get_if<ContrastParams>(&e.params))
                seed = hash_combine(seed, hash_bytes(p, sizeof(*p)));
            break;
        }
        case DropShadow: {
            if (auto* p = std::get_if<DropShadowParams>(&e.params))
                seed = hash_combine(seed, hash_bytes(p, sizeof(*p)));
            break;
        }
        case Glow: {
            if (auto* p = std::get_if<GlowParams>(&e.params))
                seed = hash_combine(seed, hash_glow_params(*p));
            break;
        }
        case Bloom: {
            if (auto* p = std::get_if<BloomParams>(&e.params))
                seed = hash_combine(seed, hash_bytes(p, sizeof(*p)));
            break;
        }
        case Fake3DWave: {
            if (auto* p = std::get_if<Fake3DWaveParams>(&e.params))
                seed = hash_combine(seed, hash_bytes(p, sizeof(*p)));
            break;
        }
        case Saturation: {
            if (auto* p = std::get_if<SaturationParams>(&e.params))
                seed = hash_combine(seed, hash_bytes(p, sizeof(*p)));
            break;
        }
        case HueRotate: {
            if (auto* p = std::get_if<HueRotateParams>(&e.params))
                seed = hash_combine(seed, hash_bytes(p, sizeof(*p)));
            break;
        }
        case Invert: {
            if (auto* p = std::get_if<InvertParams>(&e.params))
                seed = hash_combine(seed, hash_bytes(p, sizeof(*p)));
            break;
        }
        case Vignette: {
            if (auto* p = std::get_if<VignetteParams>(&e.params))
                seed = hash_combine(seed, hash_bytes(p, sizeof(*p)));
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
                    seed = hash_combine(seed, hash_value(graphics::field2d_fingerprint(*p->field)));
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
                    seed=hash_combine(seed,hash_value(graphics::field2d_fingerprint(*p->field)));
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
        case Unknown:
            break;
        }
    }
    return seed;
}

[[nodiscard]] inline u64 hash_mask(const Mask& m) {
    if (!m.enabled()) {
        return 0;
    }
    u64 seed = hash_value(static_cast<int>(m.type));
    seed = hash_combine(seed, hash_value(static_cast<int>(m.initial_mode)));
    seed = hash_combine(seed, hash_vec3(m.pos));
    seed = hash_combine(seed, hash_vec2(m.size));
    seed = hash_combine(seed, hash_value(m.radius));
    seed = hash_combine(seed, hash_value(m.feather));
    seed = hash_combine(seed, hash_value(m.expansion));
    seed = hash_combine(seed, hash_value(m.opacity));
    seed = hash_combine(seed, hash_value(m.inverted));
    seed = hash_combine(seed, hash_string(m.text_source));
    seed = hash_combine(seed, hash_value(m.field.has_value()));
    if (m.field) seed = hash_combine(seed, hash_value(graphics::field2d_fingerprint(*m.field)));
    seed = hash_combine(seed, hash_value(m.path.size()));
    for (const auto& command : m.path) {
        seed = hash_combine(seed, hash_value(static_cast<int>(command.type)));
        seed = hash_combine(seed, hash_vec2(command.p0));
        seed = hash_combine(seed, hash_vec2(command.p1));
        seed = hash_combine(seed, hash_vec2(command.p2));
    }
    seed = hash_combine(seed, hash_value(m.operations.size()));
    for (const auto& operation : m.operations) {
        seed = hash_combine(seed, hash_value(static_cast<int>(operation.mode)));
        seed = hash_combine(seed, hash_value(static_cast<int>(operation.type)));
        seed = hash_combine(seed, hash_vec3(operation.pos));
        seed = hash_combine(seed, hash_vec2(operation.size));
        seed = hash_combine(seed, hash_value(operation.radius));
        seed = hash_combine(seed, hash_value(operation.feather));
        seed = hash_combine(seed, hash_value(operation.expansion));
        seed = hash_combine(seed, hash_value(operation.opacity));
        seed = hash_combine(seed, hash_value(operation.inverted));
        seed = hash_combine(seed, hash_string(operation.text_source));
        seed = hash_combine(seed, hash_value(operation.field.has_value()));
        if (operation.field)
            seed = hash_combine(seed, hash_value(graphics::field2d_fingerprint(*operation.field)));
        seed = hash_combine(seed, hash_value(operation.path.size()));
        for (const auto& command : operation.path) {
            seed = hash_combine(seed, hash_value(static_cast<int>(command.type)));
            seed = hash_combine(seed, hash_vec2(command.p0));
            seed = hash_combine(seed, hash_vec2(command.p1));
            seed = hash_combine(seed, hash_vec2(command.p2));
        }
    }
    return seed;
}

// Forward declaration so hash_shape can delegate to it
[[nodiscard]] inline u64 hash_text_style_full(
    const TextShape& t, float effective_size, int padding,
    const Mat4* transform);

[[nodiscard]] inline u64 hash_shape(const Shape& s) {
    u64 seed = hash_value(static_cast<int>(s.type()));
    switch (s.type()) {
        case ShapeType::Rect:
            seed = hash_combine(seed, hash_vec2(s.rect().size));
            seed = hash_combine(seed, hash_value(s.rect().stroke.enabled));
            seed = hash_combine(seed, hash_color(s.rect().stroke.color));
            seed = hash_combine(seed, hash_value(s.rect().stroke.width));
            seed = hash_combine(seed, hash_bytes(&s.rect().stroke.alignment, sizeof(StrokeAlignment)));
            seed = hash_combine(seed, hash_value(s.rect().stroke.gradient.has_value()));
            if (s.rect().stroke.gradient)
                seed = hash_combine(seed, hash_gradient(*s.rect().stroke.gradient));
            return seed;
        case ShapeType::RoundedRect:
            seed = hash_combine(seed, hash_vec2(s.rounded_rect().size));
            seed = hash_combine(seed, hash_bytes(&s.rounded_rect().radius, sizeof(f32)));
            seed = hash_combine(seed, hash_value(s.rounded_rect().corner_radius.has_value()));
            if (s.rounded_rect().corner_radius)
                seed = hash_combine(seed, hash_bytes(s.rounded_rect().corner_radius->data(), sizeof(f32) * 4));
            seed = hash_combine(seed, hash_value(s.rounded_rect().stroke.enabled));
            seed = hash_combine(seed, hash_color(s.rounded_rect().stroke.color));
            seed = hash_combine(seed, hash_value(s.rounded_rect().stroke.width));
            seed = hash_combine(seed, hash_bytes(&s.rounded_rect().stroke.alignment, sizeof(StrokeAlignment)));
            seed = hash_combine(seed, hash_value(s.rounded_rect().stroke.gradient.has_value()));
            if (s.rounded_rect().stroke.gradient)
                seed = hash_combine(seed, hash_gradient(*s.rounded_rect().stroke.gradient));
            return seed;
        case ShapeType::Circle:
            seed = hash_combine(seed, hash_bytes(&s.circle().radius, sizeof(f32)));
            seed = hash_combine(seed, hash_value(s.circle().stroke.enabled));
            seed = hash_combine(seed, hash_color(s.circle().stroke.color));
            seed = hash_combine(seed, hash_value(s.circle().stroke.width));
            seed = hash_combine(seed, hash_bytes(&s.circle().stroke.alignment, sizeof(StrokeAlignment)));
            seed = hash_combine(seed, hash_value(s.circle().stroke.gradient.has_value()));
            if (s.circle().stroke.gradient)
                seed = hash_combine(seed, hash_gradient(*s.circle().stroke.gradient));
            return seed;
        case ShapeType::Line:
            seed = hash_combine(seed, hash_vec3(s.line().to));
            seed = hash_combine(seed, hash_bytes(&s.line().thickness, sizeof(f32)));
            seed = hash_combine(seed, hash_bytes(&s.line().stroke.trim_start, sizeof(f32)));
            seed = hash_combine(seed, hash_bytes(&s.line().stroke.trim_end, sizeof(f32)));
            seed = hash_combine(seed, hash_value(s.line().stroke.enabled));
            seed = hash_combine(seed, hash_color(s.line().stroke.color));
            seed = hash_combine(seed, hash_value(s.line().stroke.width));
            return hash_combine(seed, hash_bytes(&s.line().stroke.alignment, sizeof(StrokeAlignment)));
        case ShapeType::Path:
            seed = hash_combine(seed, hash_value(s.path().commands.size()));
            for (const auto& cmd : s.path().commands) {
                seed = hash_combine(seed, hash_value(static_cast<int>(cmd.type)));
                seed = hash_combine(seed, hash_vec2(cmd.p0));
                seed = hash_combine(seed, hash_vec2(cmd.p1));
                seed = hash_combine(seed, hash_vec2(cmd.p2));
            }
            seed = hash_combine(seed, hash_bytes(&s.path().stroke.width, sizeof(f32)));
            seed = hash_combine(seed, hash_value(static_cast<int>(s.path().stroke.cap)));
            seed = hash_combine(seed, hash_value(static_cast<int>(s.path().stroke.join)));
            seed = hash_combine(seed, hash_bytes(&s.path().stroke.trim_start, sizeof(f32)));
            seed = hash_combine(seed, hash_bytes(&s.path().stroke.trim_end, sizeof(f32)));
            seed = hash_combine(seed, hash_bytes(&s.path().stroke.taper_start, sizeof(f32)));
            seed = hash_combine(seed, hash_bytes(&s.path().stroke.taper_end, sizeof(f32)));
            seed = hash_combine(seed, hash_value(s.path().closed));
            seed = hash_combine(seed, hash_fill(s.path().fill));
            seed = hash_combine(seed, hash_value(s.path().stroke.enabled));
            seed = hash_combine(seed, hash_color(s.path().stroke.color));
            seed = hash_combine(seed, hash_value(s.path().stroke.dash_array.size()));
            for (const auto dash : s.path().stroke.dash_array)
                seed = hash_combine(seed, hash_value(dash));
            seed = hash_combine(seed, hash_value(s.path().stroke.dash_offset));
            seed = hash_combine(seed, hash_value(s.path().stroke.gradient.has_value()));
            if (s.path().stroke.gradient)
                seed = hash_combine(seed, hash_gradient(*s.path().stroke.gradient));
            seed = hash_combine(seed, hash_value(s.path().stroke.brush.has_value()));
            if (s.path().stroke.brush)
                seed = hash_combine(seed, hash_value(s.path().stroke.brush->fingerprint()));
            seed = hash_combine(seed, hash_value(s.path().stroke.trim_start));
            seed = hash_combine(seed, hash_value(s.path().stroke.trim_end));
            seed = hash_combine(seed, hash_bytes(&s.path().stroke.cap, sizeof(s.path().stroke.cap)));
            seed = hash_combine(seed, hash_bytes(&s.path().stroke.join, sizeof(s.path().stroke.join)));
            return seed;
        case ShapeType::Image:
            seed = hash_combine(seed, hash_bytes(s.image().path.data(), s.image().path.size()));
            seed = hash_combine(seed, hash_vec2(s.image().size));
            seed = hash_combine(seed, hash_bytes(&s.image().opacity, sizeof(f32)));
            return hash_combine(seed, hash_color(s.image().tint));
        case ShapeType::TiledImage:
            seed = hash_combine(seed, hash_bytes(s.tiled_image().image.path.data(), s.tiled_image().image.path.size()));
            seed = hash_combine(seed, hash_vec2(s.tiled_image().image.size));
            seed = hash_combine(seed, hash_bytes(&s.tiled_image().image.opacity, sizeof(f32)));
            return hash_combine(seed, hash_color(s.tiled_image().image.tint));
        case ShapeType::GridBackground:
            seed = hash_combine(seed, hash_vec2(s.grid_background().size));
            seed = hash_combine(seed, hash_vec2(s.grid_background().offset));
            seed = hash_combine(seed, hash_color(s.grid_background().bg_color));
            seed = hash_combine(seed, hash_color(s.grid_background().grid_color));
            seed = hash_combine(seed, hash_bytes(&s.grid_background().spacing, sizeof(f32)));
            seed = hash_combine(seed, hash_bytes(&s.grid_background().minor_thickness, sizeof(f32)));
            seed = hash_combine(seed, hash_bytes(&s.grid_background().major_thickness, sizeof(f32)));
            seed = hash_combine(seed, hash_bytes(&s.grid_background().major_every, sizeof(i32)));
            seed = hash_combine(seed, hash_bytes(&s.grid_background().centered, sizeof(bool)));
            seed = hash_combine(seed, hash_bytes(&s.grid_background().edge_fade, sizeof(f32)));
            seed = hash_combine(seed, hash_bytes(&s.grid_background().major_glow, sizeof(f32)));
            return hash_combine(seed, hash_value(s.grid_background().style));
        case ShapeType::Mesh:
            seed = hash_combine(seed, hash_string(s.mesh_shape().reference.path()));
            seed = hash_combine(seed, hash_string(s.mesh_shape().reference.owner()));
            seed = hash_combine(seed, hash_value(s.mesh_shape().reference.required()));
            if (s.mesh_shape().prepared) {
                seed = hash_combine(seed, hash_string(
                    s.mesh_shape().prepared->identity.cache_key()));
            }
            if (s.mesh_shape().inline_mesh) {
                const auto& mesh = *s.mesh_shape().inline_mesh;
                seed = hash_combine(seed, hash_string(mesh.name()));
                seed = hash_combine(seed, hash_value(mesh.vertices().size()));
                for (const auto& vertex : mesh.vertices()) {
                    seed = hash_combine(seed, hash_vec3(vertex.position));
                    seed = hash_combine(seed, hash_vec3(vertex.normal));
                    seed = hash_combine(seed, hash_vec2(vertex.uv));
                }
                seed = hash_combine(seed, hash_value(mesh.indices().size()));
                for (const auto index : mesh.indices())
                    seed = hash_combine(seed, hash_value(index));
            }
            return seed;
        case ShapeType::FakeBox3D:
            seed = hash_combine(seed, hash_vec3(s.fake_box3d().world_pos));
            seed = hash_combine(seed, hash_vec2(s.fake_box3d().size));
            seed = hash_combine(seed, hash_bytes(&s.fake_box3d().depth, sizeof(f32)));
            seed = hash_combine(seed, hash_color(s.fake_box3d().color));
            seed = hash_combine(seed, hash_bytes(&s.fake_box3d().top_tint, sizeof(f32)));
            return hash_combine(seed, hash_bytes(&s.fake_box3d().side_tint, sizeof(f32)));
        case ShapeType::GridPlane:
            seed = hash_combine(seed, hash_vec3(s.grid_plane().world_pos));
            seed = hash_combine(seed, hash_value(static_cast<int>(s.grid_plane().axis)));
            seed = hash_combine(seed, hash_bytes(&s.grid_plane().extent, sizeof(f32)));
            seed = hash_combine(seed, hash_bytes(&s.grid_plane().spacing, sizeof(f32)));
            seed = hash_combine(seed, hash_color(s.grid_plane().color));
            seed = hash_combine(seed, hash_bytes(&s.grid_plane().fade_distance, sizeof(f32)));
            return hash_combine(seed, hash_bytes(&s.grid_plane().fade_min_alpha, sizeof(f32)));
        case ShapeType::TextRun:
            if (s.text_run_shape_handle().value) {
                seed = hash_combine(seed, hash_text_run_shape(*s.text_run_shape_handle().value));
            }
            return seed;
        default:
            return seed;
    }
}

// ── Text hashing (canonical, used by all text caches) ─────────────

[[nodiscard]] inline u64 hash_text_style_full(
    const TextShape& t,
    float effective_size,
    int padding,
    const Mat4* transform = nullptr
) {
    u64 seed = 0;
    seed = hash_combine(seed, hash_string(t.text));
    seed = hash_combine(seed, hash_string(t.style.font_path));
    seed = hash_combine(seed, hash_string(t.style.font_family));
    seed = hash_combine(seed, hash_value(t.style.font_weight));
    seed = hash_combine(seed, hash_string(t.style.font_style));
    seed = hash_combine(seed, hash_value(effective_size));
    seed = hash_combine(seed, hash_color(t.style.color));
    seed = hash_combine(seed, hash_bytes(&t.style.anchor, sizeof(TextAnchor)));
    seed = hash_combine(seed, hash_bytes(&t.style.centering_mode, sizeof(TextCenteringMode)));
    seed = hash_combine(seed, hash_bytes(&t.style.align, sizeof(TextAlign)));
    seed = hash_combine(seed, hash_bytes(&t.style.vertical_align, sizeof(VerticalAlign)));
    seed = hash_combine(seed, hash_value(t.style.line_height));
    seed = hash_combine(seed, hash_value(t.style.tracking));
    seed = hash_combine(seed, hash_value(t.style.max_lines));
    seed = hash_combine(seed, hash_value(t.style.auto_scale));
    seed = hash_combine(seed, hash_value(t.style.min_size));
    seed = hash_combine(seed, hash_value(t.style.max_size));
    seed = hash_combine(seed, hash_value(t.style.auto_fit));
    seed = hash_combine(seed, hash_value(t.style.ellipsis));
    seed = hash_combine(seed, hash_bytes(&t.style.overflow, sizeof(TextOverflow)));
    seed = hash_combine(seed, hash_bytes(&t.style.wrap, sizeof(TextWrap)));
    seed = hash_combine(seed, hash_vec2(t.box.size));
    seed = hash_combine(seed, hash_value(t.box.enabled));
    seed = hash_combine(seed, hash_value(padding));

    // Paint
    seed = hash_combine(seed, hash_color(t.style.paint.fill));
    seed = hash_combine(seed, hash_value(t.style.paint.stroke_enabled));
    seed = hash_combine(seed, hash_color(t.style.paint.stroke_color));
    seed = hash_combine(seed, hash_value(t.style.paint.stroke_width));

    // Fill style (gradient, etc.)
    if (t.style.paint.fill_style.has_value()) {
        seed = hash_combine(seed, hash_fill(*t.style.paint.fill_style));
    } else {
        seed = hash_combine(seed, 0ULL);  // explicit "no fill_style" sentinel
    }

    // ── TextMaterial ────────────────────────────────────────────
    seed = hash_combine(seed, hash_value(t.style.material.enabled));
    if (t.style.material.enabled) {
        seed = hash_combine(seed, hash_color(t.style.material.top_color));
        seed = hash_combine(seed, hash_color(t.style.material.bottom_color));
        seed = hash_combine(seed, hash_value(t.style.material.gradient_angle));
        seed = hash_combine(seed, hash_value(t.style.material.bevel_px));
        seed = hash_combine(seed, hash_value(t.style.material.bevel_highlight_opacity));
        seed = hash_combine(seed, hash_color(t.style.material.bevel_highlight_color));
        seed = hash_combine(seed, hash_value(t.style.material.bevel_shadow_opacity));
        seed = hash_combine(seed, hash_value(t.style.material.top_highlight_opacity));
        seed = hash_combine(seed, hash_value(t.style.material.top_highlight_fraction));
        seed = hash_combine(seed, hash_value(t.style.material.bottom_shade_opacity));
        seed = hash_combine(seed, hash_value(t.style.material.bottom_shade_fraction));
        seed = hash_combine(seed, hash_value(t.style.material.inner_shadow_enabled));
        seed = hash_combine(seed, hash_vec2(t.style.material.inner_shadow_offset));
        seed = hash_combine(seed, hash_value(t.style.material.inner_shadow_blur));
        seed = hash_combine(seed, hash_color(t.style.material.inner_shadow_color));
        seed = hash_combine(seed, hash_value(t.style.material.emissive));
    }

    // ── TextShaping (direction, script, language) ────────────────
    seed = hash_combine(seed, hash_bytes(&t.style.shaping.direction, sizeof(TextDirection)));
    seed = hash_combine(seed, hash_value(t.style.shaping.script));
    seed = hash_combine(seed, hash_string(t.style.shaping.language));

    // Box style
    seed = hash_combine(seed, hash_value(t.style.box_style.enabled));
    seed = hash_combine(seed, hash_vec2(t.style.box_style.padding));
    seed = hash_combine(seed, hash_value(t.style.box_style.radius));
    seed = hash_combine(seed, hash_color(t.style.box_style.background));
    seed = hash_combine(seed, hash_value(t.style.box_style.border_enabled));
    seed = hash_combine(seed, hash_color(t.style.box_style.border_color));
    seed = hash_combine(seed, hash_value(t.style.box_style.border_width));

    // ── TextAnchor has align + vertical_align + padding baked in above

    // Shadows
    seed = hash_combine(seed, hash_value(t.style.shadows.size()));
    for (const auto& shadow : t.style.shadows) {
        seed = hash_combine(seed, hash_value(shadow.enabled));
        seed = hash_combine(seed, hash_vec2(shadow.offset));
        seed = hash_combine(seed, hash_value(shadow.blur));
        seed = hash_combine(seed, hash_value(shadow.opacity));
        seed = hash_combine(seed, hash_color(shadow.color));
    }

    // Transform
    if (transform) {
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                seed = hash_combine(seed, hash_value((*transform)[i][j]));
            }
        }
    }

    return seed;
}

[[nodiscard]] inline u64 hash_layer_transition_parameters(const LayerTransitionParameters& params) {
    return std::visit([](const auto& p) -> u64 {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return 0ULL;
        } else if constexpr (std::is_same_v<T, CrossfadeParams>) {
            return 0xcbf29ce484222325ULL;
        } else if constexpr (std::is_same_v<T, WipeLinearParams>) {
            return 0xcbf29ce484222326ULL;
        } else if constexpr (std::is_same_v<T, SlideParams>) {
            return hash_value(p.distance);
        } else if constexpr (std::is_same_v<T, ZoomParams>) {
            return hash_value(p.amount);
        } else if constexpr (std::is_same_v<T, SmoothWipeParams>) {
            return hash_value(p.feather);
        } else if constexpr (std::is_same_v<T, CircleIrisParams>) {
            u64 h = hash_vec2(p.center);
            return hash_combine(h, hash_value(p.feather));
        } else if constexpr (std::is_same_v<T, FlashParams>) {
            return hash_color(p.color);
        } else if constexpr (std::is_same_v<T, ProceduralRemotionParams>) {
            u64 h = hash_value(p.seed);
            h = hash_combine(h, hash_color(p.inner_color));
            h = hash_combine(h, hash_color(p.mid_color));
            return hash_combine(h, hash_color(p.outer_color));
        } else if constexpr (std::is_same_v<T, RemotionParams>) {
            u64 h = hash_value(p.speed);
            h = hash_combine(h, hash_value(p.direction));
            return hash_combine(h, hash_value(p.angle));
        } else {
            return hash_bytes(&p, sizeof(p));
        }
    }, params);
}

[[nodiscard]] inline u64 hash_layer_transition_spec(const LayerTransitionSpec& spec) {
    u64 h = hash_string(spec.transition_id);
    h = hash_combine(h, static_cast<u64>(spec.direction));
    h = hash_combine(h, hash_bytes(&spec.duration, sizeof(spec.duration)));
    h = hash_combine(h, hash_bytes(&spec.delay, sizeof(spec.delay)));
    h = hash_combine(h, static_cast<u64>(spec.easing));
    h = hash_combine(h, hash_layer_transition_parameters(spec.parameters));
    return h;
}

[[nodiscard]] inline u64 hash_render_node_content_only(const RenderNode& n) {
    u64 seed = hash_bytes(n.name.data(), n.name.size());
    // Ignore world_transform for content-only hashing
    seed = hash_combine(seed, hash_shape(n.shape));
    seed = hash_combine(seed, hash_color(n.color));
    seed = hash_combine(seed, hash_fill(n.fill));
    seed = hash_combine(seed, hash_value(n.inline_mesh_material.has_value()));
    if (n.inline_mesh_material) {
        seed = hash_combine(seed, hash_bytes(&n.inline_mesh_material->diffuse, sizeof(float)));
        seed = hash_combine(seed, hash_bytes(&n.inline_mesh_material->specular, sizeof(float)));
        seed = hash_combine(seed, hash_bytes(&n.inline_mesh_material->shininess, sizeof(float)));
        seed = hash_combine(seed, hash_bytes(&n.inline_mesh_material->rim_intensity, sizeof(float)));
        seed = hash_combine(seed, hash_value(n.inline_mesh_material->base_color.has_value()));
        if (n.inline_mesh_material->base_color)
            seed = hash_combine(seed, hash_color(*n.inline_mesh_material->base_color));
    }
    seed = hash_combine(seed, hash_value(n.visible));
    seed = hash_combine(seed, hash_surface_policy(n.surface_policy));
    seed = hash_combine(seed, hash_transform_policy(n.transform_policy));
    return seed;
}

[[nodiscard]] inline u64 hash_render_node_placement_only(const RenderNode& n) {
    return hash_transform(n.world_transform);
}

[[nodiscard]] inline u64 hash_render_node(const RenderNode& n) {
    static thread_local int depth = 0;
    if (depth > 50) return 0; // Prevent stack overflow
    depth++;
    struct DepthGuard { ~DepthGuard() { depth--; } } guard;

    u64 seed = hash_render_node_content_only(n);
    return hash_combine(seed, hash_render_node_placement_only(n));
}

} // namespace chronon3d::graph
