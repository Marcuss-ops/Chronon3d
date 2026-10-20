#include "render_plan_compiler_detail.hpp"
#include "render_plan_compiler_animation_internal.hpp"
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/model/camera/dof.hpp>
#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/effects/effect_param_animation.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/scene/path_sampling.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace chronon3d::render_plan::detail {
FollowPathEvaluators make_follow_path_evaluators(
    const LayerPlan::FollowPathDriverPlan& follow,
    chronon3d::Vec3 base_position,
    chronon3d::Vec3 base_rotation,
    int canvas_width,
    int canvas_height,
    FrameRate fps) {
    std::vector<chronon3d::scene::PathSampleCommand> commands;
    commands.reserve(follow.path_commands.size());
    for (const auto& command : follow.path_commands) {
        chronon3d::scene::PathSampleCommand sample;
        switch (command.type) {
            case ShapePathCommandTypePlan::MoveTo:
                sample.type = chronon3d::scene::PathSampleCommandType::MoveTo; break;
            case ShapePathCommandTypePlan::LineTo:
                sample.type = chronon3d::scene::PathSampleCommandType::LineTo; break;
            case ShapePathCommandTypePlan::QuadraticTo:
                sample.type = chronon3d::scene::PathSampleCommandType::QuadraticTo; break;
            case ShapePathCommandTypePlan::CubicTo:
                sample.type = chronon3d::scene::PathSampleCommandType::CubicTo; break;
            case ShapePathCommandTypePlan::Close:
                sample.type = chronon3d::scene::PathSampleCommandType::Close; break;
        }
        sample.point = {command.point[0], command.point[1]};
        sample.control1 = {command.control1[0], command.control1[1]};
        sample.control2 = {command.control2[0], command.control2[1]};
        commands.push_back(sample);
    }
    auto table = std::make_shared<const chronon3d::scene::PathArcLengthTable>(commands);
    chronon3d::AnimatedValue<float> progress{0.0f};
    for (const auto& key : follow.progress.keyframes)
        add_animation_keyframe(progress, follow.progress, key, key.value.front(), fps);
    progress.loop_mode(loop_mode_value(follow.progress.loop_mode));
    progress.compute_roving();
    progress.compute_auto_beziers();

    FollowPathEvaluators result;
    result.position = [table, progress, clamp = follow.clamp_end,
                       canvas_width, canvas_height, z = base_position.z, fps](double frame) {
        const float u = progress.evaluate(chronon3d::SampleTime::from_frame(frame, fps));
        const auto point = table->point_at(u, clamp);
        return chronon3d::Vec3{point.x - static_cast<float>(canvas_width) * 0.5f,
                               point.y - static_cast<float>(canvas_height) * 0.5f, z};
    };
    if (follow.auto_orient) {
        result.rotation = [table, progress, offset = follow.offset_deg, base_rotation,
                           fps](double frame) {
            const float u = progress.evaluate(chronon3d::SampleTime::from_frame(frame, fps));
            const auto tangent = table->tangent_at(u);
            const float angle = glm::degrees(std::atan2(tangent.y, tangent.x)) + offset;
            return chronon3d::Vec3{base_rotation.x, base_rotation.y, angle};
        };
    }
    return result;
}

std::optional<float> stroke_dash_offset_at(const LayerPlan& layer, double frame, FrameRate fps) {
    if (!layer.animation || !layer.shape || !layer.shape->stroke) return std::nullopt;
    const AnimationTrackPlan* found=nullptr;
    for(const auto& track:layer.animation->tracks) if(track.property=="stroke_dash_offset") {
        if(found) throw std::runtime_error("layer has overlapping stroke_dash_offset tracks");
        found=&track;
    }
    if(!found) return std::nullopt;
    chronon3d::AnimatedValue<float> value{layer.shape->stroke->dash_offset};        for (const auto& key : found->keyframes)
            if (key.roving && (key.temporal_in_dx || key.temporal_in_dy ||
                key.temporal_out_dx || key.temporal_out_dy || key.bezier_curve))
                throw std::runtime_error("stroke dash-offset roving cannot use authored temporal handles");
    for(const auto& key:found->keyframes)
        add_animation_keyframe(value,*found,key,scalar_value(key,"stroke_dash_offset"),fps);
    value.compute_roving();
    value.compute_auto_beziers();
    const float result = value.evaluate(chronon3d::SampleTime::from_frame(frame, fps));
    if(!std::isfinite(result)) throw std::runtime_error("animated stroke dash offset must be finite");
    return result;
}

