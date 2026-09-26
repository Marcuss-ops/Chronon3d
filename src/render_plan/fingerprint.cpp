#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/graphics/gradient.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory_resource>
#include <string_view>
#include <type_traits>
#include <vector>

#include <xxhash.h>

namespace chronon3d::render_plan {
namespace {

class CanonicalPlanWriter {
public:
    explicit CanonicalPlanWriter(std::pmr::memory_resource* resource)
        : bytes_(resource) {
        bytes_.reserve(kInitialScratchBytes);
    }

    void add_bool(bool value) { add_u8(value ? 1U : 0U); }

    template <typename T>
        requires std::is_enum_v<T>
    void add_enum(T value) {
        using U = std::underlying_type_t<T>;
        add_i64(static_cast<std::int64_t>(static_cast<U>(value)));
    }

    void add_frame(Frame value) { add_i64(value.integral()); }

    void add_float(float value) {
        if (value == 0.0F) value = 0.0F;  // canonicalize -0
        std::uint32_t bits = 0;
        if (std::isnan(value)) {
            bits = 0x7fc00000U;
        } else {
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&bits, &value, sizeof(bits));
        }
        add_u32(bits);
    }

    void add_i64(std::int64_t value) {
        add_u64(static_cast<std::uint64_t>(value));
    }

    void add_uint64(std::uint64_t value) { add_u64(value); }

    void add_size(std::size_t value) {
        add_u64(static_cast<std::uint64_t>(value));
    }

    void add_string(std::string_view value) {
        add_size(value.size());
        if (!value.empty()) {
            const auto* first = reinterpret_cast<const std::byte*>(value.data());
            bytes_.insert(bytes_.end(), first, first + value.size());
        }
    }

    [[nodiscard]] std::uint64_t finish() const noexcept {
        return XXH3_64bits(bytes_.data(), bytes_.size());
    }

private:
    static constexpr std::size_t kInitialScratchBytes = 64U * 1024U;

    void add_u8(std::uint8_t value) {
        bytes_.push_back(static_cast<std::byte>(value));
    }

    void add_u32(std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8)
            add_u8(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }

    void add_u64(std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8)
            add_u8(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }

    std::pmr::vector<std::byte> bytes_;
};

void add_track(CanonicalPlanWriter& out, const AnimationTrackPlan& track) {
    out.add_string(track.property);
    out.add_string(track.easing);
    if (track.bezier_curve) {
        out.add_bool(true);
        for (const auto value : *track.bezier_curve) out.add_float(value);
    }
    out.add_size(track.keyframes.size());
    for (const auto& key : track.keyframes) {
        out.add_frame(key.frame);
        out.add_size(key.value.size());
        for (const auto value : key.value) out.add_float(value);
        out.add_bool(key.interpolation.has_value());
        if (key.interpolation) out.add_string(*key.interpolation);
        out.add_bool(key.easing.has_value());
        if (key.easing) out.add_string(*key.easing);
        out.add_bool(key.bezier_curve.has_value());
        if (key.bezier_curve)
            for (const auto value : *key.bezier_curve) out.add_float(value);
    }
}

template <typename T, typename Add>
void add_optional(CanonicalPlanWriter& out, const std::optional<T>& value, Add add) {
    out.add_bool(value.has_value());
    if (value) add(out, *value);
}

// Cat-3 — the gradient identity field selection/order has ONE seat
// (graphics::write_gradient_identity in <chronon3d/graphics/gradient.hpp>).
// This sink only encodes that sequence into the canonical byte stream; the
// stream is byte-identical to the historical inline lambdas, so the
// "chronon.render-plan.content-fingerprint.v5" contract is unchanged.
struct CanonicalGradientSink {
    CanonicalPlanWriter& out;

    template <typename T>
    void operator()(const T& value) {
        if constexpr (std::is_enum_v<T>) {
            out.add_enum(value);
        } else if constexpr (std::is_same_v<T, chronon3d::Color>) {
            out.add_float(value.r); out.add_float(value.g);
            out.add_float(value.b); out.add_float(value.a);
        } else if constexpr (std::is_same_v<T, chronon3d::Vec2>) {
            out.add_float(value.x); out.add_float(value.y);
        } else {
            out.add_float(value);
        }
    }

