#include "fingerprint_detail.hpp"
#include "effect_kind_coverage.hpp"
#include <chronon3d/graphics/gradient.hpp>

#include <cstdint>
#include <string_view>

namespace chronon3d::render_plan::fingerprint_detail {

void add_spring(CanonicalPlanWriter& out, const SpringPlan& spring) {
    out.add_float(spring.mass);
    out.add_float(spring.stiffness);
    out.add_float(spring.damping);
    out.add_float(spring.initial_velocity);
}

void add_track(CanonicalPlanWriter& out, const AnimationTrackPlan& track) {
    out.add_string(track.property);
    out.add_string(track.easing);
    out.add_string(track.loop_mode);
    if (track.wiggle) {
        out.add_bool(true);
        out.add_float(track.wiggle->frequency);
        out.add_float(track.wiggle->amplitude);
        out.add_uint64(track.wiggle->seed);
    } else {
        out.add_bool(false);
    }
    if (track.oscillate) {
        out.add_bool(true);
        out.add_float(track.oscillate->frequency_hz);
        out.add_float(track.oscillate->amplitude);
        out.add_float(track.oscillate->phase_deg);
        out.add_string(track.oscillate->waveform);
        out.add_float(track.oscillate->offset);
    } else {
        out.add_bool(false);
    }
    if (track.random_hold) {
        out.add_bool(true);
        out.add_uint64(track.random_hold->interval_frames);
        out.add_float(track.random_hold->min);
        out.add_float(track.random_hold->max);
        out.add_uint64(track.random_hold->seed);
    } else {
        out.add_bool(false);
    }
    if (track.posterize_time) {
        out.add_bool(true);
        out.add_float(track.posterize_time->fps);
    } else {
        out.add_bool(false);
    }
    if (track.data_feed) {
        out.add_bool(true);
        out.add_string(track.data_feed->asset.empty() ? "inline" : "asset");
        out.add_string(track.data_feed->asset);
        out.add_string(track.data_feed->sha256);
        out.add_uint64(track.data_feed->item_count);
        out.add_uint64(static_cast<std::uint64_t>(
            static_cast<std::int64_t>(track.data_feed->index_offset)));
        out.add_string(track.data_feed->interpolation);
        if (track.data_feed->asset.empty()) {
            out.add_size(track.data_feed->values.size());
            for (const auto sample : track.data_feed->values) out.add_float(sample);
        }
    } else {
        out.add_bool(false);
    }
    if (track.audio_react) {
        out.add_bool(true);
        out.add_string(track.audio_react->source);
        out.add_string(track.audio_react->feature);
        out.add_bool(track.audio_react->band_hz.has_value());
        if (track.audio_react->band_hz) {
            out.add_float((*track.audio_react->band_hz)[0]);
            out.add_float((*track.audio_react->band_hz)[1]);
        }
        out.add_uint64(track.audio_react->smoothing_frames);
        out.add_float(track.audio_react->gain);
        out.add_string(track.audio_react->map);
        out.add_string("chronon-audio-react-v1-48k-hann-goertzel32");
        out.add_string(track.audio_react->source_sha256);
        out.add_size(track.audio_react->baked_values.size());
        for (const auto sample : track.audio_react->baked_values) out.add_float(sample);
    } else {
        out.add_bool(false);
    }
    if (track.spring) {
        out.add_bool(true);
        out.add_float(track.spring->mass);
        out.add_float(track.spring->stiffness);
        out.add_float(track.spring->damping);
        out.add_float(track.spring->initial_velocity);
    }
    if (track.bezier_curve) {
        out.add_bool(true);
        for (const auto value : *track.bezier_curve) out.add_float(value);
    }
    add_optional(out, track.spring, [](auto& writer, const SpringPlan& spring) {
        add_spring(writer, spring);
    });
    out.add_size(track.keyframes.size());
    for (const auto& key : track.keyframes) {
        out.add_frame(key.frame);
        out.add_size(key.value.size());
        for (const auto value : key.value) out.add_float(value);
        out.add_bool(key.interpolation.has_value());
        if (key.interpolation) out.add_string(*key.interpolation);
        out.add_bool(key.easing.has_value());
        if (key.easing) out.add_string(*key.easing);
        out.add_bool(key.easing_xy.has_value());
        if (key.easing_xy) {
            out.add_string((*key.easing_xy)[0]);
            out.add_string((*key.easing_xy)[1]);
        }
        if (key.spring) {
            out.add_bool(true);
            out.add_float(key.spring->mass);
            out.add_float(key.spring->stiffness);
            out.add_float(key.spring->damping);
            out.add_float(key.spring->initial_velocity);
        }
        out.add_bool(key.bezier_curve.has_value());
        if (key.bezier_curve)
            for (const auto value : *key.bezier_curve) out.add_float(value);
        out.add_bool(key.roving);
        out.add_bool(key.temporal_in_dx.has_value());
        if (key.temporal_in_dx) out.add_float(*key.temporal_in_dx);
        out.add_bool(key.temporal_in_dy.has_value());
        if (key.temporal_in_dy) out.add_float(*key.temporal_in_dy);
        out.add_bool(key.temporal_out_dx.has_value());
        if (key.temporal_out_dx) out.add_float(*key.temporal_out_dx);
        out.add_bool(key.temporal_out_dy.has_value());
        if (key.temporal_out_dy) out.add_float(*key.temporal_out_dy);
        add_optional(out, key.spring, [](auto& writer, const SpringPlan& spring) {
            add_spring(writer, spring);
        });
    }
}

// Cat-3 — the gradient identity field selection/order has ONE seat
// (graphics::write_gradient_identity in <chronon3d/graphics/gradient.hpp>).
// This sink only encodes that sequence into the canonical byte stream; the
// stream is byte-identical to the historical inline lambdas; additive
// render-plan fields are versioned by the current v27 token below.
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

void add_field2d(CanonicalPlanWriter& out, const graphics::Field2D& field) {
    out.add_enum(field.generator);
    out.add_uint64(field.seed);
    out.add_float(field.frequency);
    out.add_uint64(field.octaves);
    out.add_float(field.persistence);
    out.add_float(field.lacunarity);
    out.add_size(field.operators.size());
    for (const auto& op : field.operators) {
        out.add_enum(op.kind);
        out.add_float(op.from);
        out.add_float(op.to);
        out.add_float(op.edge);
        out.add_float(op.softness);
        out.add_float(op.amount);
    }
}

void add_gradient_animation(CanonicalPlanWriter& out,
                            const GradientAnimationPlan& animation) {
    out.add_string(animation.easing);
    add_optional(out, animation.bezier_curve, [](auto& writer, const auto& curve) {
        for (const auto value : curve) writer.add_float(value);
    });
    out.add_size(animation.keyframes.size());
    for (const auto& key : animation.keyframes) {
        out.add_frame(key.frame);
        add_gradient(out, key.value);
    }
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
        out.add_float(backdrop.rim_light);
        out.add_float(backdrop.refraction);
        out.add_float(backdrop.surface_noise);
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
        out.add_float(selector.noise_frequency);
        out.add_float(selector.noise_amplitude);
        out.add_uint64(selector.noise_seed);
        out.add_uint64(selector.index_step);
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
    out.add_float(transition.zoom_amount);
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
    out.add_bool(camera.dof_enabled);
    out.add_float(camera.dof_focus_distance);
    out.add_float(camera.dof_aperture);
    out.add_float(camera.dof_max_blur);
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
        out.add_bool(key.roving);
        out.add_bool(key.temporal_in_dx.has_value());
        if (key.temporal_in_dx) out.add_float(*key.temporal_in_dx);
        out.add_bool(key.temporal_in_dy.has_value());
        if (key.temporal_in_dy) out.add_float(*key.temporal_in_dy);
        out.add_bool(key.temporal_out_dx.has_value());
        if (key.temporal_out_dx) out.add_float(*key.temporal_out_dx);
        out.add_bool(key.temporal_out_dy.has_value());
        if (key.temporal_out_dy) out.add_float(*key.temporal_out_dy);
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

void add_stagger(CanonicalPlanWriter& out, const LayerPlan::StaggerPlan& stagger) {
    out.add_uint64(stagger.per_item_frames);
    out.add_string(stagger.order);
    out.add_uint64(stagger.seed);
    out.add_string(stagger.track_property);
    out.add_string(stagger.easing);
}


} // namespace chronon3d::render_plan::fingerprint_detail