std::optional<float> stroke_width_at(
    const LayerPlan& layer, double frame, float base, FrameRate fps) {
    if (!layer.animation || !layer.shape || !layer.shape->stroke) return std::nullopt;
    const AnimationTrackPlan* found = nullptr;
    for (const auto& track : layer.animation->tracks) {
        if (track.property != "stroke_width") continue;
        if (found) throw std::runtime_error("layer has duplicate stroke_width animation tracks");
        found = &track;
    }
    if (!found || found->keyframes.empty()) return std::nullopt;
    chronon3d::AnimatedValue<float> value{base};
    for (const auto& key : found->keyframes)
        add_animation_keyframe(value, *found, key, scalar_value(key, "stroke_width"), fps);
    value.compute_roving();
    value.compute_auto_beziers();
    const float result = value.evaluate(chronon3d::SampleTime::from_frame(frame, fps));
    if (!std::isfinite(result) || result < 0.0f)
        throw std::runtime_error("animated stroke width must be finite and non-negative");
    return result;
}

std::optional<chronon3d::Color> stroke_color_at(
    const LayerPlan& layer, double frame, chronon3d::Color base, FrameRate fps) {
    if (!layer.animation || !layer.shape || !layer.shape->stroke) return std::nullopt;
    const AnimationTrackPlan* found = nullptr;
    for (const auto& track : layer.animation->tracks) {
        if (track.property != "stroke_color") continue;
        if (found) throw std::runtime_error("layer has duplicate stroke_color animation tracks");
        found = &track;
    }
    if (!found || found->keyframes.empty()) return std::nullopt;
    const auto color = [](const AnimationKeyframePlan& key) {
        if (key.value.size() != 4 || std::any_of(key.value.begin(), key.value.end(),
                [](float component) {
                    return !std::isfinite(component) || component < 0.0f || component > 1.0f;
                }))
            throw std::runtime_error("stroke_color keyframes require finite RGBA values in [0, 1]");
        return chronon3d::Color{key.value[0], key.value[1], key.value[2], key.value[3]};
    };
    chronon3d::AnimatedValue<chronon3d::Color> value{base};
    for (const auto& key : found->keyframes)
        add_animation_keyframe(value, *found, key, color(key), fps);
    value.compute_roving();
    value.compute_auto_beziers();
    return value.evaluate(chronon3d::SampleTime::from_frame(frame, fps));
}

std::optional<float> shadow_scalar_at(
    const LayerPlan& layer, std::string_view property, double frame,
    float base, FrameRate fps) {
    if (!layer.animation) return std::nullopt;
    const AnimationTrackPlan* found = nullptr;
    for (const auto& track : layer.animation->tracks) {
        if (track.property != property) continue;
        if (found) throw std::runtime_error("layer has duplicate shadow animation tracks");
        found = &track;
    }
    if (!found || found->keyframes.empty()) return std::nullopt;
    chronon3d::AnimatedValue<float> value{base};
    for (const auto& key : found->keyframes)
        add_animation_keyframe(value, *found, key, scalar_value(key, property), fps);
    value.compute_roving();
    value.compute_auto_beziers();
    const float result = value.evaluate(chronon3d::SampleTime::from_frame(frame, fps));
    if (!std::isfinite(result))
        throw std::runtime_error("animated shadow parameter must be finite");
    return result;
}

std::optional<chronon3d::Color> shadow_color_at(
    const LayerPlan& layer, std::string_view property, double frame,
    chronon3d::Color base, FrameRate fps) {
    if (!layer.animation) return std::nullopt;
    const AnimationTrackPlan* found = nullptr;
    for (const auto& track : layer.animation->tracks) {
        if (track.property != property) continue;
        if (found) throw std::runtime_error("layer has duplicate shadow color animation tracks");
        found = &track;
    }
    if (!found || found->keyframes.empty()) return std::nullopt;
    chronon3d::AnimatedValue<chronon3d::Color> value{base};
    for (const auto& key : found->keyframes) {
        if (key.value.size() != 4 || std::any_of(key.value.begin(), key.value.end(),
                [](float component) {
                    return !std::isfinite(component) || component < 0.0f || component > 1.0f;
                }))
            throw std::runtime_error("shadow color keyframes require finite RGBA values in [0, 1]");
        add_animation_keyframe(value, *found, key,
            chronon3d::Color{key.value[0], key.value[1], key.value[2], key.value[3]}, fps);
    }
    value.compute_roving();
    value.compute_auto_beziers();
    return value.evaluate(chronon3d::SampleTime::from_frame(frame, fps));
}

