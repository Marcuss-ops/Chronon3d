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
    // Owned so reports can safely cross job/session lifetimes.
    std::string layer_id;
    TemporalRateClass rate{TemporalRateClass::Static};
    // Output-frame evaluation events. Static layers have no events. Cue events
    // are every frame in the active cue interval; keyed tracks use their keyframes;
    // PerFrame/Subframe use every output frame in the plan.
    std::vector<Frame> eval_frames;
    // Event lists are capped to keep report memory bounded. The count is exact
    // when the schedule cardinality can be derived; otherwise it is the number
    // of listed events and eval_frames_complete is false.
    std::uint64_t eval_frame_count{0};
    bool eval_frames_complete{true};
};

struct TemporalRateReport {
    // Share of authored layers estimated skippable by a future Static/Cue schedule.
    double skippable_node_fraction{0.0};
    std::vector<LayerRateReport> layers;
};

namespace temporal_rate_detail {

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
        track.posterize_time || track.data_feed || track.audio_react) {
        return true;
    }
    const auto& keys = track.keyframes;
    if (keys.size() < 2) return false;
    for (std::size_t i = 1; i < keys.size(); ++i) {
        const auto previous = keys[i - 1].frame.integral();
        const auto current = keys[i].frame.integral();
        if (current <= previous || previous == std::numeric_limits<std::int64_t>::max() ||
            current != previous + 1) return false;
    }
    return true;
}

struct TrackFacts {
    bool animated{false};
    bool per_frame{false};
};

inline void inspect_track(const AnimationTrackPlan& track, TrackFacts& facts) noexcept {
    if (!track_has_samples(track)) return;
    facts.animated = true;
    facts.per_frame = facts.per_frame || track_is_per_frame(track);
}

inline void inspect_path_mask(const LayerPlan::PathMaskPlan& mask, TrackFacts& facts) noexcept {
    if (mask.morph_animation) inspect_track(*mask.morph_animation, facts);
    if (mask.feather_track) inspect_track(*mask.feather_track, facts);
    if (mask.expansion_track) inspect_track(*mask.expansion_track, facts);
    if (mask.opacity_track) inspect_track(*mask.opacity_track, facts);
}

[[nodiscard]] inline TrackFacts inspect_layer_tracks(const LayerPlan& layer) noexcept {
    TrackFacts facts;
    if (layer.animation) {
        for (const auto& track : layer.animation->tracks) inspect_track(track, facts);
    }
    if (layer.fill_gradient_animation &&
        layer.fill_gradient_animation->keyframes.size() > 1) {
        facts.animated = true;
        const auto& keys = layer.fill_gradient_animation->keyframes;
        bool gradient_per_frame = true;
        for (std::size_t i = 1; i < keys.size(); ++i) {
            const auto previous = keys[i - 1].frame.integral();
            const auto current = keys[i].frame.integral();
            if (current <= previous || previous == std::numeric_limits<std::int64_t>::max() ||
                current != previous + 1) {
                gradient_per_frame = false;
                break;
            }
        }
        facts.per_frame = facts.per_frame || gradient_per_frame;
    }
    for (const auto& animator : layer.text_animators) {
        for (const auto& track : animator.properties) inspect_track(track, facts);
        for (const auto& selector : animator.selectors) {
            inspect_track(selector.start, facts);
            inspect_track(selector.end, facts);
            inspect_track(selector.offset, facts);
            inspect_track(selector.amount, facts);
        }
    }
    if (layer.follow_path) inspect_track(layer.follow_path->progress, facts);
    if (layer.time_remap && layer.time_remap->animation)
        inspect_track(*layer.time_remap->animation, facts);
    if (layer.text_counter && layer.text_counter->counter.duration_frames.integral() > 0) {
        facts.animated = true;
        facts.per_frame = true;
    }
    if ((layer.transition_in && layer.transition_in->duration_frames.integral() > 0) ||
        (layer.transition_out && layer.transition_out->duration_frames.integral() > 0)) {
        facts.animated = true;
        facts.per_frame = true;
    }

    if (layer.path_mask) inspect_path_mask(*layer.path_mask, facts);
    for (const auto& mask : layer.masks) inspect_path_mask(mask, facts);
    for (const auto& effect : layer.effect_param_tracks)
        inspect_track(effect.track, facts);
    for (const auto& effect : layer.effects) {
        if (effect.time_curve) inspect_track(*effect.time_curve, facts);
        const bool procedural_time = effect.animated || effect.posterize_fps ||
            effect.evolution != 0.0f || effect.warp_evolution != 0.0f ||
            effect.warp_speed != 0.0f;
        if (procedural_time) {
            facts.animated = true;
            facts.per_frame = true;
        }
    }
    if (layer.shape) {
        if (layer.shape->field_drift &&
            (layer.shape->field_drift->x != 0.0f || layer.shape->field_drift->y != 0.0f)) {
            facts.animated = true;
            facts.per_frame = true;
        }
        for (const auto& subpath : layer.shape->imported_subpaths) {
            if (subpath.morph_animation) inspect_track(*subpath.morph_animation, facts);
        }
        for (const auto& operation : layer.shape->operators) {
            if (operation.morph_animation) inspect_track(*operation.morph_animation, facts);
            if (operation.trim_animation) inspect_track(*operation.trim_animation, facts);
        }
    }
    const auto has_stagger = [](const std::optional<LayerPlan::StaggerPlan>& stagger) {
        return stagger && stagger->per_item_frames > 0;
    };
    if (has_stagger(layer.stagger_group) ||
        (layer.flow_layout && has_stagger(layer.flow_layout->stagger)) ||
        (layer.grid_layout && has_stagger(layer.grid_layout->stagger))) {
        facts.animated = true;
        facts.per_frame = true;
    }
    return facts;
}