    void count(std::size_t size) { out.add_size(size); }
};

void add_gradient(CanonicalPlanWriter& out, const graphics::GradientDefinition& value) {
    CanonicalGradientSink sink{out};
    graphics::write_gradient_identity(value, sink);
}

void add_style(CanonicalPlanWriter& out, const LayerStylePlan& style) {
    out.add_string(style.font);
    add_optional(out, style.font_size,
                 [](auto& writer, float value) { writer.add_float(value); });
    out.add_string(style.fill);
    out.add_string(style.text_fit_mode);
    add_optional(out, style.min_font_size,
                 [](auto& writer, float value) { writer.add_float(value); });
    add_optional(out, style.max_font_size,
                 [](auto& writer, float value) { writer.add_float(value); });

    add_optional(out, style.stroke, [](auto& writer, const StrokeStyle& stroke) {
        writer.add_string(stroke.color);
        add_optional(writer, stroke.width,
                     [](auto& nested, float value) { nested.add_float(value); });
        add_optional(writer, stroke.trim_start,
                     [](auto& nested, float value) { nested.add_float(value); });
        add_optional(writer, stroke.trim_end,
                     [](auto& nested, float value) { nested.add_float(value); });
        writer.add_size(stroke.dash_array.size());
        for (const float dash : stroke.dash_array) writer.add_float(dash);
        writer.add_float(stroke.dash_offset);
        add_optional(writer, stroke.gradient,
                     [](auto& nested, const graphics::GradientDefinition& value) {
            add_gradient(nested, value);
        });
    });
    add_optional(out, style.shadow, [](auto& writer, const ShadowStyle& shadow) {
        writer.add_string(shadow.color);
        add_optional(writer, shadow.opacity,
                     [](auto& nested, float value) { nested.add_float(value); });
        add_optional(writer, shadow.blur,
                     [](auto& nested, float value) { nested.add_float(value); });
        for (const auto value : shadow.offset) writer.add_float(value);
        writer.add_size(shadow.offset_dimensions);
    });
    if (!style.shadows.empty()) {
        out.add_bool(true);
        out.add_size(style.shadows.size());
        for (const auto& shadow : style.shadows) {
            out.add_string(shadow.color);
            add_optional(out, shadow.opacity, [](auto& nested, float value) { nested.add_float(value); });
            add_optional(out, shadow.blur, [](auto& nested, float value) { nested.add_float(value); });
            for (const auto value : shadow.offset) out.add_float(value);
            out.add_size(shadow.offset_dimensions);
        }
    }
    if (style.backdrop) {
        out.add_bool(true);
        const auto& backdrop = *style.backdrop;
        out.add_float(backdrop.blur);
        out.add_float(backdrop.brightness);
        out.add_float(backdrop.saturation);
    }
    add_optional(out, style.glow, [](auto& writer, const GlowStyle& glow) {
        writer.add_float(glow.radius);
        writer.add_float(glow.intensity);
        writer.add_string(glow.color);
    });
    add_optional(out, style.background,
                 [](auto& writer, const BackgroundStyle& background) {
        writer.add_string(background.color);
        add_optional(writer, background.opacity,
                     [](auto& nested, float value) { nested.add_float(value); });
        add_optional(writer, background.radius,
                     [](auto& nested, float value) { nested.add_float(value); });
        for (const auto value : background.padding) writer.add_float(value);
        writer.add_size(background.padding_dimensions);
    });
}

void add_text_animator(CanonicalPlanWriter& out, const TextAnimatorPlan& animator) {
    out.add_string(animator.id);
    out.add_size(animator.selectors.size());
    for (const auto& selector : animator.selectors) {
        out.add_string(selector.id);
        out.add_string(selector.unit);
        out.add_string(selector.shape);
        out.add_string(selector.order);
        out.add_string(selector.combine);
        add_track(out, selector.start);
        add_track(out, selector.end);
        add_track(out, selector.offset);
        add_track(out, selector.amount);
        out.add_bool(selector.exclude_spaces);
        out.add_bool(selector.randomize_order);
        out.add_uint64(selector.random_seed);
        out.add_float(selector.wiggly_frequency);
        out.add_float(selector.wiggly_amplitude);
        out.add_uint64(selector.wiggly_seed);
        add_optional(out, selector.semantic_id, [](auto& writer, const std::string& value) {
            writer.add_string(value);
        });
    }
    out.add_size(animator.properties.size());
    for (const auto& track : animator.properties) add_track(out, track);
}

void add_transition(CanonicalPlanWriter& out, const TransitionPlan& transition) {
    out.add_string(transition.id);
    out.add_string(transition.kind);
    out.add_frame(transition.duration_frames);
    out.add_frame(transition.delay_frames);
    out.add_string(transition.easing);
    out.add_string(transition.direction);
    out.add_float(transition.distance);
    out.add_float(transition.feather);
    for (const auto value : transition.center) out.add_float(value);
    for (const auto value : transition.color) out.add_float(value);
    out.add_float(transition.seed);
    for (const auto value : transition.inner_color) out.add_float(value);
    for (const auto value : transition.mid_color) out.add_float(value);
    for (const auto value : transition.outer_color) out.add_float(value);
    out.add_float(transition.speed);
    out.add_float(transition.remotion_direction);
    out.add_float(transition.angle);
}

void add_camera(CanonicalPlanWriter& out, const CameraPlan& camera) {
    out.add_string(camera.type);
    for (const auto value : camera.position) out.add_float(value);
    for (const auto value : camera.rotation_deg) out.add_float(value);
    out.add_float(camera.fov_deg);
    out.add_float(camera.near);
    out.add_float(camera.far);
    out.add_float(camera.zoom);
}

void add_camera_track(CanonicalPlanWriter& out, const CameraTrackPlan& track) {
    out.add_enum(track.property);
    out.add_string(track.easing);
    if (track.bezier_curve) {
        out.add_bool(true);
        for (const auto value : *track.bezier_curve) out.add_float(value);
    }
    out.add_size(track.keyframes.size());
    for (const auto& key : track.keyframes) {
        out.add_frame(key.frame);
        out.add_size(key.value.size());
        for (const auto value : key.value) out.add_float(value);
        out.add_bool(key.interpolation.has_value());
        if (key.interpolation) out.add_string(*key.interpolation);
        out.add_bool(key.easing.has_value());
        if (key.easing) out.add_string(*key.easing);
        out.add_bool(key.bezier_curve.has_value());
        if (key.bezier_curve)
            for (const auto value : *key.bezier_curve) out.add_float(value);
    }
}

void add_audio_mix(CanonicalPlanWriter& out, const AudioMixPlan& mix) {
    out.add_size(mix.clips.size());
    for (const auto& clip : mix.clips) {
        out.add_string(clip.id);
        out.add_string(clip.asset);
        add_optional(out, clip.clip_in, [](auto& writer, Frame value) { writer.add_frame(value); });
        add_optional(out, clip.clip_out, [](auto& writer, Frame value) { writer.add_frame(value); });
        add_optional(out, clip.start, [](auto& writer, Frame value) { writer.add_frame(value); });
        out.add_float(clip.gain_db);
        out.add_bool(clip.muted);
        out.add_string(clip.role);
        add_optional(out, clip.gain, [](auto& writer, const AnimationTrackPlan& track) {
            add_track(writer, track);
        });
    }
    out.add_float(mix.master_gain_db);
    out.add_float(mix.duck_amount_db);
    out.add_float(mix.duck_attack_ms);
    out.add_float(mix.duck_release_ms);
}

void add_layer(CanonicalPlanWriter& out, const LayerPlan& layer) {
    out.add_string(layer.id);
    out.add_enum(layer.type);
    add_optional(out, layer.parent, [](auto& writer, const std::string& value) {
        writer.add_string(value);
    });
    add_optional(out, layer.track_matte, [](auto& writer, const TrackMattePlan& value) {
        writer.add_string(value.source);
        writer.add_enum(value.type);
    });
    add_optional(out, layer.time_remap, [](auto& writer, const TimeRemapPlan& value) {
        add_optional(writer, value.speed, [](auto& nested, float speed) {
            nested.add_float(speed);
        });
        writer.add_bool(value.reverse);
        add_optional(writer, value.freeze_frame, [](auto& nested, Frame frame) {
            nested.add_frame(frame);
        });
        add_optional(writer, value.animation, [](auto& nested, const AnimationTrackPlan& track) {
            add_track(nested, track);
        });
    });
    out.add_string(layer.asset);
    out.add_string(layer.source);
    out.add_string(layer.text);
    if (!layer.text_spans.empty()) {
      out.add_size(layer.text_spans.size());
      for (const auto& span : layer.text_spans) {
        out.add_size(span.byte_start);
        out.add_size(span.byte_end);
        add_optional(out, span.color, [](auto& writer, const Color& color) {
            writer.add_float(color.r); writer.add_float(color.g);
            writer.add_float(color.b); writer.add_float(color.a);
        });
        add_optional(out, span.font_size_multiplier, [](auto& writer, float value) {
            writer.add_float(value);
        });
        add_optional(out, span.tracking, [](auto& writer, float value) {
            writer.add_float(value);
        });
        add_optional(out, span.stroke, [](auto& writer, const TextStrokeStyle& stroke) {
            writer.add_float(stroke.width_em);
            add_optional(writer, stroke.color, [](auto& nested, const Color& color) {
                nested.add_float(color.r); nested.add_float(color.g);
                nested.add_float(color.b); nested.add_float(color.a);
            });
            add_optional(writer, stroke.dash_period_em, [](auto& nested, float value) {
                nested.add_float(value);
            });
        });
        add_optional(out, span.semantic_id, [](auto& writer, const std::string& value) {
            writer.add_string(value);
        });
        add_optional(out, span.highlight, [](auto& writer, const TextHighlightStyle& highlight) {
            add_optional(writer, highlight.background, [](auto& nested, const Color& color) {
                nested.add_float(color.r); nested.add_float(color.g);
                nested.add_float(color.b); nested.add_float(color.a);
            });
            add_optional(writer, highlight.padding_em, [](auto& nested, float value) {
                nested.add_float(value);
            });
            add_optional(writer, highlight.radius_em, [](auto& nested, float value) {
                nested.add_float(value);
            });
        });
      }
    }
    if (layer.motion_blur) {
        out.add_float(layer.motion_blur->shutter_angle);
        out.add_size(layer.motion_blur->samples);
    }
    out.add_string(layer.font);
    add_optional(out, layer.style,
                 [](auto& writer, const LayerStylePlan& value) {
                     add_style(writer, value);
                 });
    // Preserve the historical optional-presence byte for every layer; only
    // present shape payloads append the additive shape contract fields.
    out.add_bool(layer.shape.has_value());
    if (layer.shape) {
        const auto& shape = *layer.shape;
        out.add_enum(shape.type);
        add_optional(out, shape.svg_asset, [](auto& nested, const std::string& asset) {
            nested.add_string(asset);
        });
        add_optional(out, shape.lottie_asset, [](auto& nested, const std::string& asset) {
            nested.add_string(asset);
        });
        out.add_float(shape.radius);
        if (shape.corner_radius) {
            out.add_bool(true);
            for (const auto radius : *shape.corner_radius) out.add_float(radius);
        }
        add_optional(out, shape.fill_color, [](auto& nested, const auto& color) {
            for (const auto component : color) nested.add_float(component);
        });
        add_optional(out, shape.gradient, [](auto& nested,
                                                 const graphics::GradientDefinition& value) {
            add_gradient(nested, value);
        });
        add_optional(out, shape.stroke, [](auto& nested, const StrokeStyle& stroke) {
            nested.add_string(stroke.color);
            add_optional(nested, stroke.width, [](auto& w, float v) { w.add_float(v); });
            add_optional(nested, stroke.solid_color, [](auto& w, const auto& color) {
                for (const auto component : color) w.add_float(component);
            });
            add_optional(nested, stroke.trim_start, [](auto& w, float v) { w.add_float(v); });
            add_optional(nested, stroke.trim_end, [](auto& w, float v) { w.add_float(v); });
            nested.add_size(stroke.dash_array.size());
            for (const float dash : stroke.dash_array) nested.add_float(dash);
            nested.add_float(stroke.dash_offset);
            add_optional(nested, stroke.gradient, [](auto& w,
                    const graphics::GradientDefinition& value) {
                add_gradient(w, value);
            });
        });
        out.add_size(shape.path.size());
        for (const auto& command : shape.path) {
            out.add_enum(command.type);
            for (const auto value : command.point) out.add_float(value);
            for (const auto value : command.control1) out.add_float(value);
            for (const auto value : command.control2) out.add_float(value);
        }
        out.add_size(shape.operators.size());
        for (const auto& op : shape.operators) {
            out.add_enum(op.kind);
            if (op.kind == PathOperatorKindPlan::RoundCorners) {
                out.add_float(op.radius);
                continue;
            }
            if (op.kind == PathOperatorKindPlan::ZigZag) {
                out.add_float(op.size);
                out.add_float(op.points_per_unit);
                out.add_bool(op.smooth);
                continue;
            }
            if (op.kind == PathOperatorKindPlan::OffsetPath) {
                out.add_float(op.amount);
                out.add_float(op.miter_limit);
                continue;
            }
            if (op.kind == PathOperatorKindPlan::PuckerBloat) {
                out.add_float(op.amount);
                for (const auto value : op.anchor) out.add_float(value);
                continue;
            }
            if (op.kind == PathOperatorKindPlan::Twist) {
                out.add_float(op.degrees);
                for (const auto value : op.center) out.add_float(value);
                continue;
            }
            if (op.kind == PathOperatorKindPlan::WigglePath) {
                out.add_float(op.amount);
                out.add_float(op.frequency);
                out.add_uint64(op.seed);
                continue;
            }
            if (op.kind == PathOperatorKindPlan::Trim) {
                out.add_float(op.trim_start);
                out.add_float(op.trim_end);
                out.add_float(op.trim_offset);
                continue;
            }
            if (op.kind == PathOperatorKindPlan::Morph) {
                out.add_size(op.morph_target.size());
                for (const auto& command : op.morph_target) {
                    out.add_enum(command.type);
                    for (const auto value : command.point) out.add_float(value);
                    for (const auto value : command.control1) out.add_float(value);
                    for (const auto value : command.control2) out.add_float(value);
                }
                out.add_float(op.morph_progress);
                add_optional(out, op.morph_animation, [](auto& nested, const AnimationTrackPlan& track) {
                    add_track(nested, track);
                });
                continue;
            }
            if (op.kind == PathOperatorKindPlan::Merge) {
                out.add_size(op.paths.size());
                for (const auto& subpath : op.paths) {
                    out.add_size(subpath.size());
                    for (const auto& command : subpath) {
                        out.add_enum(command.type);
                        for (const auto value : command.point) out.add_float(value);
                        for (const auto value : command.control1) out.add_float(value);
                        for (const auto value : command.control2) out.add_float(value);
                    }
                }
                continue;
            }
            const auto& repeater = op.repeater;
            out.add_size(repeater.copies);
            for (const auto value : repeater.position) out.add_float(value);
            for (const auto value : repeater.scale) out.add_float(value);
            out.add_float(repeater.rotation_degrees);
            for (const auto value : repeater.anchor) out.add_float(value);
            out.add_float(repeater.opacity_decay);
        }
    }
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
        out.add_size(particles.path_points.size());
        for (const auto point : particles.path_points) { out.add_float(point.x); out.add_float(point.y); }
        add_gradient(out, particles.color_ramp);
    }
    if (layer.light) {
        out.add_float(layer.light->radius);
        for (const auto component : layer.light->color) out.add_float(component);
        out.add_float(layer.light->intensity);
    }
    if (layer.path_mask) {
        out.add_bool(true);
        out.add_float(layer.path_mask->feather);
        out.add_float(layer.path_mask->expansion);
        out.add_bool(layer.path_mask->inverted);
        out.add_size(layer.path_mask->path.size());
        for (const auto& command : layer.path_mask->path) {
            out.add_enum(command.type);
            for (const auto value : command.point) out.add_float(value);
            for (const auto value : command.control1) out.add_float(value);
            for (const auto value : command.control2) out.add_float(value);
        }
    }
    if (!layer.effects.empty()) {
        out.add_bool(true);
        out.add_size(layer.effects.size());
        for (const auto& effect : layer.effects) {
            out.add_enum(effect.kind);
            if (effect.kind == LayerPlan::EffectKindPlan::Lut3D) {
                out.add_string(effect.asset);
                out.add_float(effect.amount);
            } else if (effect.kind == LayerPlan::EffectKindPlan::Echo) {
                out.add_size(effect.echoes);
                out.add_float(effect.decay);
                out.add_size(effect.time_offset_frames);
            } else if (effect.kind == LayerPlan::EffectKindPlan::Keying) {
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
            } else if (effect.kind == LayerPlan::EffectKindPlan::Threshold) {
                out.add_float(effect.threshold);
                out.add_float(effect.threshold_softness);
            } else if (effect.kind == LayerPlan::EffectKindPlan::GaussianBlur) {
                out.add_float(effect.radius);
            } else if (effect.kind == LayerPlan::EffectKindPlan::DropShadow) {
                out.add_float(effect.radius);
                for (const auto value : effect.offset) out.add_float(value);
                for (const auto value : effect.effect_color) out.add_float(value);
            } else if (effect.kind == LayerPlan::EffectKindPlan::Bloom) {
                out.add_float(effect.bloom_threshold);
                out.add_float(effect.radius);
                out.add_float(effect.bloom_intensity);
            } else if (effect.kind == LayerPlan::EffectKindPlan::Glow) {
                out.add_float(effect.radius);
                out.add_float(effect.glow_intensity);
                for (const auto value : effect.effect_color) out.add_float(value);
            } else if (effect.kind == LayerPlan::EffectKindPlan::Noise) {
                out.add_float(effect.amount);
                out.add_uint64(effect.seed);
                out.add_bool(effect.animated);
                out.add_bool(effect.rgb_noise);
                out.add_float(effect.noise_size);
                out.add_float(effect.grain_shadow_response);
                out.add_float(effect.grain_midtone_response);
                out.add_float(effect.grain_highlight_response);
            } else if (effect.kind == LayerPlan::EffectKindPlan::FractalNoise) {
                out.add_uint64(effect.seed);
                out.add_uint64(effect.octaves);
                out.add_float(effect.frequency);
                out.add_float(effect.amplitude);
                out.add_float(effect.evolution);
                out.add_float(effect.contrast);
                out.add_float(effect.brightness);
                out.add_bool(effect.rgb_noise);
            } else if (effect.kind == LayerPlan::EffectKindPlan::CornerPin) {
                for (const auto value : effect.quad) out.add_float(value);
            } else if (effect.kind == LayerPlan::EffectKindPlan::Twirl) {
                for (const auto value : effect.warp_center) out.add_float(value);
                out.add_float(effect.warp_angle);
                out.add_float(effect.warp_radius);
                out.add_float(effect.warp_falloff);
            } else if (effect.kind == LayerPlan::EffectKindPlan::Bulge) {
                for (const auto value : effect.warp_center) out.add_float(value);
                out.add_float(effect.warp_amount);
                out.add_float(effect.warp_radius);
            } else if (effect.kind == LayerPlan::EffectKindPlan::MeshWarp) {
                out.add_size(effect.mesh_cols);
                out.add_size(effect.mesh_rows);
                out.add_float(effect.mesh_smoothness);
                out.add_size(effect.mesh_offsets.size());
                for (const auto value : effect.mesh_offsets) out.add_float(value);
            } else if (effect.kind == LayerPlan::EffectKindPlan::Sharpen) {
                out.add_float(effect.amount); out.add_float(effect.radius); out.add_float(effect.threshold);
            } else if (effect.kind == LayerPlan::EffectKindPlan::TurbulentDisplace) {
                out.add_float(effect.amount); out.add_float(effect.warp_size); out.add_float(effect.warp_evolution);
                out.add_float(effect.warp_speed); out.add_uint64(effect.warp_complexity);
            } else if (effect.kind == LayerPlan::EffectKindPlan::WaveWarp) {
                out.add_uint64(effect.wave_type); for (auto v : effect.warp_direction) out.add_float(v);
                out.add_float(effect.amount); out.add_float(effect.warp_wavelength); out.add_float(effect.warp_speed); out.add_float(effect.warp_phase);
            } else if (effect.kind == LayerPlan::EffectKindPlan::Ripple) {
                for(auto v:effect.warp_center) out.add_float(v); out.add_float(effect.warp_radius); out.add_float(effect.warp_speed);
                out.add_float(effect.warp_width); out.add_float(effect.warp_height); out.add_float(effect.warp_damping);
            } else if(effect.kind==LayerPlan::EffectKindPlan::DisplacementMap) {
                out.add_string(effect.map_layer); out.add_float(effect.map_intensity_x);
                out.add_float(effect.map_intensity_y); out.add_uint64(effect.map_channel);

            } else {
                out.add_size(effect.block_size);
            }
            add_optional(out,effect.posterize_fps,[](auto& w,float v){w.add_float(v);});
            add_optional(out,effect.time_curve,[](auto& w,const AnimationTrackPlan& track){add_track(w,track);});
        }
    }