chronon3d::TemporalCurve1D make_effect_time_curve(const AnimationTrackPlan& track) {
    chronon3d::AnimatedValue<float> curve{0.0f};
    for (const auto& key : track.keyframes)
        if (key.roving && (key.temporal_in_dx || key.temporal_in_dy ||
            key.temporal_out_dx || key.temporal_out_dy || key.bezier_curve))
            throw std::runtime_error("effect-time roving cannot use authored temporal handles");
    for(const auto& key:track.keyframes)
        add_animation_keyframe(curve,track,key,scalar_value(key,"effect_time"));
    curve.compute_roving();
    curve.compute_auto_beziers();
    return chronon3d::TemporalCurve1D{std::move(curve)};
}

chronon3d::EasingCurve animation_keyframe_easing(
    const AnimationTrackPlan& track, const AnimationKeyframePlan& key) {
    return keyframe_easing(track, key);
}

std::optional<chronon3d::Color> fill_color_at(
    const LayerPlan& layer, double frame, chronon3d::Color base, FrameRate fps) {
    if (!layer.animation) return std::nullopt;
    const AnimationTrackPlan* track = nullptr;
    for (const auto& candidate : layer.animation->tracks) {
        if (candidate.property != "fill_color") continue;
        if (track) throw std::runtime_error("layer has duplicate fill_color animation tracks");
        track = &candidate;
    }
    if (!track || track->keyframes.empty()) return std::nullopt;

    std::vector<const AnimationKeyframePlan*> keys;
    keys.reserve(track->keyframes.size());
    for (const auto& key : track->keyframes) keys.push_back(&key);
    std::stable_sort(keys.begin(), keys.end(), [](const auto* left, const auto* right) {
        return left->frame < right->frame;
    });
    const auto color = [](const AnimationKeyframePlan& key) {
        if (key.value.size() != 4 || std::any_of(key.value.begin(), key.value.end(),
                [](float component) {
                    return !std::isfinite(component) || component < 0.0f || component > 1.0f;
                }))
            throw std::runtime_error("fill_color keyframes require finite RGBA values in [0, 1]");
        return chronon3d::Color{key.value[0], key.value[1], key.value[2], key.value[3]};
    };
    for (std::size_t index = 1; index < keys.size(); ++index)
        if (keys[index - 1]->frame == keys[index]->frame)
            throw std::runtime_error("fill_color keyframe frames must be unique");
    const auto easing = track->easing == "spring"
        ? chronon3d::EasingCurve{chronon3d::Easing::Linear}
        : track_easing(track->easing, track->bezier_curve);
    chronon3d::AnimatedValue<chronon3d::Color> animated{base};
    for (const auto* key : keys)
        add_animation_keyframe(animated, *track, *key, color(*key), fps);
    animated.compute_roving();
    animated.compute_auto_beziers();
    const auto sample_key_segment = [&](std::size_t index, double sample_frame) {
        const double start = static_cast<double>(keys[index]->frame.integral());
        const double end = static_cast<double>(keys[index + 1]->frame.integral());
        if (sample_frame >= end || end <= start) return color(*keys[index + 1]);
        return animated.evaluate(sample_frame);
    };
    chronon3d::Color sampled = base;
    const double first_frame = static_cast<double>(keys.front()->frame.integral());
    if (frame < first_frame) {
        const float t = first_frame <= 0.0 ? 1.0f : static_cast<float>(
            std::clamp(frame / first_frame, 0.0, 1.0));
        sampled = easing.preset == chronon3d::Easing::Hold && !easing.cubic
            ? base
            : (keys.front()->interpolation || keys.front()->easing
                ? animated.evaluate(frame)
                : chronon3d::lerp(base, color(*keys.front()),
                                  std::clamp(easing.apply(t), 0.0f, 1.0f)));
    } else if (frame >= static_cast<double>(keys.back()->frame.integral())) {
        sampled = color(*keys.back());
    } else {
        for (std::size_t index = 1; index < keys.size(); ++index) {
            const double start = static_cast<double>(keys[index - 1]->frame.integral());
            const double end = static_cast<double>(keys[index]->frame.integral());
            if (frame >= end) continue;
            const float t = end <= start ? 1.0f : static_cast<float>(
                std::clamp((frame - start) / (end - start), 0.0, 1.0));
            sampled = easing.preset == chronon3d::Easing::Hold && !easing.cubic
                ? color(*keys[index - 1])
                : ((keys[index - 1]->interpolation || keys[index - 1]->easing ||
                    keys[index]->interpolation || keys[index]->easing)
                    ? sample_key_segment(index - 1, frame)
                    : chronon3d::lerp(
                        color(*keys[index - 1]), color(*keys[index]),
                        std::clamp(easing.apply(t), 0.0f, 1.0f)));
            break;
        }
    }
    if (!std::isfinite(sampled.r) || !std::isfinite(sampled.g) ||
        !std::isfinite(sampled.b) || !std::isfinite(sampled.a))
        throw std::runtime_error("animated fill_color must be finite");
    return sampled;
}

