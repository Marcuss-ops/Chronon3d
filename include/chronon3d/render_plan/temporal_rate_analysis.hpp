#pragma once

#include <chronon3d/render_plan/render_plan.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d::render_plan {

enum class TemporalRateClass { Static, Cue, KeyframeInterval, PerFrame, Subframe };

struct LayerRateReport {
    std::string layer_id;
    TemporalRateClass rate{TemporalRateClass::Static};
    std::vector<Frame> eval_frames;
    std::uint64_t eval_frame_count{0};
    bool eval_frames_complete{true};
    struct PropertySchedule {
        std::string path;
        TemporalRateClass rate{TemporalRateClass::Static};
        std::vector<Frame> eval_frames;
        std::uint64_t eval_frame_count{0};
        bool eval_frames_complete{true};
    };
    std::vector<PropertySchedule> properties;
};

struct TemporalRateReport {
    double skippable_node_fraction{0.0};
    std::vector<LayerRateReport> layers;
};

namespace temporal_rate_detail {
inline constexpr std::size_t kMaxReportedFrames = 1'000'000;

[[nodiscard]] inline bool active_motion_blur(const MotionBlurPlan& blur) noexcept {
    return blur.samples > 1 && blur.shutter_angle > 0.0f;
}

[[nodiscard]] inline bool track_has_samples(const AnimationTrackPlan& track) noexcept {
    return track.keyframes.size() > 1 || track.wiggle.has_value() ||
        track.oscillate.has_value() || track.random_hold.has_value() ||
        track.posterize_time.has_value() || track.data_feed.has_value() ||
        track.audio_react.has_value();
}

[[nodiscard]] inline bool track_is_per_frame(const AnimationTrackPlan& track) noexcept {
    if (track.wiggle || track.oscillate || track.random_hold ||
        track.posterize_time || track.data_feed || track.audio_react) return true;
    if (track.keyframes.size() < 2) return false;
    for (std::size_t i = 1; i < track.keyframes.size(); ++i) {
        const auto previous = track.keyframes[i - 1].frame.integral();
        const auto current = track.keyframes[i].frame.integral();
        if (current <= previous || previous == std::numeric_limits<std::int64_t>::max() ||
            current != previous + 1) return false;
    }
    return true;
}

struct TrackFacts { bool animated{false}; bool per_frame{false}; };
inline void inspect_track(const AnimationTrackPlan& track, TrackFacts& facts) noexcept {
    if (!track_has_samples(track)) return;
    facts.animated = true;
    facts.per_frame = facts.per_frame || track_is_per_frame(track) ||
        track.loop_mode != "none";
}
inline void inspect_mask(const LayerPlan::PathMaskPlan& mask, TrackFacts& facts) noexcept {
    if (mask.morph_animation) inspect_track(*mask.morph_animation, facts);
    if (mask.feather_track) inspect_track(*mask.feather_track, facts);
    if (mask.expansion_track) inspect_track(*mask.expansion_track, facts);
    if (mask.opacity_track) inspect_track(*mask.opacity_track, facts);
}
inline void inspect_gradient(const GradientAnimationPlan& gradient, TrackFacts& facts) noexcept {
    if (gradient.keyframes.size() < 2) return;
    facts.animated = true;
    bool dense = true;
    for (std::size_t i = 1; i < gradient.keyframes.size(); ++i) {
        const auto previous = gradient.keyframes[i - 1].frame.integral();
        const auto current = gradient.keyframes[i].frame.integral();
        if (current <= previous || previous == std::numeric_limits<std::int64_t>::max() ||
            current != previous + 1) { dense = false; break; }
    }
    facts.per_frame = facts.per_frame || dense;
}
inline void inspect_subpath(const ImportedSubpathPlan& subpath, TrackFacts& facts) noexcept {
    for (const auto* track : {&subpath.fill_color_animation, &subpath.fill_opacity_animation,
                              &subpath.stroke_color_animation, &subpath.stroke_opacity_animation,
                              &subpath.stroke_width_animation, &subpath.svg_opacity_animation,
                              &subpath.svg_translation_animation, &subpath.svg_scale_animation,
                              &subpath.svg_rotation_animation, &subpath.morph_animation})
        if (*track) inspect_track(**track, facts);
    if (subpath.fill_gradient_animation) inspect_gradient(*subpath.fill_gradient_animation, facts);
    if (subpath.lottie_group_transform) {
        const auto& t = *subpath.lottie_group_transform;
        for (const auto* track : {&t.position_animation, &t.scale_animation,
                                  &t.rotation_animation, &t.opacity_animation})
            if (*track) inspect_track(**track, facts);
    }
}

[[nodiscard]] inline TrackFacts inspect_layer(const LayerPlan& layer) noexcept {
    TrackFacts facts;
    if (layer.animation) for (const auto& t : layer.animation->tracks) inspect_track(t, facts);
    if (layer.fill_gradient_animation) inspect_gradient(*layer.fill_gradient_animation, facts);
    for (const auto& animator : layer.text_animators) {
        for (const auto& t : animator.properties) inspect_track(t, facts);
        for (const auto& s : animator.selectors) {
            inspect_track(s.start, facts); inspect_track(s.end, facts);
            inspect_track(s.offset, facts); inspect_track(s.amount, facts);
        }
    }
    if (layer.follow_path) inspect_track(layer.follow_path->progress, facts);
    if (layer.time_remap && layer.time_remap->animation) inspect_track(*layer.time_remap->animation, facts);
    if (layer.text_counter && layer.text_counter->counter.duration_frames.integral() > 0)
        facts.animated = facts.per_frame = true;
    if ((layer.transition_in && layer.transition_in->duration_frames.integral() > 0) ||
        (layer.transition_out && layer.transition_out->duration_frames.integral() > 0))
        facts.animated = facts.per_frame = true;
    if (layer.path_mask) inspect_mask(*layer.path_mask, facts);
    for (const auto& m : layer.masks) inspect_mask(m, facts);
    if (layer.type == LayerType::Particles) facts.animated = facts.per_frame = true;
    for (const auto& e : layer.effect_param_tracks) inspect_track(e.track, facts);
    for (const auto& e : layer.effects) {
        if (e.time_curve) inspect_track(*e.time_curve, facts);
        if (e.animated || e.posterize_fps || e.evolution != 0.0f ||
            e.warp_evolution != 0.0f || e.warp_speed != 0.0f)
            facts.animated = facts.per_frame = true;
    }
    if (layer.shape) {
        if (layer.shape->field_drift &&
            (layer.shape->field_drift->x != 0.0f || layer.shape->field_drift->y != 0.0f))
            facts.animated = facts.per_frame = true;
        for (const auto& subpath : layer.shape->imported_subpaths) inspect_subpath(subpath, facts);
        for (const auto& op : layer.shape->operators) {
            if (op.morph_animation) inspect_track(*op.morph_animation, facts);
            if (op.trim_animation) inspect_track(*op.trim_animation, facts);
        }
    }
    const auto staggered = [](const std::optional<LayerPlan::StaggerPlan>& s) {
        return s && s->per_item_frames > 0;
    };
    if (staggered(layer.stagger_group) ||
        (layer.flow_layout && staggered(layer.flow_layout->stagger)) ||
        (layer.grid_layout && staggered(layer.grid_layout->stagger)))
        facts.animated = facts.per_frame = true;
    return facts;
}

[[nodiscard]] inline Frame saturating_add(Frame lhs, Frame rhs) noexcept {
    const auto a = lhs.integral(), b = rhs.integral();
    if (b > 0 && a > std::numeric_limits<std::int64_t>::max() - b)
        return Frame{std::numeric_limits<std::int64_t>::max()};
    if (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b)
        return Frame{std::numeric_limits<std::int64_t>::min()};
    return Frame{a + b};
}
[[nodiscard]] inline Frame clamp_frame(Frame frame, Frame duration) noexcept {
    if (frame.integral() < 0) return Frame{0};
    return frame > duration ? duration : frame;
}
[[nodiscard]] inline std::pair<Frame, Frame> active_interval(
    const LayerPlan& layer, Frame duration) noexcept {
    const auto start = layer.start_frame.value_or(Frame{0});
    const auto end = layer.duration_frames ? saturating_add(start, *layer.duration_frames) : duration;
    return {clamp_frame(start, duration), clamp_frame(end, duration)};
}
[[nodiscard]] inline std::uint64_t range_count(Frame first, Frame end) noexcept {
    if (end <= first) return 0;
    return static_cast<std::uint64_t>(end.integral()) -
        static_cast<std::uint64_t>(first.integral());
}
inline void append_range(std::vector<Frame>& out, Frame first, Frame end, bool& complete) {
    const auto count = range_count(first, end);
    const auto available = kMaxReportedFrames - std::min(out.size(), kMaxReportedFrames);
    const auto n = std::min<std::uint64_t>(count, available);
    out.reserve(out.size() + static_cast<std::size_t>(n));
    for (std::uint64_t i = 0; i < n; ++i)
        out.emplace_back(first.integral() + static_cast<std::int64_t>(i));
    if (n != count) complete = false;
}
inline void append_event(std::vector<Frame>& out, Frame frame, bool& complete) {
    if (out.size() == kMaxReportedFrames) { complete = false; return; }
    out.push_back(frame);
}
inline void append_track_schedule(std::vector<Frame>& out, const AnimationTrackPlan& t,
                                  Frame first, Frame end, bool& complete) {
    if (t.keyframes.empty() || end <= first) return;
    for (std::size_t i = 0; i < t.keyframes.size(); ++i) {
        const auto& key = t.keyframes[i];
        if (i + 1 == t.keyframes.size()) {
            if (key.frame >= first && key.frame < end)
                append_event(out, key.frame, complete);
            continue;
        }
        const auto& next = t.keyframes[i + 1];
        const Frame segment_first{std::max(first.integral(), key.frame.integral())};
        const Frame segment_end{std::min(end.integral(), next.frame.integral())};
        if (segment_end <= segment_first) continue;
        const bool hold = key.interpolation
            ? *key.interpolation == "hold"
            : (key.easing ? *key.easing == "hold" : t.easing == "hold");
        if (hold) {
            if (key.frame >= first && key.frame < end)
                append_event(out, key.frame, complete);
        } else {
            append_range(out, segment_first, segment_end, complete);
        }
    }
}
inline void append_gradient_keys(std::vector<Frame>& out, const GradientAnimationPlan& t,
                                 Frame first, Frame end, bool& complete) {
    for (const auto& key : t.keyframes)
        if (key.frame >= first && key.frame < end) append_event(out, key.frame, complete);
}

inline LayerRateReport::PropertySchedule make_property_schedule(
    std::string path, const AnimationTrackPlan& track, Frame first, Frame end) {
    LayerRateReport::PropertySchedule property;
    property.path = std::move(path);
    property.rate = track_is_per_frame(track) || track.loop_mode != "none"
        ? TemporalRateClass::PerFrame
        : (track_has_samples(track) ? TemporalRateClass::KeyframeInterval
                                    : TemporalRateClass::Static);
    if (property.rate == TemporalRateClass::PerFrame) {
        property.eval_frame_count = range_count(first, end);
        if (property.eval_frame_count <= kMaxReportedFrames)
            append_range(property.eval_frames, first, end, property.eval_frames_complete);
        else
            property.eval_frames_complete = false;
    } else {
        append_track_schedule(property.eval_frames, track, first, end,
                              property.eval_frames_complete);
        std::sort(property.eval_frames.begin(), property.eval_frames.end());
        property.eval_frames.erase(std::unique(property.eval_frames.begin(), property.eval_frames.end()),
                                   property.eval_frames.end());
        property.eval_frame_count = property.eval_frames.size();
    }
    // An incomplete list is diagnostic only. Never expose a prefix as an executable schedule.
    if (!property.eval_frames_complete) {
        property.eval_frames.clear();
        if (property.rate != TemporalRateClass::PerFrame)
            property.eval_frame_count = 0;
    }
    return property;
}

inline void append_property_schedules(std::vector<LayerRateReport::PropertySchedule>& out,
                                      const LayerPlan& layer, Frame first, Frame end) {
    const auto add = [&](std::string path, const AnimationTrackPlan& track) {
        if (!track_has_samples(track)) return;
        out.push_back(make_property_schedule(std::move(path), track, first, end));
    };
    if (layer.animation) for (const auto& track : layer.animation->tracks)
        add("animation." + track.property, track);
    for (std::size_t i = 0; i < layer.text_animators.size(); ++i) {
        const auto& animator = layer.text_animators[i];
        for (const auto& track : animator.properties)
            add("text_animators[" + std::to_string(i) + "]." + track.property, track);
        for (std::size_t j = 0; j < animator.selectors.size(); ++j) {
            const auto& selector = animator.selectors[j];
            const auto prefix = "text_animators[" + std::to_string(i) + "].selectors[" +
                std::to_string(j) + "].";
            add(prefix + "start", selector.start); add(prefix + "end", selector.end);
            add(prefix + "offset", selector.offset); add(prefix + "amount", selector.amount);
        }
    }
    if (layer.follow_path) add("follow_path.progress", layer.follow_path->progress);
    if (layer.time_remap && layer.time_remap->animation)
        add("time_remap", *layer.time_remap->animation);
    for (std::size_t i = 0; i < layer.effect_param_tracks.size(); ++i)
        add("effect_param_tracks[" + std::to_string(i) + "]." +
            layer.effect_param_tracks[i].param, layer.effect_param_tracks[i].track);
    if (layer.path_mask) {
        const auto& m = *layer.path_mask;
        if (m.morph_animation) add("path_mask.morph", *m.morph_animation);
        if (m.feather_track) add("path_mask.feather", *m.feather_track);
        if (m.expansion_track) add("path_mask.expansion", *m.expansion_track);
        if (m.opacity_track) add("path_mask.opacity", *m.opacity_track);
    }
    for (std::size_t i = 0; i < layer.masks.size(); ++i) {
        const auto& m = layer.masks[i];
        const auto prefix = "masks[" + std::to_string(i) + "].";
        if (m.morph_animation) add(prefix + "morph", *m.morph_animation);
        if (m.feather_track) add(prefix + "feather", *m.feather_track);
        if (m.expansion_track) add(prefix + "expansion", *m.expansion_track);
        if (m.opacity_track) add(prefix + "opacity", *m.opacity_track);
    }
    if (layer.shape) {
        for (std::size_t i = 0; i < layer.shape->imported_subpaths.size(); ++i) {
            const auto& s = layer.shape->imported_subpaths[i];
            const auto prefix = "shape.imported_subpaths[" + std::to_string(i) + "].";
            const std::pair<const char*, const std::optional<AnimationTrackPlan>*> tracks[] = {
                {"fill_color", &s.fill_color_animation}, {"fill_opacity", &s.fill_opacity_animation},
                {"stroke_color", &s.stroke_color_animation}, {"stroke_opacity", &s.stroke_opacity_animation},
                {"stroke_width", &s.stroke_width_animation}, {"svg_opacity", &s.svg_opacity_animation},
                {"svg_translation", &s.svg_translation_animation}, {"svg_scale", &s.svg_scale_animation},
                {"svg_rotation", &s.svg_rotation_animation}, {"morph", &s.morph_animation}};
            for (const auto& [name, track] : tracks)
                if (*track) add(prefix + name, **track);
            if (s.lottie_group_transform) {
                const auto& t = *s.lottie_group_transform;
                if (t.position_animation) add(prefix + "group.position", *t.position_animation);
                if (t.scale_animation) add(prefix + "group.scale", *t.scale_animation);
                if (t.rotation_animation) add(prefix + "group.rotation", *t.rotation_animation);
                if (t.opacity_animation) add(prefix + "group.opacity", *t.opacity_animation);
            }
        }
        for (std::size_t i = 0; i < layer.shape->operators.size(); ++i) {
            const auto& op = layer.shape->operators[i];
            const auto prefix = "shape.operators[" + std::to_string(i) + "].";
            if (op.morph_animation) add(prefix + "morph", *op.morph_animation);
            if (op.trim_animation) add(prefix + "trim", *op.trim_animation);
        }
    }
    for (std::size_t i = 0; i < layer.effects.size(); ++i)
        if (layer.effects[i].time_curve)
            add("effects[" + std::to_string(i) + "].time_curve", *layer.effects[i].time_curve);
}

inline void append_builtin_property_schedules(
    std::vector<LayerRateReport::PropertySchedule>& out,
    const LayerPlan& layer, TemporalRateClass layer_rate,
    Frame first, Frame end, Frame duration) {
    const auto has_prefix = [&out](std::string_view prefix) {
        return std::any_of(out.begin(), out.end(), [prefix](const auto& item) {
            return std::string_view{item.path}.starts_with(prefix);
        });
    };
    const auto add_static = [&out](const char* path) {
        LayerRateReport::PropertySchedule property;
        property.path = path;
        property.rate = TemporalRateClass::Static;
        out.push_back(std::move(property));
    };
    const auto add_cue = [&out, first, end, duration](const char* path) {
        LayerRateReport::PropertySchedule property;
        property.path = path;
        property.rate = TemporalRateClass::Cue;
        if (first < end) {
            append_event(property.eval_frames, first, property.eval_frames_complete);
            if (end < duration)
                append_event(property.eval_frames, end, property.eval_frames_complete);
        }
        property.eval_frame_count = property.eval_frames.size();
        out.push_back(std::move(property));
    };
    if (!has_prefix("animation.position") && !has_prefix("follow_path."))
        add_static("transform.position");
    if (!has_prefix("animation.scale")) add_static("transform.scale");
    if (!has_prefix("animation.rotation")) add_static("transform.rotation");
    if (!has_prefix("animation.opacity")) add_static("opacity");
    if (layer.start_frame || layer.duration_frames)
        add_cue("visibility.active");
    else
        add_static("visibility.active");

    LayerRateReport::PropertySchedule content;
    content.path = "content";
    content.rate = (layer.type == LayerType::Video || layer.type == LayerType::Particles)
        ? TemporalRateClass::PerFrame : TemporalRateClass::Static;
    if (content.rate == TemporalRateClass::PerFrame) {
        content.eval_frame_count = range_count(first, end);
        if (content.eval_frame_count <= kMaxReportedFrames)
            append_range(content.eval_frames, first, end, content.eval_frames_complete);
        else
            content.eval_frames_complete = false;
        if (!content.eval_frames_complete) content.eval_frames.clear();
    }
    (void)layer_rate;
    out.push_back(std::move(content));
}

inline void append_layer_keys(std::vector<Frame>& out, const LayerPlan& layer,
                              Frame duration, bool& complete) {
    const auto [first, end] = active_interval(layer, duration);
    const auto add = [&](const AnimationTrackPlan& t) { append_track_schedule(out, t, first, end, complete); };
    if (layer.animation) for (const auto& t : layer.animation->tracks) add(t);
    if (layer.fill_gradient_animation) append_gradient_keys(out, *layer.fill_gradient_animation, first, end, complete);
    for (const auto& a : layer.text_animators) {
        for (const auto& t : a.properties) add(t);
        for (const auto& s : a.selectors) { add(s.start); add(s.end); add(s.offset); add(s.amount); }
    }
    if (layer.follow_path) add(layer.follow_path->progress);
    if (layer.time_remap && layer.time_remap->animation) add(*layer.time_remap->animation);
    if (layer.shape) {
        for (const auto& s : layer.shape->imported_subpaths) {
            for (const auto* t : {&s.fill_color_animation, &s.fill_opacity_animation,
                                  &s.stroke_color_animation, &s.stroke_opacity_animation,
                                  &s.stroke_width_animation, &s.svg_opacity_animation,
                                  &s.svg_translation_animation, &s.svg_scale_animation,
                                  &s.svg_rotation_animation, &s.morph_animation})
                if (*t) add(**t);
            if (s.fill_gradient_animation) append_gradient_keys(out, *s.fill_gradient_animation, first, end, complete);
            if (s.lottie_group_transform) {
                const auto& t = *s.lottie_group_transform;
                for (const auto* x : {&t.position_animation, &t.scale_animation,
                                      &t.rotation_animation, &t.opacity_animation})
                    if (*x) add(**x);
            }
        }
        for (const auto& op : layer.shape->operators) {
            if (op.morph_animation) add(*op.morph_animation);
            if (op.trim_animation) add(*op.trim_animation);
        }
    }
    if (layer.path_mask) {
        const auto& m = *layer.path_mask;
        for (const auto* t : {&m.morph_animation, &m.feather_track, &m.expansion_track, &m.opacity_track})
            if (*t) add(**t);
    }
    for (const auto& m : layer.masks)
        for (const auto* t : {&m.morph_animation, &m.feather_track, &m.expansion_track, &m.opacity_track})
            if (*t) add(**t);
    for (const auto& e : layer.effect_param_tracks) add(e.track);
    for (const auto& e : layer.effects) if (e.time_curve) add(*e.time_curve);
    if (layer.text_counter) {
        const auto& c = layer.text_counter->counter;
        if (c.duration_frames.integral() > 0) {
            const auto cfirst = clamp_frame(c.start_frame, duration);
            const auto cend = clamp_frame(saturating_add(c.start_frame, c.duration_frames), duration);
            append_range(out, Frame{std::max(first.integral(), cfirst.integral())},
                         Frame{std::min(end.integral(), cend.integral())}, complete);
        }
    }
}
inline void append_camera_keys(std::vector<Frame>& out, const std::vector<CameraTrackPlan>& tracks,
                               Frame first, Frame end, bool& complete) {
    for (const auto& t : tracks) for (const auto& key : t.keyframes)
        if (key.frame >= first && key.frame < end) append_event(out, key.frame, complete);
}
[[nodiscard]] inline const char* camera_property_name(CameraPropertyPlan property) noexcept {
    switch (property) {
        case CameraPropertyPlan::PositionX: return "position_x";
        case CameraPropertyPlan::PositionY: return "position_y";
        case CameraPropertyPlan::PositionZ: return "position_z";
        case CameraPropertyPlan::RotationX: return "rotation_x";
        case CameraPropertyPlan::RotationY: return "rotation_y";
        case CameraPropertyPlan::RotationZ: return "rotation_z";
        case CameraPropertyPlan::FovDeg: return "fov_deg";
        case CameraPropertyPlan::Zoom: return "zoom";
        case CameraPropertyPlan::FocusDistance: return "focus_distance";
        case CameraPropertyPlan::Aperture: return "aperture";
        case CameraPropertyPlan::MaxBlur: return "max_blur";
    }
    return "unknown";
}
} // namespace temporal_rate_detail