[[nodiscard]] inline Frame saturating_add(Frame lhs, Frame rhs) noexcept {
    const auto a = lhs.integral();
    const auto b = rhs.integral();
    if (b > 0 && a > std::numeric_limits<std::int64_t>::max() - b)
        return Frame{std::numeric_limits<std::int64_t>::max()};
    if (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b)
        return Frame{std::numeric_limits<std::int64_t>::min()};
    return Frame{a + b};
}

[[nodiscard]] inline Frame bounded_frame(Frame frame, Frame duration) noexcept {
    if (frame.integral() < 0) return Frame{0};
    if (frame >= duration) return duration;
    return frame;
}

inline constexpr std::int64_t kMaxReportedFrames = 1'000'000;

[[nodiscard]] inline std::pair<Frame, Frame> active_interval(
    const LayerPlan& layer, Frame duration) noexcept {
    const Frame raw_start = layer.start_frame.value_or(Frame{0});
    const Frame start = bounded_frame(raw_start, duration);
    const Frame raw_end = layer.duration_frames
        ? saturating_add(raw_start, *layer.duration_frames)
        : duration;
    return {start, bounded_frame(raw_end, duration)};
}

inline std::uint64_t interval_count(Frame start, Frame end) noexcept {
    const auto first = start.integral();
    const auto last = end.integral();
    if (last <= first) return 0;
    return static_cast<std::uint64_t>(last) - static_cast<std::uint64_t>(first);
}

inline bool append_frame_range(std::vector<Frame>& frames, Frame start, Frame end,
                               bool& complete) {
    const auto count = interval_count(start, end);
    if (count == 0) return true;
    const auto cap = static_cast<std::size_t>(kMaxReportedFrames);
    const auto available = cap - std::min(frames.size(), cap);
    const auto append_count = std::min<std::uint64_t>(count, available);
    frames.reserve(frames.size() + static_cast<std::size_t>(append_count));
    for (std::uint64_t i = 0; i < append_count; ++i)
        frames.push_back(Frame{start.integral() + static_cast<std::int64_t>(i)});
    if (append_count != count) complete = false;
    return complete;
}

inline void append_event_frame(std::vector<Frame>& frames, Frame frame,
                               bool& complete) {
    if (frames.size() >= static_cast<std::size_t>(kMaxReportedFrames)) {
        complete = false;
        return;
    }
    frames.push_back(frame);
}