namespace {

graphics::GradientDefinition interpolate_authored_gradient(
    const graphics::GradientDefinition& a,
    const graphics::GradientDefinition& b,
    float amount) {
    graphics::GradientDefinition result = a;
    std::vector<float> color_positions;
    color_positions.reserve(a.color_stops.size() + b.color_stops.size());
    for (const auto& stop : a.color_stops) color_positions.push_back(stop.position);
    for (const auto& stop : b.color_stops) color_positions.push_back(stop.position);
    std::sort(color_positions.begin(), color_positions.end());
    color_positions.erase(std::unique(color_positions.begin(), color_positions.end()),
                          color_positions.end());
    result.color_stops.clear();
    result.color_stops.reserve(color_positions.size());
    for (const float position : color_positions) {
        const auto left = graphics::detail::sample_color_stops(a.color_stops, position);
        const auto right = graphics::detail::sample_color_stops(b.color_stops, position);
        result.color_stops.push_back({position,
            chronon3d::lerp(left, right, amount).to_srgb()});
    }

    if (!a.opacity_stops.empty() || !b.opacity_stops.empty()) {
        std::vector<float> opacity_positions{0.0f, 1.0f};
        for (const auto& stop : a.opacity_stops) opacity_positions.push_back(stop.position);
        for (const auto& stop : b.opacity_stops) opacity_positions.push_back(stop.position);
        std::sort(opacity_positions.begin(), opacity_positions.end());
        opacity_positions.erase(std::unique(opacity_positions.begin(), opacity_positions.end()),
                                opacity_positions.end());
        result.opacity_stops.clear();
        result.opacity_stops.reserve(opacity_positions.size());
        for (const float position : opacity_positions) {
            const float left = a.opacity_stops.empty()
                ? 1.0f : graphics::detail::sample_opacity_stops(a.opacity_stops, position);
            const float right = b.opacity_stops.empty()
                ? 1.0f : graphics::detail::sample_opacity_stops(b.opacity_stops, position);
            result.opacity_stops.push_back({position, left + (right - left) * amount});
        }
    }

    const auto mix = [amount](float x, float y) { return x + (y - x) * amount; };
    result.start = {mix(a.start.x, b.start.x), mix(a.start.y, b.start.y)};
    result.end = {mix(a.end.x, b.end.x), mix(a.end.y, b.end.y)};
    result.center = {mix(a.center.x, b.center.x), mix(a.center.y, b.center.y)};
    result.radius = mix(a.radius, b.radius);
    result.angle = mix(a.angle, b.angle);
    return result;
}

}  // namespace