[[nodiscard]] inline const char* temporal_rate_class_name(TemporalRateClass rate) noexcept {
    switch (rate) {
        case TemporalRateClass::Static: return "static";
        case TemporalRateClass::Cue: return "cue";
        case TemporalRateClass::KeyframeInterval: return "keyframe_interval";
        case TemporalRateClass::PerFrame: return "per_frame";
        case TemporalRateClass::Subframe: return "subframe";
    }
    return "static";
}

/// Pure conservative analysis only; it never compiles or schedules rendering.
[[nodiscard]] inline TemporalRateReport analyze_temporal_rates(const RenderPlan& plan) {
    using namespace temporal_rate_detail;
    TemporalRateReport report;
    report.layers.reserve(plan.layers.size());
    const Frame duration = plan.canvas.duration.integral() > 0 ? plan.canvas.duration : Frame{0};
    std::size_t skippable = 0;
    bool camera_shake_active = false;
    if (plan.camera_shake) {
        const auto& s = *plan.camera_shake;
        camera_shake_active = (s.amplitude_px != 0.0f || s.rotation_amplitude_deg != 0.0f) &&
            s.frequency_hz > 0.0f;
    }
    bool camera_animated = false, camera_per_frame = false;
    if (plan.camera_animation) for (const auto& t : *plan.camera_animation) {
        if (t.keyframes.size() < 2) continue;
        camera_animated = true;
        bool dense = true;
        for (std::size_t i = 1; i < t.keyframes.size(); ++i) {
            const auto previous = t.keyframes[i - 1].frame.integral();
            const auto current = t.keyframes[i].frame.integral();
            if (current <= previous || previous == std::numeric_limits<std::int64_t>::max() ||
                current != previous + 1) { dense = false; break; }
        }
        camera_per_frame = camera_per_frame || dense;
    }

    for (const auto& layer : plan.layers) {
        LayerRateReport item;
        item.layer_id = layer.id;
        const auto [first, end] = active_interval(layer, duration);
        append_property_schedules(item.properties, layer, first, end);
        auto facts = inspect_layer(layer);
        const bool camera_affects = layer.enable_3d || layer.type == LayerType::Mesh;
        if (camera_affects && plan.camera_animation) {
            for (const auto& track : *plan.camera_animation) {
                if (track.keyframes.size() < 2) continue;
                LayerRateReport::PropertySchedule property;
                property.path = std::string{"camera."} + camera_property_name(track.property);
                property.rate = TemporalRateClass::PerFrame;
                property.eval_frame_count = range_count(first, end);
                if (property.eval_frame_count <= kMaxReportedFrames)
                    append_range(property.eval_frames, first, end,
                                 property.eval_frames_complete);
                else
                    property.eval_frames_complete = false;
                if (!property.eval_frames_complete) property.eval_frames.clear();
                item.properties.push_back(std::move(property));
            }
        }
        if (camera_affects && (camera_shake_active || camera_per_frame ||
                               (layer.enable_3d && camera_animated)))
            facts.animated = facts.per_frame = true;
        else if (camera_affects && camera_animated) facts.animated = facts.per_frame = true;
        const bool subframe =
            (plan.temporal_motion_blur && active_motion_blur(*plan.temporal_motion_blur)) ||
            (layer.motion_blur && active_motion_blur(*layer.motion_blur));
        if (subframe) item.rate = TemporalRateClass::Subframe;
        else if (layer.type == LayerType::Video || layer.type == LayerType::Particles || facts.per_frame)
            item.rate = TemporalRateClass::PerFrame;
        else if (facts.animated) item.rate = TemporalRateClass::KeyframeInterval;
        else if (layer.start_frame || layer.duration_frames) item.rate = TemporalRateClass::Cue;

        if (item.rate == TemporalRateClass::Subframe) {
            // Temporal shutter samples are scheduled against the composition
            // timeline, including frames where a layer is transparent/inactive.
            item.eval_frame_count = range_count(Frame{0}, duration);
            append_range(item.eval_frames, Frame{0}, duration,
                         item.eval_frames_complete);
        } else if (item.rate == TemporalRateClass::PerFrame) {
            item.eval_frame_count = range_count(first, end);
            if (item.eval_frame_count <= kMaxReportedFrames)
                append_range(item.eval_frames, first, end, item.eval_frames_complete);
            else
                item.eval_frames_complete = false;
        } else if (item.rate == TemporalRateClass::Cue) {
            // A cue changes activity at its half-open interval boundaries.
            // The layer need not be reevaluated for every frame while its
            // static content remains active; the end event invalidates the
            // previous composite when the layer becomes inactive.
            if (first < end) {
                append_event(item.eval_frames, first, item.eval_frames_complete);
                if (end < duration)
                    append_event(item.eval_frames, end, item.eval_frames_complete);
            }
        } else if (item.rate == TemporalRateClass::KeyframeInterval) {
            const auto active_count = range_count(first, end);
            if (active_count > kMaxReportedFrames) {
                append_range(item.eval_frames, first, end,
                             item.eval_frames_complete);
            } else {
                append_layer_keys(item.eval_frames, layer, duration,
                                  item.eval_frames_complete);
                if (camera_affects && plan.camera_animation)
                    append_camera_keys(item.eval_frames, *plan.camera_animation,
                                       first, end, item.eval_frames_complete);
                if (camera_affects && camera_animated)
                    append_range(item.eval_frames, first, end,
                                 item.eval_frames_complete);
            }
        }

        std::sort(item.eval_frames.begin(), item.eval_frames.end());
        item.eval_frames.erase(std::unique(item.eval_frames.begin(), item.eval_frames.end()),
                               item.eval_frames.end());
        if (item.eval_frames.size() > kMaxReportedFrames) {
            item.eval_frames.resize(kMaxReportedFrames);
            item.eval_frames_complete = false;
        }
        if (item.rate == TemporalRateClass::Cue ||
            item.rate == TemporalRateClass::KeyframeInterval)
            item.eval_frame_count = static_cast<std::uint64_t>(item.eval_frames.size());
        append_builtin_property_schedules(
            item.properties, layer, item.rate, first, end, duration);
        if (item.rate == TemporalRateClass::Static || item.rate == TemporalRateClass::Cue) ++skippable;
        report.layers.push_back(std::move(item));
    }
    if (!report.layers.empty()) report.skippable_node_fraction =
        static_cast<double>(skippable) / static_cast<double>(report.layers.size());
    return report;
}

} // namespace chronon3d::render_plan