inline void append_keyframes(std::vector<Frame>& frames, const LayerPlan& layer,
                             Frame duration, bool& complete) {
    const auto [active_start, active_end] = active_interval(layer, duration);
    const auto append = [&frames, active_start, active_end, &complete](
                            const AnimationTrackPlan& track) {
        for (const auto& key : track.keyframes) {
            if (key.frame >= active_start && key.frame < active_end)
                append_event_frame(frames, key.frame, complete);
        }
    };
    if (layer.animation) {
        for (const auto& track : layer.animation->tracks) append(track);
    }
    if (layer.fill_gradient_animation) {
        for (const auto& key : layer.fill_gradient_animation->keyframes) {
            if (key.frame >= active_start && key.frame < active_end)
                append_event_frame(frames, key.frame, complete);
        }
    }
    for (const auto& animator : layer.text_animators) {
        for (const auto& track : animator.properties) append(track);
        for (const auto& selector : animator.selectors) {
            append(selector.start);
            append(selector.end);
            append(selector.offset);
            append(selector.amount);
        }
    }
    if (layer.follow_path) append(layer.follow_path->progress);
    if (layer.text_counter) {
        const auto& counter = layer.text_counter->counter;
        if (counter.duration_frames.integral() > 0 && duration.integral() > 0) {
            const auto [active_start, active_end] = active_interval(layer, duration);
            const auto counter_start = bounded_frame(counter.start_frame, duration);
            const auto counter_end = bounded_frame(
                saturating_add(counter.start_frame, counter.duration_frames), duration);
            const Frame first{std::max(active_start.integral(), counter_start.integral())};
            const Frame last{std::min(active_end.integral(), counter_end.integral())};
            (void)append_frame_range(frames, first, last, complete);
        }
    }
    if (layer.time_remap && layer.time_remap->animation)
        append(*layer.time_remap->animation);
    if (layer.shape) {
        for (const auto& subpath : layer.shape->imported_subpaths)
            if (subpath.morph_animation) append(*subpath.morph_animation);
    }
    const auto append_mask = [&append](const LayerPlan::PathMaskPlan& mask) {
        if (mask.morph_animation) append(*mask.morph_animation);
        if (mask.feather_track) append(*mask.feather_track);
        if (mask.expansion_track) append(*mask.expansion_track);
        if (mask.opacity_track) append(*mask.opacity_track);
    };
    if (layer.path_mask) append_mask(*layer.path_mask);
    for (const auto& mask : layer.masks) append_mask(mask);
    for (const auto& effect : layer.effect_param_tracks) append(effect.track);
    for (const auto& effect : layer.effects)
        if (effect.time_curve) append(*effect.time_curve);
    if (layer.shape) {
        for (const auto& operation : layer.shape->operators) {
            if (operation.morph_animation) append(*operation.morph_animation);
            if (operation.trim_animation) append(*operation.trim_animation);
        }
    }
    std::sort(frames.begin(), frames.end());
    frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
    if (frames.size() > static_cast<std::size_t>(kMaxReportedFrames)) {
        frames.resize(static_cast<std::size_t>(kMaxReportedFrames));
        complete = false;
    }
}

