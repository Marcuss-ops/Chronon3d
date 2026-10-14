#include "render_plan_compiler_detail.hpp"
#include "render_plan_compiler_animation_internal.hpp"
#include "enum_mapping.hpp"

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/animation/animation_property_kind.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/render_plan/spring_bake.hpp>
#include <chronon3d/scene/path_sampling.hpp>

#include <algorithm>
#include <optional>
#include <set>
#include <stdexcept>
#include <cmath>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

namespace chronon3d::render_plan::detail {
namespace {

void apply_animation_tracks(chronon3d::LayerBuilder& builder,
                            const LayerPlan& layer,
                            int canvas_width,
                            int canvas_height,
                            FrameRate fps,
                            const FollowPathEvaluators* precompiled_follow_path,
                            std::optional<chronon3d::Vec3> source_position = std::nullopt) {
    if (!layer.animation && !layer.follow_path) return;
    chronon3d::Vec3 base_position = source_position.value_or(
        chronon3d::Vec3{layer.position[0], layer.position[1], layer.position[2]});
    if ((layer.type == LayerType::Text || layer.type == LayerType::Shape ||
         layer.type == LayerType::Particles || layer.type == LayerType::Light) &&
        layer.position_dimensions >= 2) {
        // Primitive animation keyframes are offsets from the authored element
        // centre. Resolve that centre once before applying the offsets.
        base_position.x -= static_cast<float>(canvas_width) * 0.5f;
        base_position.y -= static_cast<float>(canvas_height) * 0.5f;
    }
    const chronon3d::Vec3 base_scale{layer.scale[0], layer.scale[1], layer.scale[2]};
    const chronon3d::Vec3 base_rotation{layer.rotation[0], layer.rotation[1], layer.rotation[2]};

    struct AxisTrackGroup {
        const AnimationTrackPlan* full = nullptr;
        const AnimationTrackPlan* x = nullptr;
        const AnimationTrackPlan* y = nullptr;
        const AnimationTrackPlan* z = nullptr;
    };
    struct SkewTrackGroup {
        const AnimationTrackPlan* angle = nullptr;
        const AnimationTrackPlan* axis = nullptr;
        [[nodiscard]] bool empty() const { return !angle && !axis; }
    };

    AxisTrackGroup pos_tracks;
    AxisTrackGroup scale_tracks;
    AxisTrackGroup rot_tracks;
    const AnimationTrackPlan* opacity_track = nullptr;
    const AnimationTrackPlan* blur_track = nullptr;
    SkewTrackGroup skew_tracks;

    auto assign_track = [&](const AnimationTrackPlan*& slot, const AnimationTrackPlan& track) {
        if (slot)
            throw std::runtime_error("layer '" + layer.id + "' has duplicate " +
                                     track.property + " tracks");
        slot = &track;
    };
    auto assign_axis_track = [&](AxisTrackGroup& group, const AnimationTrackPlan& track,
                                 AnimationPropertyKind kind) {
        switch (kind) {
        case AnimationPropertyKind::PositionVector:
        case AnimationPropertyKind::UniformScale:
        case AnimationPropertyKind::RotationVector:
            assign_track(group.full, track);
            return;
        case AnimationPropertyKind::PositionXOffset:
        case AnimationPropertyKind::ScaleX:
        case AnimationPropertyKind::RotationX:
            assign_track(group.x, track);
            return;
        case AnimationPropertyKind::PositionYOffset:
        case AnimationPropertyKind::ScaleY:
        case AnimationPropertyKind::RotationY:
            assign_track(group.y, track);
            return;
        case AnimationPropertyKind::PositionZOffset:
        case AnimationPropertyKind::ScaleZ:
        case AnimationPropertyKind::RotationZ:
            assign_track(group.z, track);
            return;
        default:
            return;
        }
    };

    if (layer.animation) for (const auto& track : layer.animation->tracks) {
        const auto kind = animation_property_kind(track.property);
        switch (kind) {
        case AnimationPropertyKind::PositionVector:
        case AnimationPropertyKind::PositionXOffset:
        case AnimationPropertyKind::PositionYOffset:
        case AnimationPropertyKind::PositionZOffset:
            assign_axis_track(pos_tracks, track, kind);
            break;
        case AnimationPropertyKind::UniformScale:
        case AnimationPropertyKind::ScaleX:
        case AnimationPropertyKind::ScaleY:
        case AnimationPropertyKind::ScaleZ:
            assign_axis_track(scale_tracks, track, kind);
            break;
        case AnimationPropertyKind::RotationVector:
        case AnimationPropertyKind::RotationX:
        case AnimationPropertyKind::RotationY:
        case AnimationPropertyKind::RotationZ:
            assign_axis_track(rot_tracks, track, kind);
            break;
        case AnimationPropertyKind::Opacity:
            assign_track(opacity_track, track);
            break;
        case AnimationPropertyKind::Blur:
            assign_track(blur_track, track);
            break;
        case AnimationPropertyKind::Skew:
            assign_track(skew_tracks.angle, track);
            break;
        case AnimationPropertyKind::SkewAxis:
            assign_track(skew_tracks.axis, track);
            break;
        case AnimationPropertyKind::FillColor:
            // Paint tracks are sampled into the per-frame source node.
            break;
        case AnimationPropertyKind::StrokeColor:
        case AnimationPropertyKind::StrokeWidth:
            // Shape paint tracks are materialized by the per-frame paint path.
            break;
        default:
            if (is_per_frame_paint_property(track.property)) break;
            throw std::runtime_error("unsupported primitive animation property: " + track.property);
        }
    }

    if (pos_tracks.full && (pos_tracks.x || pos_tracks.y || pos_tracks.z))
        throw std::runtime_error("layer '" + layer.id + "' mixes 'position' with component tracks");
    if (scale_tracks.full && (scale_tracks.x || scale_tracks.y || scale_tracks.z))
        throw std::runtime_error("layer '" + layer.id + "' mixes 'scale' with component tracks");
    if (rot_tracks.full && (rot_tracks.x || rot_tracks.y || rot_tracks.z))
        throw std::runtime_error("layer '" + layer.id + "' mixes 'rotation' with component tracks");

    auto compile_component_track = [&](const AnimationTrackPlan* track, float fallback) {
        chronon3d::AnimatedValue<float> result{fallback};
        if (!track) return result;
        for (const auto& key : track->keyframes)
            add_animation_keyframe(result, *track, key, scalar_value(key, track->property), fps);
        result.compute_roving();
        result.compute_auto_beziers();
        return result;
    };

    auto prepare_composed_component_loops = [&](const AxisTrackGroup& group,
                                                auto& x, auto& y, auto& z,
                                                chronon3d::AnimatedValue<chronon3d::Vec3>& output) {
        const AnimationTrackPlan* reference = nullptr;
        for (const auto* track : {group.x, group.y, group.z}) {
            if (!track) continue;
            if (reference && track->loop_mode != reference->loop_mode)
                throw std::runtime_error("component tracks must share loop_mode");
            reference = track;
        }
        // Sample authored key values without remapping their endpoints. The
        // composed vector receives the loop mode and handles remapping once.
        if (x) x->loop_mode(chronon3d::LoopMode::Hold);
        if (y) y->loop_mode(chronon3d::LoopMode::Hold);
        if (z) z->loop_mode(chronon3d::LoopMode::Hold);
        if (reference) output.loop_mode(loop_mode_value(reference->loop_mode));
    };

    auto dominant_easing = [](const AnimationTrackPlan* x,
                              const AnimationTrackPlan* y,
                              const AnimationTrackPlan* z,
                              Frame f) -> chronon3d::EasingCurve {
        chronon3d::EasingCurve result{chronon3d::Easing::Linear};
        for (const auto* track : {x, y, z}) {
            if (!track) continue;
            for (const auto& key : track->keyframes) {
                if (key.frame != f) continue;
                const auto curve = keyframe_easing(*track, key);
                if (curve != chronon3d::Easing::Linear || result == chronon3d::Easing::Linear)
                    result = curve;
            }
        }
        return result;
    };

    auto add_component_vector_track = [&](chronon3d::AnimatedValue<chronon3d::Vec3>& animated,
                                          const AxisTrackGroup& group,
                                          const chronon3d::Vec3& fallback,
                                          bool additive) {
        const std::array<const AnimationTrackPlan*, 3> components{group.x, group.y, group.z};
        const AnimationTrackPlan* reference = nullptr;
        for (const auto* track : components) {
            if (!track) continue;
            for (const auto& key : track->keyframes)
                if (track->easing != "linear" || track->bezier_curve || key.interpolation ||
                    key.easing || key.bezier_curve || key.roving || key.temporal_in_dx ||
                    key.temporal_in_dy || key.temporal_out_dx || key.temporal_out_dy) {
                    reference = track;
                    break;
                }
            if (reference) break;
        }
        if (!reference) return false;
        if (reference->easing == "spring" || std::any_of(reference->keyframes.begin(),
                reference->keyframes.end(), [](const auto& key) { return key.easing == "spring"; }))
            return false;
        for (const auto* track : components) {
            if (!track) continue;
            if (track->loop_mode != reference->loop_mode)
                throw std::runtime_error("component tracks with per-key timing must share loop_mode");
            if (track->easing != reference->easing || track->bezier_curve != reference->bezier_curve ||
                track->keyframes.size() != reference->keyframes.size())
                throw std::runtime_error("component tracks with per-key timing must share keyframes and track easing");
            for (std::size_t index = 0; index < reference->keyframes.size(); ++index) {
                const auto& key = track->keyframes[index];
                const auto& ref = reference->keyframes[index];
                if (key.frame != ref.frame || key.interpolation != ref.interpolation ||
                    key.easing != ref.easing || key.bezier_curve != ref.bezier_curve ||
                    key.roving != ref.roving || key.temporal_in_dx != ref.temporal_in_dx ||
                    key.temporal_out_dx != ref.temporal_out_dx ||
                    key.temporal_in_dy.has_value() != ref.temporal_in_dy.has_value() ||
                    key.temporal_out_dy.has_value() != ref.temporal_out_dy.has_value())
                    throw std::runtime_error("component tracks with per-key timing must share keyframes and timing data");
            }
        }
        for (std::size_t index = 0; index < reference->keyframes.size(); ++index) {
            auto value = fallback;
            chronon3d::Vec3 in_dy{0.0f};
            chronon3d::Vec3 out_dy{0.0f};
            for (std::size_t axis = 0; axis < components.size(); ++axis) {
                const auto* track = components[axis];
                if (!track) continue;
                const auto scalar = scalar_value(track->keyframes[index], track->property);
                auto& component = axis == 0 ? value.x : (axis == 1 ? value.y : value.z);
                component = additive ? component + scalar : scalar;
                const auto& key = track->keyframes[index];
                if (key.temporal_in_dy) {
                    auto& in_component = axis == 0 ? in_dy.x : (axis == 1 ? in_dy.y : in_dy.z);
                    in_component = *key.temporal_in_dy;
                }
                if (key.temporal_out_dy) {
                    auto& out_component = axis == 0 ? out_dy.x : (axis == 1 ? out_dy.y : out_dy.z);
                    out_component = *key.temporal_out_dy;
                }
            }
            const auto& reference_key = reference->keyframes[index];
            if (reference_key.interpolation == "bezier" && !reference_key.bezier_curve) {
                animated.add_vector_temporal_keyframe(reference_key.frame, value,
                    chronon3d::InterpMode::Bezier,
                    reference_key.temporal_in_dx.value_or(0.0f), in_dy,
                    reference_key.temporal_out_dx.value_or(0.0f), out_dy,
                    reference_key.roving, chronon3d::EasingCurve{chronon3d::Easing::Linear});
            } else {
                AnimationKeyframePlan vector_key = reference_key;
                vector_key.value = {value.x, value.y, value.z};
            add_animation_keyframe(animated, *reference, vector_key, value, fps);
            }
        }
        animated.compute_roving();
        animated.compute_auto_beziers();
        return true;
    };

    if (pos_tracks.full) {
        auto& animated = builder.position_anim();
        animated.set(base_position);
        const bool has_axis_easing = std::any_of(pos_tracks.full->keyframes.begin(),
            pos_tracks.full->keyframes.end(), [](const auto& key) { return key.easing_xy.has_value(); });
        if (has_axis_easing) {
            auto x_track = *pos_tracks.full;
            auto y_track = *pos_tracks.full;
            x_track.property = "position_x";
            y_track.property = "position_y";
            const std::array<std::pair<AnimationTrackPlan*, std::size_t>, 2> axis_tracks{{
                {&x_track, 0}, {&y_track, 1}}};
            for (const auto& [track, axis] : axis_tracks)
                for (std::size_t index = 0; index < track->keyframes.size(); ++index) {
                    auto& key = track->keyframes[index];
                    if (key.value.size() != 2)
                        throw std::runtime_error("easing_xy requires 2D position keyframes");
                    key.value = {key.value[axis]};
                    if (key.easing_xy) key.easing = (*key.easing_xy)[axis];
                    key.easing_xy.reset();
                }
            animated.frame_evaluator([x_track = std::move(x_track),
                                      y_track = std::move(y_track),
                                      base_position, fps](double frame) {
                return chronon3d::Vec3{
                    evaluate_scalar_track(x_track, frame, base_position.x, fps),
                    evaluate_scalar_track(y_track, frame, base_position.y, fps),
                    base_position.z};
            });
        } else {
            for (const auto& key : pos_tracks.full->keyframes)
                add_animation_keyframe(animated, *pos_tracks.full, key,
                    vector_value(key, pos_tracks.full->property, base_position), fps);
        }
        animated.compute_roving();
        animated.compute_auto_beziers();
    } else if (pos_tracks.x || pos_tracks.y || pos_tracks.z) {
        auto& animated = builder.position_anim();
        animated.set(base_position);
        if (add_component_vector_track(animated, pos_tracks, base_position, true)) {
            // Timing metadata was preserved on the composed vector keyframes.
        } else {
        auto eval_x = pos_tracks.x ? std::optional{compile_component_track(pos_tracks.x, 0.0f)} : std::nullopt;
        auto eval_y = pos_tracks.y ? std::optional{compile_component_track(pos_tracks.y, 0.0f)} : std::nullopt;
        auto eval_z = pos_tracks.z ? std::optional{compile_component_track(pos_tracks.z, 0.0f)} : std::nullopt;
        prepare_composed_component_loops(pos_tracks, eval_x, eval_y, eval_z, animated);

        std::set<Frame> frames;
        for (const auto* t : {pos_tracks.x, pos_tracks.y, pos_tracks.z}) {
            if (t) {
                for (const auto& k : t->keyframes) frames.insert(k.frame);
            }
        }
        for (const auto* t : {pos_tracks.x, pos_tracks.y, pos_tracks.z})
            if (t && (t->easing == "spring" || std::any_of(t->keyframes.begin(), t->keyframes.end(),
                    [](const auto& key) { return key.easing == "spring"; })))
                for (Frame f{t->keyframes.front().frame.integral()}; f <= t->keyframes.back().frame; f += Frame{1})
                    frames.insert(f);

        for (const auto& f : frames) {
            chronon3d::Vec3 v = base_position;
            if (eval_x) v.x += eval_x->evaluate(static_cast<double>(f.integral()));
            if (eval_y) v.y += eval_y->evaluate(static_cast<double>(f.integral()));
            if (eval_z) v.z += eval_z->evaluate(static_cast<double>(f.integral()));
            animated.add_keyframe(f, v, dominant_easing(pos_tracks.x, pos_tracks.y, pos_tracks.z, f));
        }
        }
    }

    if (scale_tracks.full) {
        auto& animated = builder.scale_anim();
        animated.set(base_scale);
        for (const auto& key : scale_tracks.full->keyframes)
            add_animation_keyframe(animated, *scale_tracks.full, key,
                vector_value(key, scale_tracks.full->property, base_scale), fps);
        animated.compute_roving();
        animated.compute_auto_beziers();
    } else if (scale_tracks.x || scale_tracks.y || scale_tracks.z) {
        auto& animated = builder.scale_anim();
        animated.set(base_scale);
        if (add_component_vector_track(animated, scale_tracks, base_scale, false)) {
            // Timing metadata was preserved on the composed vector keyframes.
        } else {
        auto eval_x = scale_tracks.x ? std::optional{compile_component_track(scale_tracks.x, base_scale.x)} : std::nullopt;
        auto eval_y = scale_tracks.y ? std::optional{compile_component_track(scale_tracks.y, base_scale.y)} : std::nullopt;
        auto eval_z = scale_tracks.z ? std::optional{compile_component_track(scale_tracks.z, base_scale.z)} : std::nullopt;
        prepare_composed_component_loops(scale_tracks, eval_x, eval_y, eval_z, animated);

        std::set<Frame> frames;
        for (const auto* t : {scale_tracks.x, scale_tracks.y, scale_tracks.z}) {
            if (t) {
                for (const auto& k : t->keyframes) frames.insert(k.frame);
            }
        }
        for (const auto* t : {scale_tracks.x, scale_tracks.y, scale_tracks.z})
            if (t && (t->easing == "spring" || std::any_of(t->keyframes.begin(), t->keyframes.end(),
                    [](const auto& key) { return key.easing == "spring"; })))
                for (Frame f{t->keyframes.front().frame.integral()}; f <= t->keyframes.back().frame; f += Frame{1})
                    frames.insert(f);

        for (const auto& f : frames) {
            chronon3d::Vec3 v = base_scale;
            if (eval_x) v.x = eval_x->evaluate(static_cast<double>(f.integral()));
            if (eval_y) v.y = eval_y->evaluate(static_cast<double>(f.integral()));
            if (eval_z) v.z = eval_z->evaluate(static_cast<double>(f.integral()));
            animated.add_keyframe(f, v, dominant_easing(scale_tracks.x, scale_tracks.y, scale_tracks.z, f));
        }
        }
    }

    if (rot_tracks.full) {
        auto& animated = builder.rotate_anim();
        animated.set(base_rotation);
        for (const auto& key : rot_tracks.full->keyframes)
            add_animation_keyframe(animated, *rot_tracks.full, key,
                vector_value(key, rot_tracks.full->property, base_rotation), fps);
        animated.compute_roving();
        animated.compute_auto_beziers();
    } else if (rot_tracks.x || rot_tracks.y || rot_tracks.z) {
        auto& animated = builder.rotate_anim();
        animated.set(base_rotation);
        if (add_component_vector_track(animated, rot_tracks, base_rotation, false)) {
            // Timing metadata was preserved on the composed vector keyframes.
        } else {
        auto eval_x = rot_tracks.x ? std::optional{compile_component_track(rot_tracks.x, base_rotation.x)} : std::nullopt;
        auto eval_y = rot_tracks.y ? std::optional{compile_component_track(rot_tracks.y, base_rotation.y)} : std::nullopt;
        auto eval_z = rot_tracks.z ? std::optional{compile_component_track(rot_tracks.z, base_rotation.z)} : std::nullopt;
        prepare_composed_component_loops(rot_tracks, eval_x, eval_y, eval_z, animated);

        std::set<Frame> frames;
        for (const auto* t : {rot_tracks.x, rot_tracks.y, rot_tracks.z}) {
            if (t) {
                for (const auto& k : t->keyframes) frames.insert(k.frame);
            }
        }
        for (const auto* t : {rot_tracks.x, rot_tracks.y, rot_tracks.z})
            if (t && (t->easing == "spring" || std::any_of(t->keyframes.begin(), t->keyframes.end(),
                    [](const auto& key) { return key.easing == "spring"; })))
                for (Frame f{t->keyframes.front().frame.integral()}; f <= t->keyframes.back().frame; f += Frame{1})
                    frames.insert(f);

        for (const auto& f : frames) {
            chronon3d::Vec3 v = base_rotation;
            if (eval_x) v.x = eval_x->evaluate(static_cast<double>(f.integral()));
            if (eval_y) v.y = eval_y->evaluate(static_cast<double>(f.integral()));
            if (eval_z) v.z = eval_z->evaluate(static_cast<double>(f.integral()));
            animated.add_keyframe(f, v, dominant_easing(rot_tracks.x, rot_tracks.y, rot_tracks.z, f));
        }
        }
    }

    if (layer.auto_orient) {
        auto& position = builder.position_anim();
        const auto& position_keys = position.keyframes();
        if (position_keys.size() < 2)
            throw std::runtime_error("auto_orient requires at least two evaluated position keyframes");
        const auto first_frame = position_keys.front().frame.integral();
        const auto last_frame = position_keys.back().frame.integral();
        if (last_frame <= first_frame || last_frame - first_frame > 65535)
            throw std::runtime_error("auto_orient position track span must be in [1, 65535] frames");

        auto& rotation = builder.rotate_anim();
        rotation.set(base_rotation);
        rotation.loop_mode(position.loop_mode());
        float last_angle = base_rotation.z;
        constexpr double kTangentSample = 0.5;
        constexpr float kTangentEpsilon = 1.0e-6f;
        for (std::int64_t frame = first_frame; frame <= last_frame; ++frame) {
            const double left_frame = std::max(
                static_cast<double>(first_frame), static_cast<double>(frame) - kTangentSample);
            const double right_frame = std::min(
                static_cast<double>(last_frame), static_cast<double>(frame) + kTangentSample);
            const auto delta = position.evaluate(right_frame) - position.evaluate(left_frame);
            if (std::hypot(delta.x, delta.y) > kTangentEpsilon) {
                float angle = glm::degrees(std::atan2(delta.y, delta.x));
                while (angle - last_angle > 180.0f) angle -= 360.0f;
                while (angle - last_angle < -180.0f) angle += 360.0f;
                last_angle = angle;
            }
            rotation.add_keyframe(Frame{frame},
                chronon3d::Vec3{base_rotation.x, base_rotation.y, last_angle});
        }
        rotation.compute_roving();
        rotation.compute_auto_beziers();
    }

    if (opacity_track) {
        auto& animated = builder.opacity_anim();
        animated.set(layer.opacity.value_or(1.0f));
        for (const auto& key : opacity_track->keyframes)
            add_animation_keyframe(animated, *opacity_track, key,
                scalar_value(key, opacity_track->property), fps);
        animated.compute_roving();
        animated.compute_auto_beziers();
    }
    if (blur_track) {
        auto& animated = builder.blur_anim();
        animated.set(0.0f);
        for (const auto& key : blur_track->keyframes)
            add_animation_keyframe(animated, *blur_track, key,
                scalar_value(key, blur_track->property), fps);
        animated.compute_roving();
        animated.compute_auto_beziers();
    }
    if (!skew_tracks.empty()) {
        auto& animated = builder.skew_anim();
        animated.set(chronon3d::Vec3{layer.skew_angle_deg, layer.skew_axis_deg, 0.0f});
        const auto angle_track = skew_tracks.angle
            ? std::optional<AnimationTrackPlan>{*skew_tracks.angle} : std::nullopt;
        const auto axis_track = skew_tracks.axis
            ? std::optional<AnimationTrackPlan>{*skew_tracks.axis} : std::nullopt;
        const float base_x = layer.skew_angle_deg;
        const float base_y = layer.skew_axis_deg;
        animated.frame_evaluator([angle_track, axis_track, base_x, base_y, fps](double frame) {
            const float angle = std::clamp(
                angle_track ? evaluate_scalar_track(*angle_track, frame, base_x, fps) : base_x,
                -80.0f, 80.0f);
            const float axis = axis_track
                ? evaluate_scalar_track(*axis_track, frame, base_y, fps) : base_y;
            return chronon3d::Vec3{angle, axis, 0.0f};
        });
    }

    if (layer.follow_path) {
        const auto& follow = *layer.follow_path;
        const auto owned_evaluators = precompiled_follow_path
            ? FollowPathEvaluators{}
            : make_follow_path_evaluators(
                  follow, base_position, base_rotation, canvas_width, canvas_height, fps);
        const auto& evaluators = precompiled_follow_path
            ? *precompiled_follow_path : owned_evaluators;

        auto& position = builder.position_anim();
        position.set(base_position);
        position.frame_evaluator(evaluators.position);
        if (follow.auto_orient && !(rot_tracks.full || rot_tracks.x || rot_tracks.y || rot_tracks.z)) {
            auto& rotation = builder.rotate_anim();
            rotation.set(base_rotation);
            rotation.frame_evaluator(evaluators.rotation);
        }
    }
}

}  // namespace

chronon3d::Vec3 light_position_at(const LayerPlan& layer, double frame,
                                  chronon3d::Vec3 base, FrameRate fps) {
    // Reuse the canonical transform authoring sampler: vector positions are
    // absolute and underscore components are offsets. A zero-sized canvas
    // leaves light-source coordinates in canvas pixels, not layer space.
    chronon3d::LayerBuilder builder(layer.id,
        chronon3d::SampleTime::from_frame(frame, fps));
    builder.position(base);
    apply_animation_tracks(builder, layer, 0, 0, fps, nullptr, base);
    const auto result = builder.position_anim().evaluate(
        chronon3d::SampleTime::from_frame(frame, fps));
    if (!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z))
        throw std::runtime_error("animated light position must be finite");
    return result;
}