graphics::GradientDefinition gradient_animation_at(
    const graphics::GradientDefinition& base,
    const GradientAnimationPlan& track, double frame) {
    if (track.keyframes.empty()) return base;
    std::vector<const GradientKeyframePlan*> keys;
    keys.reserve(track.keyframes.size());
    for (const auto& key : track.keyframes) keys.push_back(&key);
    std::stable_sort(keys.begin(), keys.end(), [](const auto* left, const auto* right) {
        return left->frame < right->frame;
    });
    const auto easing = track_easing(track.easing, track.bezier_curve);
    const auto interpolate = [&](const graphics::GradientDefinition& a,
                                 const graphics::GradientDefinition& b,
                                 double start, double end) {
        const float t = static_cast<float>(std::clamp(
            (frame - start) / (end - start), 0.0, 1.0));
        const float eased = easing.apply(t);
        return interpolate_authored_gradient(a, b, std::clamp(eased, 0.0f, 1.0f));
    };
    const double first_frame = static_cast<double>(keys.front()->frame.integral());
    if (frame < first_frame) {
        if (first_frame <= 0.0) return keys.front()->value;
        if (easing.preset == chronon3d::Easing::Hold && !easing.cubic)
            return base;
        return interpolate(base, keys.front()->value, 0.0, first_frame);
    }
    for (std::size_t index = 1; index < keys.size(); ++index) {
        const double start = static_cast<double>(keys[index - 1]->frame.integral());
        const double end = static_cast<double>(keys[index]->frame.integral());
        if (frame < end) return interpolate(keys[index - 1]->value, keys[index]->value, start, end);
    }
    return keys.back()->value;
}

std::optional<graphics::GradientDefinition> fill_gradient_at(
    const LayerPlan& layer, double frame) {
    if (!layer.shape || !layer.shape->gradient) return std::nullopt;
    if (!layer.fill_gradient_animation) return *layer.shape->gradient;
    return gradient_animation_at(*layer.shape->gradient,
                                 *layer.fill_gradient_animation, frame);
}

namespace {

std::optional<chronon3d::Camera2_5D> make_camera_base(const RenderPlan& plan) {
    if (!plan.camera) return std::nullopt;

    const auto& source = *plan.camera;
    chronon3d::Camera2_5D camera;
    camera.enabled = true;
    camera.is_animated = false;
    camera.position = {source.position[0], source.position[1], source.position[2]};
    camera.rotation = {source.rotation_deg[0], source.rotation_deg[1], source.rotation_deg[2]};
    camera.fov_deg = source.fov_deg;
    camera.zoom = 1000.0f * source.zoom;
    camera.optics_mode = source.type == "orthographic"
        ? chronon3d::CameraOpticsMode::Zoom
        : (source.focal_length_mm > 0.0f
            ? chronon3d::CameraOpticsMode::PhysicalLens
            : chronon3d::CameraOpticsMode::FieldOfView);
    if (source.type == "orthographic") camera.zoom = 1000.0f * source.zoom;
    if (source.focal_length_mm > 0.0f) {
        camera.lens.focal_length = source.focal_length_mm;
        camera.lens.sensor_width = 35.0f;
        const float aspect = plan.canvas.width > 0 && plan.canvas.height > 0
            ? static_cast<float>(plan.canvas.width) / static_cast<float>(plan.canvas.height)
            : 16.0f / 9.0f;
        camera.lens.sensor_height = 35.0f / aspect;
    }
    const float authored_f_stop = source.dof_f_stop > 0.0f
        ? source.dof_f_stop : source.f_stop;
    if (authored_f_stop > 0.0f) camera.lens.f_stop = authored_f_stop;
    camera.dof.enabled = source.dof_enabled;
    camera.dof.focus_distance = source.dof_focus_distance;
    camera.dof.aperture = source.dof_aperture;
    camera.dof.max_blur = source.dof_max_blur;
    camera.dof.iris_blades = static_cast<std::uint32_t>(source.dof_iris_blades);
    camera.dof.highlight_gain = source.dof_highlight_gain;
    camera.dof.use_physical_model = source.focal_length_mm > 0.0f && authored_f_stop > 0.0f;
    if (source.dof_enabled) {
        camera.dof.focus_z = chronon3d::world_z_plane_for_camera_space_depth(
            camera, source.dof_focus_distance);
    }
    return camera;
}

} // namespace