    for (const auto value : layer.size) out.add_float(value);
    out.add_size(layer.size_dimensions);
    for (const auto value : layer.color) out.add_float(value);
    for (const auto value : layer.position) out.add_float(value);
    out.add_size(layer.position_dimensions);
    for (const auto value : layer.scale) out.add_float(value);
    out.add_size(layer.scale_dimensions);
    for (const auto value : layer.rotation) out.add_float(value);
    out.add_size(layer.rotation_dimensions);
    out.add_bool(layer.enable_3d);
    out.add_bool(layer.screen_space);
    // Preserve byte-for-byte fingerprints for plans authored before safe
    // insets existed; the presence marker is only emitted for this extension.
    if (layer.safe_insets) {
        out.add_bool(true);
        for (const float inset : *layer.safe_insets) out.add_float(inset);
    }
    add_optional(out, layer.depth_role,
                 [](auto& writer, const std::string& value) { writer.add_string(value); });
    out.add_float(layer.depth_offset);

    add_optional(out, layer.radius,
                 [](auto& writer, float value) { writer.add_float(value); });

    add_optional(out, layer.start_frame,
                 [](auto& writer, Frame value) { writer.add_frame(value); });
    add_optional(out, layer.duration_frames,
                 [](auto& writer, Frame value) { writer.add_frame(value); });
    add_optional(out, layer.transition_in,
                 [](auto& writer, const TransitionPlan& value) { add_transition(writer, value); });
    add_optional(out, layer.transition_out,
                 [](auto& writer, const TransitionPlan& value) { add_transition(writer, value); });
    add_optional(out, layer.fit,
                 [](auto& writer, FitMode value) { writer.add_enum(value); });

