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
    facts.per_frame = facts.per_frame || track_is_per_frame(track);
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
        if (layer.shape->fill_gradient_animation) inspect_gradient(*layer.shape->fill_gradient_animation, facts);
        if (layer.shape->fill_color_animation) inspect_track(*layer.shape->fill_color_animation, facts);
        if (layer.shape->fill_opacity_animation) inspect_track(*layer.shape->fill_opacity_animation, facts);
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
inline void append_track_keys(std::vector<Frame>& out, const AnimationTrackPlan& t,
                              Frame first, Frame end, bool& complete) {
    for (const auto& key : t.keyframes)
        if (key.frame >= first && key.frame < end) append_event(out, key.frame, complete);
}
inline void append_gradient_keys(std::vector<Frame>& out, const GradientAnimationPlan& t,
                                 Frame first, Frame end, bool& complete) {
    for (const auto& key : t.keyframes)
        if (key.frame >= first && key.frame < end) append_event(out, key.frame, complete);
}

inline void append_layer_keys(std::vector<Frame>& out, const LayerPlan& layer,
                              Frame duration, bool& complete) {
    const auto [first, end] = active_interval(layer, duration);
    const auto add = [&](const AnimationTrackPlan& t) { append_track_keys(out, t, first, end, complete); };
    if (layer.animation) for (const auto& t : layer.animation->tracks) add(t);
    if (layer.fill_gradient_animation) append_gradient_keys(out, *layer.fill_gradient_animation, first, end, complete);
    for (const auto& a : layer.text_animators) {
        for (const auto& t : a.properties) add(t);
        for (const auto& s : a.selectors) { add(s.start); add(s.end); add(s.offset); add(s.amount); }
    }
    if (layer.follow_path) add(layer.follow_path->progress);
    if (layer.time_remap && layer.time_remap->animation) add(*layer.time_remap->animation);
    if (layer.shape) {
        if (layer.shape->fill_color_animation) add(*layer.shape->fill_color_animation);
        if (layer.shape->fill_opacity_animation) add(*layer.shape->fill_opacity_animation);
        if (layer.shape->fill_gradient_animation) append_gradient_keys(out, *layer.shape->fill_gradient_animation, first, end, complete);
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
        auto facts = inspect_layer(layer);
        const bool camera_affects = layer.enable_3d || layer.type == LayerType::Mesh;
        if (camera_affects && (camera_shake_active || camera_per_frame))
            facts.animated = facts.per_frame = true;
        else if (camera_affects && camera_animated) facts.animated = true;
        const bool subframe =
            (plan.temporal_motion_blur && active_motion_blur(*plan.temporal_motion_blur)) ||
            (layer.motion_blur && active_motion_blur(*layer.motion_blur));
        if (subframe) item.rate = TemporalRateClass::Subframe;
        else if (layer.type == LayerType::Video || layer.type == LayerType::Particles || facts.per_frame)
            item.rate = TemporalRateClass::PerFrame;
        else if (facts.animated) item.rate = TemporalRateClass::KeyframeInterval;
        else if (layer.start_frame || layer.duration_frames) item.rate = TemporalRateClass::Cue;

        if (item.rate == TemporalRateClass::PerFrame || item.rate == TemporalRateClass::Subframe ||
            item.rate == TemporalRateClass::Cue) {
            item.eval_frame_count = range_count(first, end);
            append_range(item.eval_frames, first, end, item.eval_frames_complete);
        } else if (item.rate == TemporalRateClass::KeyframeInterval) {
            append_layer_keys(item.eval_frames, layer, duration, item.eval_frames_complete);
            if (camera_affects && plan.camera_animation)
                append_camera_keys(item.eval_frames, *plan.camera_animation, first, end,
                                   item.eval_frames_complete);
            if (layer.start_frame || layer.duration_frames)
                append_range(item.eval_frames, first, end, item.eval_frames_complete);
        }
        std::sort(item.eval_frames.begin(), item.eval_frames.end());
        item.eval_frames.erase(std::unique(item.eval_frames.begin(), item.eval_frames.end()),
                               item.eval_frames.end());
        if (item.eval_frames.size() > kMaxReportedFrames) {
            item.eval_frames.resize(kMaxReportedFrames);
            item.eval_frames_complete = false;
        }
        if (item.rate == TemporalRateClass::KeyframeInterval)
            item.eval_frame_count = static_cast<std::uint64_t>(item.eval_frames.size());
        if (item.rate == TemporalRateClass::Static || item.rate == TemporalRateClass::Cue) ++skippable;
        report.layers.push_back(std::move(item));
    }
    if (!report.layers.empty()) report.skippable_node_fraction =
        static_cast<double>(skippable) / static_cast<double>(report.layers.size());
    return report;
}

} // namespace chronon3d::render_plan