CameraTrackEvaluator make_camera_evaluator(const RenderPlan& plan) {
    auto resolved = make_camera_base(plan);
    if (!resolved) return {};
    constexpr std::size_t kCameraPropertyCount =
        static_cast<std::size_t>(CameraPropertyPlan::FStop) + 1;
    using TrackValues = std::array<std::optional<chronon3d::AnimatedValue<float>>,
                                   kCameraPropertyCount>;
    TrackValues tracks{};
    const auto& source = *plan.camera;
    const auto base_for = [&](CameraPropertyPlan property) {
        switch (property) {
            case CameraPropertyPlan::PositionX: return resolved->position.x;
            case CameraPropertyPlan::PositionY: return resolved->position.y;
            case CameraPropertyPlan::PositionZ: return resolved->position.z;
            case CameraPropertyPlan::RotationX: return resolved->rotation.x;
            case CameraPropertyPlan::RotationY: return resolved->rotation.y;
            case CameraPropertyPlan::RotationZ: return resolved->rotation.z;
            case CameraPropertyPlan::FovDeg: return resolved->fov_deg;
            case CameraPropertyPlan::Zoom: return resolved->zoom;
            case CameraPropertyPlan::FocusDistance: return resolved->dof.focus_distance;
            case CameraPropertyPlan::Aperture: return resolved->dof.aperture;
            case CameraPropertyPlan::MaxBlur: return resolved->dof.max_blur;
            case CameraPropertyPlan::TargetX:
            case CameraPropertyPlan::TargetY:
            case CameraPropertyPlan::TargetZ: return 0.0f;
            case CameraPropertyPlan::FocalLengthMM: return resolved->lens.focal_length;
            case CameraPropertyPlan::FStop:
                return source.dof_f_stop > 0.0f ? source.dof_f_stop : resolved->lens.f_stop;
        }
        return 0.0f;
    };
    if (plan.camera_animation) {
        for (const auto& authored : *plan.camera_animation) {
            if (authored.keyframes.empty()) continue;
            const auto index = static_cast<std::size_t>(authored.property);
            if (index >= tracks.size())
                throw std::runtime_error("camera track property is outside the supported range");
            chronon3d::AnimatedValue<float> value{base_for(authored.property)};
            AnimationTrackPlan easing_track;
            easing_track.property = "camera";
            easing_track.easing = authored.easing;
            easing_track.bezier_curve = authored.bezier_curve;
            for (const auto& key : authored.keyframes) {
                if (key.value.size() != 1)
                    throw std::runtime_error("camera track keyframes require scalar values");
                add_animation_keyframe(value, easing_track, key, key.value.front(), plan.canvas.fps);
            }
            value.compute_roving();
            value.compute_auto_beziers();
            tracks[index] = std::move(value);
        }
    }
    const auto base_camera = *resolved;
    const auto fps = plan.canvas.fps;
    const bool animated = plan.camera_shake.has_value() ||
        std::any_of(tracks.begin(), tracks.end(), [](const auto& track) {
            return track.has_value();
        });
    return [base_camera, tracks = std::move(tracks), fps, animated,
            orthographic = source.type == "orthographic"](double frame) mutable {
    auto camera = base_camera;
    const auto sample = [&](CameraPropertyPlan property, float base) {
        const auto index = static_cast<std::size_t>(property);
        return tracks[index]
            ? tracks[index]->evaluate(chronon3d::SampleTime::from_frame(frame, fps))
            : base;
    };
    camera.position.x = sample(CameraPropertyPlan::PositionX, base_camera.position.x);
    camera.position.y = sample(CameraPropertyPlan::PositionY, base_camera.position.y);
    camera.position.z = sample(CameraPropertyPlan::PositionZ, base_camera.position.z);
    camera.rotation.x = sample(CameraPropertyPlan::RotationX, base_camera.rotation.x);
    camera.rotation.y = sample(CameraPropertyPlan::RotationY, base_camera.rotation.y);
    camera.rotation.z = sample(CameraPropertyPlan::RotationZ, base_camera.rotation.z);
    camera.fov_deg = sample(CameraPropertyPlan::FovDeg, base_camera.fov_deg);
    if (orthographic)
        camera.zoom = sample(CameraPropertyPlan::Zoom, camera.zoom);
    camera.dof.focus_distance = sample(CameraPropertyPlan::FocusDistance,
                                       camera.dof.focus_distance);
    camera.dof.aperture = sample(CameraPropertyPlan::Aperture, camera.dof.aperture);
    camera.dof.max_blur = sample(CameraPropertyPlan::MaxBlur, camera.dof.max_blur);
    camera.lens.focal_length = sample(CameraPropertyPlan::FocalLengthMM,
                                      camera.lens.focal_length);
    camera.lens.f_stop = sample(CameraPropertyPlan::FStop, camera.lens.f_stop);
    if (tracks[static_cast<std::size_t>(CameraPropertyPlan::TargetX)] ||
        tracks[static_cast<std::size_t>(CameraPropertyPlan::TargetY)] ||
        tracks[static_cast<std::size_t>(CameraPropertyPlan::TargetZ)]) {
        camera.point_of_interest_enabled = true;
        camera.point_of_interest.x = sample(CameraPropertyPlan::TargetX, 0.f);
        camera.point_of_interest.y = sample(CameraPropertyPlan::TargetY, 0.f);
        camera.point_of_interest.z = sample(CameraPropertyPlan::TargetZ, 0.f);
    }
    camera.is_animated = animated;
    if (camera.dof.enabled && !camera.dof.use_physical_model)
        camera.dof.focus_z = chronon3d::world_z_plane_for_camera_space_depth(
            camera, camera.dof.focus_distance);
    return camera;
    };
}