[[nodiscard]] inline bool has_text_animator(const LayerPlan& layer) noexcept {
    return inspect_layer_tracks(layer).animated;
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

/// Classify each layer by its fastest required temporal rate without compiling,
/// sampling, mutating, or otherwise executing the render plan.
[[nodiscard]] inline TemporalRateReport analyze_temporal_rates(const RenderPlan& plan) {
    TemporalRateReport report;
    report.layers.reserve(plan.layers.size());
    const Frame duration = plan.canvas.duration.integral() > 0
        ? plan.canvas.duration : Frame{0};
    std::size_t skippable = 0;
    const bool global_subframe = plan.temporal_motion_blur &&
        temporal_rate_detail::active_motion_blur(*plan.temporal_motion_blur);
    bool camera_per_frame = plan.camera_shake &&
        (plan.camera_shake->amplitude_px != 0.0f ||
         plan.camera_shake->rotation_amplitude_deg != 0.0f) &&
        plan.camera_shake->frequency_hz > 0.0f;
    bool camera_keyframed = false;
    bool camera_keys_per_frame = false;
    if (plan.camera_animation) {
        for (const auto& track : *plan.camera_animation) {
            if (track.keyframes.size() < 2) continue;
            camera_keyframed = true;
            bool consecutive = true;
            for (std::size_t i = 1; i < track.keyframes.size(); ++i) {
                const auto previous = track.keyframes[i - 1].frame.integral();
                const auto current = track.keyframes[i].frame.integral();
                if (current <= previous ||
                    previous == std::numeric_limits<std::int64_t>::max() ||
                    current != previous + 1) {
                    consecutive = false;
                    break;
                }
            }
            camera_keys_per_frame = camera_keys_per_frame || consecutive;
        }
    }

    for (const auto& layer : plan.layers) {
        LayerRateReport layer_report{layer.id, TemporalRateClass::Static, {}};
        auto facts = temporal_rate_detail::inspect_layer_tracks(layer);
        const bool camera_affects_layer = layer.enable_3d || layer.type == LayerType::Mesh;
        if (camera_affects_layer && (camera_per_frame || camera_keys_per_frame)) {
            facts.animated = true;
            facts.per_frame = true;
        } else if (camera_affects_layer && camera_keyframed) {
            facts.animated = true;
        }
        const bool layer_subframe = layer.motion_blur &&
            temporal_rate_detail::active_motion_blur(*layer.motion_blur);
        if (global_subframe || layer_subframe) {
            layer_report.rate = TemporalRateClass::Subframe;
            const auto [active_start, active_end] =
                temporal_rate_detail::active_interval(layer, duration);
            layer_report.eval_frame_count = temporal_rate_detail::interval_count(
                active_start, active_end);
            (void)temporal_rate_detail::append_frame_range(
                layer_report.eval_frames, active_start, active_end,
                layer_report.eval_frames_complete);
        } else if (layer.type == LayerType::Video ||
                   layer.type == LayerType::Particles || facts.per_frame) {
            layer_report.rate = TemporalRateClass::PerFrame;
            const auto [active_start, active_end] =
                temporal_rate_detail::active_interval(layer, duration);
            layer_report.eval_frame_count = temporal_rate_detail::interval_count(
                active_start, active_end);
            (void)temporal_rate_detail::append_frame_range(
                layer_report.eval_frames, active_start, active_end,
                layer_report.eval_frames_complete);
        } else if (facts.animated) {
            layer_report.rate = TemporalRateClass::KeyframeInterval;
            temporal_rate_detail::append_keyframes(layer_report.eval_frames, layer, duration,
                                                    layer_report.eval_frames_complete);
        } else if (layer.start_frame || layer.duration_frames) {
            // Any finite activation interval changes visibility over time; do
            // not describe image/shape cues as globally static.
            const auto [start, end] =
                temporal_rate_detail::active_interval(layer, duration);
            layer_report.rate = TemporalRateClass::Cue;
            const auto count = temporal_rate_detail::interval_count(start, end);
            layer_report.eval_frame_count = count;
            if (count > 0 && count <= static_cast<std::uint64_t>(
                    temporal_rate_detail::kMaxReportedFrames)) {
                (void)temporal_rate_detail::append_frame_range(
                    layer_report.eval_frames, start, end,
                    layer_report.eval_frames_complete);
            } else if (count > static_cast<std::uint64_t>(
                           temporal_rate_detail::kMaxReportedFrames)) {
                layer_report.eval_frames_complete = false;
            }
        }

        // Cue boundaries remain scheduling events even when the layer also has
        // keyframed animation. Merge them into its event list, then deduplicate.
        if (layer_report.rate != TemporalRateClass::PerFrame &&
            layer_report.rate != TemporalRateClass::Subframe &&
            (facts.animated || layer.type == LayerType::Video ||
             layer.type == LayerType::Particles) && (layer.start_frame || layer.duration_frames)) {
            const auto [cue_start, cue_end] =
                temporal_rate_detail::active_interval(layer, duration);
            const auto cue_count = temporal_rate_detail::interval_count(cue_start, cue_end);
            if (cue_count > 0 && cue_count <= static_cast<std::uint64_t>(
                    temporal_rate_detail::kMaxReportedFrames)) {
                (void)temporal_rate_detail::append_frame_range(
                    layer_report.eval_frames, cue_start, cue_end,
                    layer_report.eval_frames_complete);
            } else if (cue_count > static_cast<std::uint64_t>(
                           temporal_rate_detail::kMaxReportedFrames)) {
                layer_report.eval_frames_complete = false;
            }
        }
        std::sort(layer_report.eval_frames.begin(), layer_report.eval_frames.end());
        layer_report.eval_frames.erase(std::unique(layer_report.eval_frames.begin(),
            layer_report.eval_frames.end()), layer_report.eval_frames.end());
        if (layer_report.eval_frames_complete || layer_report.eval_frame_count == 0)
            layer_report.eval_frame_count = static_cast<std::uint64_t>(
                layer_report.eval_frames.size());

        if (layer_report.rate == TemporalRateClass::Static ||
            layer_report.rate == TemporalRateClass::Cue) {
            ++skippable;
        }
        report.layers.push_back(std::move(layer_report));
    }
    if (!report.layers.empty()) {
        report.skippable_node_fraction = static_cast<double>(skippable) /
            static_cast<double>(report.layers.size());
    }
    return report;
}

} // namespace chronon3d::render_plan