void apply_layer_primitives(chronon3d::LayerBuilder& builder,
                            const LayerPlan& layer,
                            int canvas_width,
                            int canvas_height,
                            FrameRate fps,
                            const FollowPathEvaluators* precompiled_follow_path) {
    if (layer.transition_in) builder.transition_in(compile_transition(*layer.transition_in, fps));
    if (layer.transition_out) builder.transition_out(compile_transition(*layer.transition_out, fps));
    if (layer.parent) builder.parent(*layer.parent);
    if (layer.track_matte) {
        switch (layer.track_matte->type) {
            case TrackMatteTypePlan::Alpha:
                builder.track_matte_alpha(layer.track_matte->source); break;
            case TrackMatteTypePlan::AlphaInverted:
                builder.track_matte_alpha_inverted(layer.track_matte->source); break;
            case TrackMatteTypePlan::Luma:
                builder.track_matte_luma(layer.track_matte->source); break;
            case TrackMatteTypePlan::LumaInverted:
                builder.track_matte_luma_inverted(layer.track_matte->source); break;
        }
    }
    if (layer.time_remap) {
        if (layer.time_remap->speed)
            builder.speed(*layer.time_remap->speed);
        if (layer.time_remap->reverse)
            builder.reverse();
        if (layer.time_remap->freeze_frame)
            builder.freeze_frame(*layer.time_remap->freeze_frame);
        if (layer.time_remap->animation) {
            auto& remap = builder.time_remap_anim();
            for (const auto& key : layer.time_remap->animation->keyframes)
                add_animation_keyframe(remap, *layer.time_remap->animation, key,
                                       scalar_value(key, "time_remap"));
            remap.compute_roving();
            remap.compute_auto_beziers();
        }
    }
    if (layer.enable_3d) builder.enable_3d();
    if (layer.screen_space) builder.screen_space();
    if (layer.depth_role) {
        builder.depth_role(depth_role_value(*layer.depth_role));
        builder.depth_offset(layer.depth_offset);
    }
    if (layer.start_frame) builder.from(*layer.start_frame);
    if (layer.duration_frames) builder.duration(*layer.duration_frames);
    if (layer.position_dimensions >= 2) {
        // Render-plan geometry (Text, Shape) is authored in canvas coordinates
        // with a top-left box origin, while LayerBuilder transforms are centred
        // on the canvas. Convert the box origin once; motion tracks remain
        // additive offsets from this resolved placement.
        if (layer.id.ends_with("__stroke")) {
            const float sw = layer.size_dimensions >= 2 ? layer.size[0] : 0.0f;
            const float sh = layer.size_dimensions >= 2 ? layer.size[1] : 0.0f;
            builder.position({
                layer.position[0] + static_cast<float>(canvas_width) - sw * 0.5f,
                layer.position[1] + static_cast<float>(canvas_height) - sh * 0.5f,
                layer.position[2]});
        } else if (layer.type == LayerType::Text || layer.type == LayerType::Shape) {
            // Screen-space shapes receive the screen-layer origin in their
            // graph placement; retain the authored primitive center-relative
            // offset rather than converting a top-left box origin.
            if (layer.type == LayerType::Shape && layer.screen_space) {
                builder.position({layer.position[0], layer.position[1], layer.position[2]});
            } else {
            builder.position({
                layer.position[0] - static_cast<float>(canvas_width) * 0.5f,
                layer.position[1] - static_cast<float>(canvas_height) * 0.5f,
                layer.position[2]});
            }
        } else {
            builder.position({layer.position[0], layer.position[1], layer.position[2]});
        }
    }
    if (layer.scale_dimensions >= 2)
        builder.scale({layer.scale[0], layer.scale[1], layer.scale[2]});
    if (layer.rotation_dimensions > 0)
        builder.rotate({layer.rotation[0], layer.rotation[1], layer.rotation[2]});
    builder.skew({layer.skew_angle_deg, layer.skew_axis_deg});
    if (layer.blend_mode) builder.blend(*layer.blend_mode);
    if (layer.opacity) builder.opacity(*layer.opacity);
    apply_animation_tracks(builder, layer, canvas_width, canvas_height, fps,
                           precompiled_follow_path);
}

}  // namespace chronon3d::render_plan::detail