chronon3d::DepthRole depth_role_value(std::string_view value) {
    if (value == "far_background") return chronon3d::DepthRole::FarBackground;
    if (value == "background") return chronon3d::DepthRole::Background;
    if (value == "midground") return chronon3d::DepthRole::Midground;
    if (value == "subject") return chronon3d::DepthRole::Subject;
    if (value == "atmosphere") return chronon3d::DepthRole::Atmosphere;
    if (value == "foreground") return chronon3d::DepthRole::Foreground;
    if (value == "overlay") return chronon3d::DepthRole::Overlay;
    throw std::runtime_error("unsupported depth role: " + std::string(value));
}

chronon3d::FitMode fit_mode(FitMode value) {
    switch (value) {
        case FitMode::Contain: return chronon3d::FitMode::Contain;
        case FitMode::Stretch: return chronon3d::FitMode::Stretch;
        case FitMode::None: return chronon3d::FitMode::None;
        case FitMode::Cover: return chronon3d::FitMode::Cover;
    }
    return chronon3d::FitMode::Cover;
}

chronon3d::LayerTransitionSpec compile_transition(const TransitionPlan& plan, FrameRate fps) {
    chronon3d::LayerTransitionSpec spec;
    spec.transition_id = plan.kind;
    spec.duration = static_cast<double>(plan.duration_frames.integral()) * fps.den() / fps.num();
    spec.delay = static_cast<double>(plan.delay_frames.integral()) * fps.den() / fps.num();
    spec.easing = chronon3d::string_to_easing(plan.easing);
    spec.direction = plan.direction.empty()
        ? chronon3d::TransitionDirection::None
        : chronon3d::string_to_transition_direction(plan.direction);
    if (plan.kind == "crossfade") spec.parameters = chronon3d::CrossfadeParams{};
    else if (plan.kind == "dip_to_black") spec.parameters = chronon3d::DipToBlackParams{plan.dip_px};
    else if (plan.kind == "slide") spec.parameters = chronon3d::SlideParams{plan.distance};
    else if (plan.kind == "zoom") spec.parameters = chronon3d::ZoomParams{plan.zoom_amount};
    else if (plan.kind == "wipe_linear") spec.parameters = chronon3d::WipeLinearParams{};
    else if (plan.kind == "smooth_wipe") spec.parameters = chronon3d::SmoothWipeParams{plan.feather};
    else if (plan.kind == "circle_iris")
        spec.parameters = chronon3d::CircleIrisParams{{plan.center[0], plan.center[1]}, plan.feather};
    else if (plan.kind == "flash")
        spec.parameters = chronon3d::FlashParams{{plan.color[0], plan.color[1], plan.color[2], plan.color[3]}};
    else if (plan.kind == "procedural_remotion")
        spec.parameters = chronon3d::ProceduralRemotionParams{
            plan.seed,
            {plan.inner_color[0], plan.inner_color[1], plan.inner_color[2], plan.inner_color[3]},
            {plan.mid_color[0], plan.mid_color[1], plan.mid_color[2], plan.mid_color[3]},
            {plan.outer_color[0], plan.outer_color[1], plan.outer_color[2], plan.outer_color[3]}};
    else if (plan.kind == "remotion")
        spec.parameters = chronon3d::RemotionParams{plan.speed, plan.remotion_direction, plan.angle};
    return spec;
}