    add_optional(out, layer.animation,
                 [](auto& writer, const AnimationPlan& animation) {
        writer.add_size(animation.tracks.size());
        for (const auto& track : animation.tracks) add_track(writer, track);
    });

    out.add_size(layer.text_animators.size());
    for (const auto& animator : layer.text_animators)
        add_text_animator(out, animator);

    add_optional(out, layer.blend_mode,
                 [](auto& writer, chronon3d::BlendMode value) {
                     writer.add_enum(value);
                 });
    add_optional(out, layer.opacity,
                 [](auto& writer, float value) { writer.add_float(value); });
    out.add_bool(layer.loop);
}

}  // namespace

std::uint64_t compute_render_plan_content_fingerprint(const RenderPlan& plan) {
    std::array<std::byte, 64U * 1024U> scratch_storage{};
    std::pmr::monotonic_buffer_resource scratch{
        scratch_storage.data(), scratch_storage.size()};
    CanonicalPlanWriter out{&scratch};

    out.add_string("chronon3d.render-plan.content-fingerprint.v9");
    out.add_string(plan.schema);
    out.add_i64(plan.canvas.width);
    out.add_i64(plan.canvas.height);
    out.add_i64(plan.canvas.fps.num());
    out.add_i64(plan.canvas.fps.den());
    out.add_frame(plan.canvas.duration);
    if (plan.temporal_motion_blur) {
        // Preserve the historical byte stream for plans without this v3-only
        // extension; present values still contribute fully to identity.
        out.add_bool(true);
        out.add_float(plan.temporal_motion_blur->shutter_angle);
        out.add_uint64(plan.temporal_motion_blur->samples);
    }
    add_optional(out, plan.camera,
                 [](auto& writer, const CameraPlan& camera) { add_camera(writer, camera); });
    add_optional(out, plan.camera_animation,
                 [](auto& writer, const std::vector<CameraTrackPlan>& tracks) {
                     writer.add_size(tracks.size());
                     for (const auto& track : tracks) add_camera_track(writer, track);
                 });
    add_optional(out, plan.audio, [](auto& writer, const AudioMixPlan& mix) {
        add_audio_mix(writer, mix);
    });
    out.add_uint64(plan.budget.max_temporal_pixels);
    out.add_size(plan.layers.size());
    for (const auto& layer : plan.layers) add_layer(out, layer);

    return out.finish();
}

}  // namespace chronon3d::render_plan
