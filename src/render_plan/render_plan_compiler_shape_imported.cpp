#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/render_plan/easing_contract.hpp>
#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/assets/svg_path_loader.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/graphics/shape_style/stroke_style.hpp>
#include <chronon3d/scene/model/shape/path.hpp>
#include <chronon3d/scene/model/shape/path_operator.hpp>
#include "render_plan_compiler_detail.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace easing_name = chronon3d::render_plan::detail::easing_name;

namespace chronon3d::render_plan::detail {

void append_imported_svg_paths(chronon3d::LayerBuilder& builder,
                               const RenderPlan& plan,
                               const chronon3d::FrameContext& ctx,
                               const ShapeContentPlan& shape,
                               Color base_color) {
const auto attach_svg_groups = [&](const ImportedSubpathPlan& source) {
    std::vector<RenderNode::SvgGroupIsolation> groups;
    groups.reserve(source.svg_groups.size());
    for (const auto& group : source.svg_groups) {
        RenderNode::SvgGroupIsolation lowered{group.id, group.opacity};
        if (group.opacity_animation &&
            group.opacity_animation->keyframes.size() == 2) {
            const auto& first = group.opacity_animation->keyframes.front();
            const auto& last = group.opacity_animation->keyframes.back();
            if (first.value.size() == 1 && last.value.size() == 1)
                lowered.opacity_animation = RenderNode::SvgGroupIsolation::OpacityAnimation{
                    first.frame.integral(), last.frame.integral(),
                    first.value.front(), last.value.front(),
                    first.interpolation == easing_name::kHold};
        }
        groups.push_back(std::move(lowered));
    }
    if (groups.empty() && source.svg_group)
        groups.push_back({source.svg_group->id, source.svg_group->opacity});
    if (!groups.empty()) builder.svg_group_isolation(std::move(groups));
};
const auto attach_svg_masks = [&](const ImportedSubpathPlan& source) {
    if (source.svg_local_masks.empty()) return;
    std::vector<PathShape> masks;
    masks.reserve(source.svg_local_masks.size());
    for (const auto& commands : source.svg_local_masks) {
        PathShape mask;
        mask.fill = Fill::solid_color(Color::white());
        mask.stroke.enabled = false;
        for (const auto& command : commands) {
            const Vec2 point{command.point[0], command.point[1]};
            const Vec2 c1{command.control1[0], command.control1[1]};
            const Vec2 c2{command.control2[0], command.control2[1]};
            switch (command.type) {
                case ShapePathCommandTypePlan::MoveTo: mask.commands.push_back(PathCommand::move_to(point)); break;
                case ShapePathCommandTypePlan::LineTo: mask.commands.push_back(PathCommand::line_to(point)); break;
                case ShapePathCommandTypePlan::QuadraticTo: mask.commands.push_back(PathCommand::quadratic_to(c1, point)); break;
                case ShapePathCommandTypePlan::CubicTo: mask.commands.push_back(PathCommand::cubic_to(c1, c2, point)); break;
                case ShapePathCommandTypePlan::Close: mask.commands.push_back(PathCommand::close()); mask.closed = true; break;
            }
        }
        masks.push_back(std::move(mask));
    }
    builder.svg_element_masks(std::move(masks));
};
const auto sample_rgba = [&](const AnimationTrackPlan& track,
                             Color base_color) {
    AnimatedValue<Color> value{base_color};
    for (const auto& key : track.keyframes) {
        if (key.value.size() != 4 || std::any_of(key.value.begin(), key.value.end(),
                [](float component) {
                    return !std::isfinite(component) || component < 0.0f || component > 1.0f;
                }))
            throw std::runtime_error("imported paint color keyframes require bounded RGBA values");
        value.add_keyframe(key.frame,
            Color{key.value[0], key.value[1], key.value[2], key.value[3]},
            detail::animation_keyframe_easing(track, key));
    }
    value.compute_roving();
    value.compute_auto_beziers();
    return value.evaluate(ctx.effective_frame());
};
const auto sample_opacity = [&](const AnimationTrackPlan& track) {
    const float base = track.keyframes.empty() || track.keyframes.front().value.size() != 1
        ? 1.0f : track.keyframes.front().value.front();
    AnimatedValue<float> value{base};
    for (const auto& key : track.keyframes) {
        if (key.value.size() != 1 || !std::isfinite(key.value[0]) ||
            key.value[0] < 0.0f || key.value[0] > 1.0f)
            throw std::runtime_error("imported paint opacity keyframes require scalar values in [0, 1]");
        value.add_keyframe(key.frame, key.value[0],
            detail::animation_keyframe_easing(track, key));
    }
    value.compute_roving();
    value.compute_auto_beziers();
    return value.evaluate(ctx.effective_frame());
};
const auto sample_vec2 = [&](const AnimationTrackPlan& track, Vec2 base) {
    AnimatedValue<float> x{base.x}, y{base.y};
    for (const auto& key : track.keyframes) {
        if (key.value.size() != 2 ||
            !std::isfinite(key.value[0]) || !std::isfinite(key.value[1]))
            throw std::runtime_error("imported transform keyframes require finite Vec2 values");
        const auto easing = detail::animation_keyframe_easing(track, key);
        x.add_keyframe(key.frame, key.value[0], easing);
        y.add_keyframe(key.frame, key.value[1], easing);
    }
    x.compute_roving(); x.compute_auto_beziers();
    y.compute_roving(); y.compute_auto_beziers();
    return Vec2{x.evaluate(ctx.effective_frame()), y.evaluate(ctx.effective_frame())};
};
const auto sample_scalar = [&](const AnimationTrackPlan& track) {
    if (track.keyframes.empty() || track.keyframes.front().value.size() != 1)
        throw std::runtime_error("imported scalar animation has no valid keyframes");
    AnimatedValue<float> value{track.keyframes.front().value.front()};
    for (const auto& key : track.keyframes) {
        if (key.value.size() != 1 || !std::isfinite(key.value[0]))
            throw std::runtime_error("imported scalar keyframes require finite values");
        value.add_keyframe(key.frame, key.value.front(),
            detail::animation_keyframe_easing(track, key));
    }
    value.compute_roving();
    value.compute_auto_beziers();
    return value.evaluate(ctx.effective_frame());
};
for (std::size_t path_index = 0;
     path_index < shape.imported_subpaths.size(); ++path_index) {
    const auto& source = shape.imported_subpaths[path_index];
    PathParams imported_path;
    imported_path.color = base_color;
    imported_path.fill_rule = source.fill_rule == ShapeFillRulePlan::NonZero
        ? PathFillRule::NonZero : PathFillRule::EvenOdd;
    for (const auto& command : source.path) {
        const Vec2 point{command.point[0], command.point[1]};
        const Vec2 c1{command.control1[0], command.control1[1]};
        const Vec2 c2{command.control2[0], command.control2[1]};
        switch (command.type) {
            case ShapePathCommandTypePlan::MoveTo: imported_path.commands.push_back(PathCommand::move_to(point)); break;
            case ShapePathCommandTypePlan::LineTo: imported_path.commands.push_back(PathCommand::line_to(point)); break;
            case ShapePathCommandTypePlan::QuadraticTo: imported_path.commands.push_back(PathCommand::quadratic_to(c1, point)); break;
            case ShapePathCommandTypePlan::CubicTo: imported_path.commands.push_back(PathCommand::cubic_to(c1, c2, point)); break;
            case ShapePathCommandTypePlan::Close:
                imported_path.commands.push_back(PathCommand::close());
                imported_path.closed = true;
                break;
        }
    }
    std::vector<std::vector<ShapePathCommandPlan>> legacy_morph_target;
    if (source.morph_keyframe_targets.empty() && !source.morph_target.empty())
        legacy_morph_target.push_back(source.morph_target);
    const auto& morph_targets = source.morph_keyframe_targets.empty()
        ? legacy_morph_target : source.morph_keyframe_targets;
    if (source.morph_animation &&
        source.morph_animation->keyframes.size() >= 2 &&
        !morph_targets.empty() &&
        morph_targets.front().size() == imported_path.commands.size()) {
        AnimatedValue<float> progress{0.0f};
        for (const auto& key : source.morph_animation->keyframes) {
            if (key.value.size() != 1)
                throw std::runtime_error("imported morph animation requires scalar progress");
            progress.add_keyframe(key.frame, key.value.front(),
                detail::animation_keyframe_easing(*source.morph_animation, key));
        }
        const float morph_position = std::clamp(
            progress.evaluate(ctx.effective_frame()), 0.0f,
            static_cast<float>(morph_targets.size()));
        const auto target_index = std::min(
            static_cast<std::size_t>(morph_position), morph_targets.size() - 1);
        const float amount = std::clamp(
            morph_position - static_cast<float>(target_index), 0.0f, 1.0f);
        for (std::size_t command_index = 0;
             command_index < imported_path.commands.size(); ++command_index) {
            const auto& target = morph_targets[target_index][command_index];
            auto& command = imported_path.commands[command_index];
            const auto& from = target_index == 0
                ? source.path[command_index]
                : morph_targets[target_index - 1][command_index];
            const Vec2 from_point{from.point[0], from.point[1]};
            const Vec2 from_c1{from.control1[0], from.control1[1]};
            const Vec2 from_c2{from.control2[0], from.control2[1]};
            const Vec2 target_point{target.point[0], target.point[1]};
            const Vec2 target_c1{target.control1[0], target.control1[1]};
            const Vec2 target_c2{target.control2[0], target.control2[1]};
            command.p0 = glm::mix(from_point, target_point, amount);
            command.p1 = glm::mix(from_c1, target_c1, amount);
            command.p2 = glm::mix(from_c2, target_c2, amount);
        }
    }
    Vec2 lottie_group_position{0.0f, 0.0f};
    Vec2 lottie_group_scale{1.0f, 1.0f};
    Vec2 lottie_group_anchor{0.0f, 0.0f};
    float lottie_group_rotation = 0.0f;
    float lottie_group_opacity = 1.0f;
    if (source.lottie_group_transform) {
        const auto& group = *source.lottie_group_transform;
        lottie_group_position = group.position_animation
            ? sample_vec2(*group.position_animation,
                Vec2{group.position[0], group.position[1]})
            : Vec2{group.position[0], group.position[1]};
        lottie_group_scale = group.scale_animation
            ? sample_vec2(*group.scale_animation, Vec2{group.scale[0], group.scale[1]})
            : Vec2{group.scale[0], group.scale[1]};
        lottie_group_anchor = {group.anchor[0], group.anchor[1]};
        lottie_group_rotation = group.rotation_animation
            ? sample_scalar(*group.rotation_animation) * 0.01745329251994329577f
            : group.rotation_degrees * 0.01745329251994329577f;
        lottie_group_opacity = group.opacity_animation
            ? sample_opacity(*group.opacity_animation) : group.opacity;
        if (std::abs(lottie_group_scale.x - lottie_group_scale.y) > 1e-5f ||
            lottie_group_opacity < 0.0f || lottie_group_opacity > 1.0f)
            throw std::runtime_error("animated Lottie groups require uniform scale and opacity in [0,1]");
        const float cosine = std::cos(lottie_group_rotation);
        const float sine = std::sin(lottie_group_rotation);
        const auto transform_point = [&](Vec2 point) {
            const float x = (point.x - lottie_group_anchor.x) * lottie_group_scale.x;
            const float y = (point.y - lottie_group_anchor.y) * lottie_group_scale.y;
            return Vec2{cosine * x - sine * y + lottie_group_position.x,
                        sine * x + cosine * y + lottie_group_position.y};
        };
        for (auto& command : imported_path.commands) {
            if (command.type == PathCommandType::Close) continue;
            command.p0 = transform_point(command.p0);
            if (command.type == PathCommandType::QuadraticTo ||
                command.type == PathCommandType::CubicTo)
                command.p1 = transform_point(command.p1);
            if (command.type == PathCommandType::CubicTo)
                command.p2 = transform_point(command.p2);
        }
    }
    Vec2 svg_scale{1.0f, 1.0f};
    if (!source.svg_transform_animations.empty()) {
        // Loader order is ancestor → descendant. Apply the
        // operations in reverse so points see the innermost
        // transform first, matching SVG matrix composition.
        for (auto transform = source.svg_transform_animations.rbegin();
             transform != source.svg_transform_animations.rend(); ++transform) {
            const Vec2 values = transform->operation == "rotate"
                ? Vec2{0.0f, 0.0f}
                : sample_vec2(transform->animation,
                    transform->operation == "scale" ? Vec2{1.0f, 1.0f}
                    : Vec2{0.0f, 0.0f});
            const float angle = transform->operation == "rotate"
                ? sample_scalar(transform->animation) * 0.01745329251994329577f
                : 0.0f;
            if (transform->operation == "scale") svg_scale *= values;
            const float cosine = std::cos(angle), sine = std::sin(angle);
            const auto apply_transform = [&](Vec2 point) {
                if (transform->operation == "translate")
                    return point + values;
                if (transform->operation == "scale") {
                    return point * values;
                }
                return Vec2{cosine * point.x - sine * point.y,
                            sine * point.x + cosine * point.y};
            };
            for (auto& command : imported_path.commands) {
                if (command.type == PathCommandType::Close) continue;
                command.p0 = apply_transform(command.p0);
                if (command.type == PathCommandType::QuadraticTo ||
                    command.type == PathCommandType::CubicTo)
                    command.p1 = apply_transform(command.p1);
                if (command.type == PathCommandType::CubicTo)
                    command.p2 = apply_transform(command.p2);
            }
        }
    } else if (source.svg_scale_animation || source.svg_rotation_animation ||
        source.svg_translation_animation) {
        svg_scale = source.svg_scale_animation
            ? sample_vec2(*source.svg_scale_animation, Vec2{1.0f, 1.0f})
            : Vec2{1.0f, 1.0f};
        const float angle = source.svg_rotation_animation
            ? sample_scalar(*source.svg_rotation_animation) * 0.01745329251994329577f : 0.0f;
        const float cosine = std::cos(angle), sine = std::sin(angle);
        const Vec2 translation = source.svg_translation_animation
            ? sample_vec2(*source.svg_translation_animation, Vec2{0.0f, 0.0f})
            : Vec2{0.0f, 0.0f};
        const auto transform_point = [&](Vec2 point) {
            const float x = point.x * svg_scale.x, y = point.y * svg_scale.y;
            return Vec2{cosine * x - sine * y + translation.x,
                        sine * x + cosine * y + translation.y};
        };
        for (auto& command : imported_path.commands) {
            if (command.type == PathCommandType::Close) continue;
            command.p0 = transform_point(command.p0);
            if (command.type == PathCommandType::QuadraticTo ||
                command.type == PathCommandType::CubicTo)
                command.p1 = transform_point(command.p1);
            if (command.type == PathCommandType::CubicTo)
                command.p2 = transform_point(command.p2);
        }
    }
    const float svg_opacity = source.svg_opacity_animation
        ? sample_opacity(*source.svg_opacity_animation) : 1.0f;
    if (source.fill_color) {
        const auto& rgba = *source.fill_color;
        Color fill_color{rgba[0], rgba[1], rgba[2], rgba[3]};
        if (source.fill_color_animation)
            fill_color = sample_rgba(*source.fill_color_animation, fill_color);
        if (source.fill_opacity_animation)
            fill_color.a *= sample_opacity(*source.fill_opacity_animation);
        fill_color.a *= svg_opacity;
        fill_color.a *= lottie_group_opacity;
        imported_path.fill = Fill::solid_color(fill_color);
    } else if (source.fill_gradient) {
        configure_shape_path_gradient_fill(imported_path.fill,
                                           *source.fill_gradient);
        auto gradient = source.fill_gradient_animation
            ? detail::gradient_animation_at(*source.fill_gradient,
                *source.fill_gradient_animation, ctx.effective_frame())
            : *source.fill_gradient;
        if (source.lottie_group_transform) {
            const float cosine = std::cos(lottie_group_rotation);
            const float sine = std::sin(lottie_group_rotation);
            const auto transform_point = [&](Vec2 point) {
                const float x = (point.x - lottie_group_anchor.x) * lottie_group_scale.x;
                const float y = (point.y - lottie_group_anchor.y) * lottie_group_scale.y;
                return Vec2{cosine * x - sine * y + lottie_group_position.x,
                            sine * x + cosine * y + lottie_group_position.y};
            };
            if (gradient.type == graphics::GradientType::Linear) {
                gradient.start = transform_point(gradient.start);
                gradient.end = transform_point(gradient.end);
            } else if (gradient.type == graphics::GradientType::Radial) {
                gradient.center = transform_point(gradient.center);
                gradient.radius *= std::abs(lottie_group_scale.x);
            }
            for (auto& stop : gradient.color_stops)
                stop.color.a *= lottie_group_opacity;
            for (auto& stop : gradient.opacity_stops)
                stop.opacity *= lottie_group_opacity;
        }
        imported_path.fill.gradient = std::move(gradient);
    } else {
        imported_path.fill.enabled = false;
    }
    if (source.stroke && source.stroke->solid_color) {
        const auto& rgba = *source.stroke->solid_color;
        Color stroke_color{rgba[0], rgba[1], rgba[2], rgba[3]};
        if (source.stroke_color_animation)
            stroke_color = sample_rgba(*source.stroke_color_animation, stroke_color);
        if (source.stroke_opacity_animation)
            stroke_color.a *= sample_opacity(*source.stroke_opacity_animation);
        stroke_color.a *= svg_opacity;
        stroke_color.a *= lottie_group_opacity;
        float stroke_width = source.stroke->width.value_or(1.0f);
        stroke_width *= std::abs(svg_scale.x) * std::abs(lottie_group_scale.x);
        if (source.stroke_width_animation)
            stroke_width = detail::evaluate_scalar_track(
                *source.stroke_width_animation, ctx.effective_frame(),
                stroke_width, plan.canvas.fps);
        auto imported_stroke = graphics::StrokeStyle::solid(
            stroke_color,
            stroke_width);
        imported_stroke.cap = source.stroke->cap == "round" ? LineCap::Round
            : source.stroke->cap == "square" ? LineCap::Square : LineCap::Butt;
        imported_stroke.join = source.stroke->join == "round" ? LineJoin::Round
            : source.stroke->join == "bevel" ? LineJoin::Bevel : LineJoin::Miter;
        imported_stroke.miter_limit = source.stroke->miter_limit;
        imported_stroke.dash_array = source.stroke->dash_array;
        for (auto& dash : imported_stroke.dash_array)
            dash *= std::abs(lottie_group_scale.x);
        imported_stroke.dash_offset = source.stroke->dash_offset *
            std::abs(lottie_group_scale.x);
        imported_path.stroke = imported_stroke.to_path_stroke();
    } else {
        imported_path.stroke.enabled = false;
    }
    if (shape.operators.empty()) {
        builder.path("svg_path_" + std::to_string(path_index),
                     std::move(imported_path));
        attach_svg_groups(source);
        attach_svg_masks(source);
        continue;
    }
    PathShape source_path;
    source_path.commands = imported_path.commands;
    source_path.stroke = imported_path.stroke;
    source_path.fill = imported_path.fill;
    source_path.closed = imported_path.closed;
    source_path.fill_rule = imported_path.fill_rule;
    std::vector<PathOperatorSpec> specs;
    for (const auto& operation : shape.operators) {
        if (!operation.target_id.empty()) {
            const bool has_exact_target = std::any_of(
                shape.imported_subpaths.begin(), shape.imported_subpaths.end(),
                [&](const ImportedSubpathPlan& candidate) {
                    return candidate.source_id == operation.target_id;
                });
            const bool matches = has_exact_target
                ? source.source_id == operation.target_id
                : std::find(source.source_group_ids.begin(),
                    source.source_group_ids.end(), operation.target_id) !=
                    source.source_group_ids.end();
            if (!matches) continue;
        }
        switch (operation.kind) {
            case PathOperatorKindPlan::Trim: {
                TrimPathParams trim{operation.trim_start, operation.trim_end,
                                    operation.trim_offset};
                if (operation.trim_animation) {
                    AnimatedValue<float> start{operation.trim_start};
                    AnimatedValue<float> end{operation.trim_end};
                    AnimatedValue<float> offset{operation.trim_offset};
                    for (const auto& key : operation.trim_animation->keyframes) {
                        const auto easing = detail::animation_keyframe_easing(
                            *operation.trim_animation, key);
                        if (!key.value.empty()) start.add_keyframe(key.frame, key.value[0], easing);
                        if (key.value.size() > 1) end.add_keyframe(key.frame, key.value[1], easing);
                        if (key.value.size() > 2) offset.add_keyframe(key.frame, key.value[2], easing);
                    }
                    trim.start = std::clamp(start.evaluate(ctx.effective_frame()), 0.0f, 1.0f);
                    trim.end = std::clamp(end.evaluate(ctx.effective_frame()), trim.start, 1.0f);
                    trim.offset = offset.evaluate(ctx.effective_frame());
                }
                specs.push_back({PathOperatorKind::Trim, trim, 0});
                break;
            }
            case PathOperatorKindPlan::Repeater: {
                const auto& r = operation.repeater;
                RepeaterParams params{r.copies, {r.position[0], r.position[1]},
                    {r.scale[0], r.scale[1]}, r.rotation_degrees,
                    {r.anchor[0], r.anchor[1]}, r.opacity_decay};
                if (operation.repeater_animation) {
                    AnimatedValue<float> px{r.position[0]}, py{r.position[1]};
                    AnimatedValue<float> sx{r.scale[0]}, sy{r.scale[1]};
                    AnimatedValue<float> rotation{r.rotation_degrees};
                    for (const auto& key : operation.repeater_animation->keyframes) {
                        const auto easing = detail::animation_keyframe_easing(
                            *operation.repeater_animation, key);
                        if (!key.value.empty()) px.add_keyframe(key.frame, key.value[0], easing);
                        if (key.value.size() > 1) py.add_keyframe(key.frame, key.value[1], easing);
                        if (key.value.size() > 2) sx.add_keyframe(key.frame, key.value[2], easing);
                        if (key.value.size() > 3) sy.add_keyframe(key.frame, key.value[3], easing);
                        if (key.value.size() > 4) rotation.add_keyframe(key.frame, key.value[4], easing);
                    }
                    params.position = {px.evaluate(ctx.effective_frame()),
                                       py.evaluate(ctx.effective_frame())};
                    params.scale = {sx.evaluate(ctx.effective_frame()),
                                    sy.evaluate(ctx.effective_frame())};
                    params.rotation_degrees = rotation.evaluate(ctx.effective_frame());
                }
                specs.push_back({PathOperatorKind::Repeater, params, 0});
                break;
            }
            case PathOperatorKindPlan::RoundCorners:
                specs.push_back({PathOperatorKind::RoundCorners,
                    RoundCornersParams{operation.radius}, 0}); break;
            case PathOperatorKindPlan::ZigZag:
                specs.push_back({PathOperatorKind::ZigZag,
                    ZigZagParams{operation.size, operation.points_per_unit,
                                 operation.smooth}, 0}); break;
            case PathOperatorKindPlan::OffsetPath:
                specs.push_back({PathOperatorKind::OffsetPath,
                    OffsetPathParams{operation.amount, operation.miter_limit}, 0}); break;
            case PathOperatorKindPlan::PuckerBloat:
                specs.push_back({PathOperatorKind::PuckerBloat,
                    PuckerBloatParams{operation.amount,
                        {operation.anchor[0], operation.anchor[1]}}, 0}); break;
            case PathOperatorKindPlan::Twist:
                specs.push_back({PathOperatorKind::Twist,
                    TwistParams{operation.degrees,
                        {operation.center[0], operation.center[1]}}, 0}); break;
            case PathOperatorKindPlan::WigglePath:
                specs.push_back({PathOperatorKind::WigglePath,
                    WigglePathParams{operation.amount, operation.frequency},
                    operation.seed}); break;
            case PathOperatorKindPlan::Morph: {
                if (operation.target_id.empty())
                    throw std::runtime_error(
                        "SVG morph operators require an exact element target id");
                MorphPathParams morph;
                morph.progress = operation.morph_progress;
                morph.target.reserve(operation.morph_target.size());
                for (const auto& command : operation.morph_target) {
                    const Vec2 point{command.point[0], command.point[1]};
                    const Vec2 c1{command.control1[0], command.control1[1]};
                    const Vec2 c2{command.control2[0], command.control2[1]};
                    switch (command.type) {
                        case ShapePathCommandTypePlan::MoveTo:
                            morph.target.push_back(PathCommand::move_to(point)); break;
                        case ShapePathCommandTypePlan::LineTo:
                            morph.target.push_back(PathCommand::line_to(point)); break;
                        case ShapePathCommandTypePlan::QuadraticTo:
                            morph.target.push_back(PathCommand::quadratic_to(c1, point)); break;
                        case ShapePathCommandTypePlan::CubicTo:
                            morph.target.push_back(PathCommand::cubic_to(c1, c2, point)); break;
                        case ShapePathCommandTypePlan::Close:
                            morph.target.push_back(PathCommand::close()); break;
                    }
                }
                if (operation.morph_animation)
                    morph.progress = std::clamp(
                        sample_scalar(*operation.morph_animation), 0.0f, 1.0f);
                specs.push_back({PathOperatorKind::Morph, std::move(morph), 0});
                break;
            }
            case PathOperatorKindPlan::Merge:
            case PathOperatorKindPlan::BooleanUnion: {
                if (operation.target_id.empty())
                    throw std::runtime_error(
                        "SVG merge/boolean-union requires an exact element target id");
                std::vector<std::vector<PathCommand>> planned_paths;
                planned_paths.reserve(operation.paths.size());
                for (const auto& planned_path : operation.paths) {
                    std::vector<PathCommand> subpath;
                    subpath.reserve(planned_path.size());
                    for (const auto& command : planned_path) {
                        const Vec2 point{command.point[0], command.point[1]};
                        const Vec2 c1{command.control1[0], command.control1[1]};
                        const Vec2 c2{command.control2[0], command.control2[1]};
                        switch (command.type) {
                            case ShapePathCommandTypePlan::MoveTo:
                                subpath.push_back(PathCommand::move_to(point)); break;
                            case ShapePathCommandTypePlan::LineTo:
                                subpath.push_back(PathCommand::line_to(point)); break;
                            case ShapePathCommandTypePlan::QuadraticTo:
                                subpath.push_back(PathCommand::quadratic_to(c1, point)); break;
                            case ShapePathCommandTypePlan::CubicTo:
                                subpath.push_back(PathCommand::cubic_to(c1, c2, point)); break;
                            case ShapePathCommandTypePlan::Close:
                                subpath.push_back(PathCommand::close()); break;
                        }
                    }
                    planned_paths.push_back(std::move(subpath));
                }
                if (operation.kind == PathOperatorKindPlan::BooleanUnion)
                    specs.push_back({PathOperatorKind::BooleanUnion,
                        BooleanUnionParams{std::move(planned_paths)}, 0});
                else
                    specs.push_back({PathOperatorKind::Merge,
                        MergeParams{std::move(planned_paths)}, 0});
                break;
            }
        }
    }
    auto expanded = apply_path_operators(source_path, specs);
    for (std::size_t copy_index = 0; copy_index < expanded.size(); ++copy_index) {
        PathParams copy = imported_path;
        copy.commands = std::move(expanded[copy_index].commands);
        copy.stroke = std::move(expanded[copy_index].stroke);
        copy.fill = std::move(expanded[copy_index].fill);
        copy.color.a *= expanded[copy_index].opacity;
        builder.path("svg_path_" + std::to_string(path_index) + "_" +
                     std::to_string(copy_index), std::move(copy));
        attach_svg_groups(source);
        attach_svg_masks(source);
    }
}

}

} // namespace chronon3d::render_plan::detail