chronon3d::Vec3 light_direction_at(const LayerPlan& layer, double frame, FrameRate fps) {
    const auto& direction = layer.light->direction;
    chronon3d::AnimatedValue<chronon3d::Vec3> animated{
        chronon3d::Vec3{direction[0], direction[1], direction[2]}};
    const AnimationTrackPlan* found = nullptr;
    if (layer.animation) for (const auto& track : layer.animation->tracks) {
        if (track.property != "light_direction") continue;
        if (found) throw std::runtime_error("layer has duplicate light_direction tracks");
        found = &track;
        for (const auto& key : track.keyframes) {
            if (key.value.size() != 3)
                throw std::runtime_error("light_direction keyframes require three components");
            add_animation_keyframe(animated, track, key,
                chronon3d::Vec3{key.value[0], key.value[1], key.value[2]}, fps);
        }
    }
    animated.compute_roving();
    animated.compute_auto_beziers();
    const auto result = animated.evaluate(chronon3d::SampleTime::from_frame(frame, fps));
    const float length_squared = glm::dot(result, result);
    if (!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z) ||
        !std::isfinite(length_squared) || length_squared <= 1.0e-8f)
        throw std::runtime_error("animated light_direction must be finite and non-zero");
    return result;
}

float evaluate_scalar_track(const AnimationTrackPlan& track, double frame,
                            float base, FrameRate fps) {
    chronon3d::AnimatedValue<float> animated{base};
    for (const auto& key : track.keyframes)
        add_animation_keyframe(animated, track, key, scalar_value(key, track.property), fps);
    animated.compute_roving();
    animated.compute_auto_beziers();
    return animated.evaluate(chronon3d::SampleTime::from_frame(frame, fps));
}


std::optional<chronon3d::DirectionalBlurParams> motion_blur_params_at(
    const LayerPlan& layer, double frame, FrameRate fps,
    const FollowPathEvaluators* precompiled_follow_path) {
    if (!layer.motion_blur || (!layer.animation &&
        !(precompiled_follow_path && precompiled_follow_path->position))) return std::nullopt;
    const double half_exposure_frames =
        static_cast<double>(layer.motion_blur->shutter_angle) / 720.0;
    chronon3d::Vec3 delta{};
    if (precompiled_follow_path && precompiled_follow_path->position) {
        delta = precompiled_follow_path->position(frame + half_exposure_frames) -
                precompiled_follow_path->position(frame - half_exposure_frames);
    } else {
    const chronon3d::Vec3 base{layer.position[0], layer.position[1], layer.position[2]};
    chronon3d::AnimatedValue<chronon3d::Vec3> position{base};
    const auto data = position_animation_data(layer, base);
    if (data.keyframes.empty()) return std::nullopt;
    for (const auto& key : data.keyframes) {
        if (key.timing.interpolation == "bezier" && !key.timing.bezier_curve) {
            position.add_vector_temporal_keyframe(key.frame, key.value,
                chronon3d::InterpMode::Bezier,
                key.timing.temporal_in_dx.value_or(0.0f), key.temporal_in_value,
                key.timing.temporal_out_dx.value_or(0.0f), key.temporal_out_value,
                key.timing.roving, chronon3d::EasingCurve{chronon3d::Easing::Linear});
        } else {
            add_animation_keyframe(position, data.timing, key.timing, key.value, fps);
        }
    }
    position.compute_roving();
    position.compute_auto_beziers();
    delta = position.evaluate(frame + half_exposure_frames) -
            position.evaluate(frame - half_exposure_frames);
    }
    const float length = glm::length(glm::vec2{delta.x, delta.y});
    if (!std::isfinite(length) || length <= 1.0e-4f) return std::nullopt;
    chronon3d::DirectionalBlurParams result;
    result.angle = glm::degrees(std::atan2(delta.y, delta.x));
    result.length = length;
    result.samples = static_cast<int>(layer.motion_blur->samples);
    return result;
}

}
