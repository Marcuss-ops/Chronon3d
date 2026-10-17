#include <chronon3d/scene/model/shape/path_operator.hpp>
#include <chronon3d/scene/model/shape/path.hpp>
#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/geometry/mesh.hpp>
#include <chronon3d/effects/cube_lut.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/effects/effect_param_animation.hpp>
#include <cmath>
#include <string>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/assets/svg_path_loader.hpp>

#include "render_plan_compiler_detail.hpp"
#include "effect_kind_coverage.hpp"
#include "audio_react_baker.hpp"
#include "audio_react_plan_baker.hpp"
#include "render_plan_effect_preparation.hpp"

#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/graphics/shape_style/stroke_style.hpp>
#include <chronon3d/scene/builders/text_run_materialization.hpp>
#include <chronon3d/text/text_run_builder.hpp>
#include <chronon3d/text/animated_text_document.hpp>
#include "../scene/camera/camera_v1/camera_program_sources.hpp"
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <chronon3d/timeline/composition_definition.hpp>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <locale>
#include <sstream>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace chronon3d::render_plan::detail {

void append_shape_layer(chronon3d::LayerBuilder& builder,
                        const LayerPlan& layer, const RenderPlan& plan,
                        const chronon3d::FrameContext& ctx) {
    if (!layer.shape)
        throw std::runtime_error("shape layer '" + layer.id + "' has no shape payload");
    const auto& shape = *layer.shape;
    const Vec2 size = layer.size_dimensions == 2
        ? Vec2{layer.size[0], layer.size[1]}
        : Vec2{static_cast<float>(plan.canvas.width),
               static_cast<float>(plan.canvas.height)};
    const Color authored_color{layer.color[0], layer.color[1],
                               layer.color[2], layer.color[3]};
    const Color base_color = detail::fill_color_at(
        layer, ctx.effective_frame(), authored_color,
        plan.canvas.fps).value_or(authored_color);
    const auto animated_gradient = detail::fill_gradient_at(
        layer, ctx.effective_frame());
    const auto& active_gradient = animated_gradient
        ? animated_gradient : shape.gradient;
    std::optional<graphics::Field2D> sampled_field = shape.field
        ? std::optional<graphics::Field2D>{*shape.field} : std::nullopt;
    if (sampled_field && shape.field_drift) {
        const double fps = static_cast<double>(plan.canvas.fps.numerator) /
            static_cast<double>(plan.canvas.fps.denominator);
        if (!std::isfinite(fps) || fps <= 0.0)
            throw std::runtime_error("animated Field2D requires a positive canvas frame rate");
        const double seconds = static_cast<double>(ctx.effective_frame()) / fps;
        try {
            sampled_field = sampled_field->drifted(seconds, *shape.field_drift);
        } catch (const std::invalid_argument& error) {
            throw std::runtime_error(error.what());
        }
    }
    if ((active_gradient || shape.gradient_mesh) && shape.field)
        throw std::runtime_error("shape '" + layer.id +
            "' cannot combine gradient/mesh fill with Field2D");
    std::optional<graphics::FillStyle> fill;
    if (active_gradient) {
        fill = graphics::FillStyle::solid(base_color);
        fill->gradient = *active_gradient;
    } else if (shape.gradient_mesh) {
        fill = graphics::FillStyle::solid(base_color);
        fill->gradient_mesh = *shape.gradient_mesh;
    } else if (shape.fill_color) {
        fill = graphics::FillStyle::solid(base_color);
    }
    if (shape.field) {
        fill = graphics::FillStyle::solid(base_color);
        fill->field = *sampled_field;
        fill->field_ramp = shape.field_ramp;
        fill->field_render_scale = shape.field_render_scale;
        fill->field_render_size = size;
    }
    graphics::StrokeStyle stroke;
    if (shape.stroke) {
        stroke.enabled = true;
        stroke.width = shape.stroke->width.value_or(1.0f);
        if (shape.stroke->solid_color) {
            const auto& rgba = *shape.stroke->solid_color;
            stroke.color = Color{rgba[0], rgba[1], rgba[2], rgba[3]};
        } else if (!shape.stroke->color.empty() && !shape.stroke->gradient) {
            const auto parsed = parse_hex_color(shape.stroke->color);
            if (!parsed)
                throw std::runtime_error("shape layer '" + layer.id + "' has invalid stroke color");
            stroke.color = *parsed;
        }
        stroke.gradient = shape.stroke->gradient;
        stroke.brush = shape.stroke->brush;
        stroke.cap = shape.stroke->cap == "round" ? LineCap::Round
            : shape.stroke->cap == "square" ? LineCap::Square : LineCap::Butt;
        stroke.join = shape.stroke->join == "round" ? LineJoin::Round
            : shape.stroke->join == "bevel" ? LineJoin::Bevel : LineJoin::Miter;
        stroke.taper_start = shape.stroke->taper_start;
        stroke.taper_end = shape.stroke->taper_end;
        stroke.trim_start = shape.stroke->trim_start.value_or(0.0f);
        stroke.trim_end = shape.stroke->trim_end.value_or(1.0f);
        stroke.dash_array = shape.stroke->dash_array;
        stroke.dash_offset = shape.stroke->dash_offset;
        if (const auto animated_width = detail::stroke_width_at(
                layer, ctx.effective_frame(), stroke.width,
                plan.canvas.fps))
            stroke.width = *animated_width;
        if (!shape.stroke->gradient) {
            if (const auto animated_color = detail::stroke_color_at(
                    layer, ctx.effective_frame(), stroke.color,
                    plan.canvas.fps))
                stroke.color = *animated_color;
        }
        if (const auto animated_offset=detail::stroke_dash_offset_at(
                layer, ctx.effective_frame(), plan.canvas.fps))
            stroke.dash_offset=*animated_offset;
    }
    if (layer.position_dimensions < 2 &&
        shape.type != ShapeContentTypePlan::Line &&
        shape.type != ShapeContentTypePlan::Path &&
        shape.type != ShapeContentTypePlan::Star &&
        shape.type != ShapeContentTypePlan::Polygon &&
        shape.type != ShapeContentTypePlan::Arrow &&
        shape.type != ShapeContentTypePlan::Arc &&
        shape.type != ShapeContentTypePlan::Grid &&
        shape.type != ShapeContentTypePlan::DotGrid &&
        shape.type != ShapeContentTypePlan::DeviceFrame) {
        builder.pin_to(Anchor::Center);
    }
    switch (shape.type) {
        case ShapeContentTypePlan::Rect: {
            RectParams params;
            params.size = size;
            params.color = base_color;
            params.fill = fill;
            params.stroke = stroke;
            builder.rect("shape", std::move(params));
            break;
        }
        case ShapeContentTypePlan::RoundedRect: {
            RoundedRectParams params;
            params.size = size;
            params.radius = shape.radius;
            params.corner_radius = shape.corner_radius;
            params.color = base_color;
            params.fill = fill;
            params.stroke = stroke;
            builder.rounded_rect("shape", std::move(params));
            break;
        }
        case ShapeContentTypePlan::Ellipse: {
            CircleParams params;
            params.radius = std::min(size.x, size.y) * 0.5f;
            params.color = base_color;
            params.fill = fill;
            params.stroke = stroke;
            builder.circle("shape", std::move(params));
            break;
        }
        case ShapeContentTypePlan::Line:
        case ShapeContentTypePlan::Path:
        case ShapeContentTypePlan::Star:
        case ShapeContentTypePlan::Polygon:
        case ShapeContentTypePlan::Arrow:
        case ShapeContentTypePlan::Arc:
        case ShapeContentTypePlan::Grid:
        case ShapeContentTypePlan::DotGrid: {
            if (!shape.imported_subpaths.empty()) {
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
                    if (source.svg_scale_animation || source.svg_rotation_animation ||
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
                        imported_path.fill.enabled = true;
                        imported_path.fill.type = fill_type_for_gradient(
                            source.fill_gradient->type);
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
                        imported_stroke.dash_array = source.stroke->dash_array;
                        for (auto& dash : imported_stroke.dash_array)
                            dash *= std::abs(lottie_group_scale.x);
                        imported_stroke.dash_offset = source.stroke->dash_offset *
                            std::abs(lottie_group_scale.x);
                        imported_stroke.cap = source.stroke->cap == "round" ? LineCap::Round
                            : source.stroke->cap == "square" ? LineCap::Square : LineCap::Butt;
                        imported_stroke.join = source.stroke->join == "round" ? LineJoin::Round
                            : source.stroke->join == "bevel" ? LineJoin::Bevel : LineJoin::Miter;
                        imported_path.stroke = imported_stroke.to_path_stroke();
                    } else {
                        imported_path.stroke.enabled = false;
                    }
                    std::vector<PathShape> rendered_paths;
                    {
                        PathShape base;
                        base.commands = imported_path.commands;
                        base.stroke = imported_path.stroke;
                        base.fill = imported_path.fill;
                        base.closed = imported_path.closed;
                        rendered_paths.push_back(std::move(base));
                    }
                    for (const auto& operation : shape.operators) {
                        if (operation.kind == PathOperatorKindPlan::Trim) {
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
                            for (auto& path_to_trim : rendered_paths) {
                                const auto trimmed = apply_path_operators(path_to_trim,
                                    {{PathOperatorKind::Trim, trim, 0}});
                                path_to_trim.commands = std::move(trimmed.front().commands);
                                path_to_trim.closed = trimmed.front().closed;
                            }
                        } else if (operation.kind == PathOperatorKindPlan::Repeater) {
                            const auto& repeater = operation.repeater;
                            RepeaterParams rp;
                            rp.copies = repeater.copies;
                            rp.position = {repeater.position[0], repeater.position[1]};
                            rp.scale = {repeater.scale[0], repeater.scale[1]};
                            rp.rotation_degrees = repeater.rotation_degrees;
                            rp.anchor = {repeater.anchor[0], repeater.anchor[1]};
                            rp.opacity_decay = repeater.opacity_decay;
                            if (operation.repeater_animation &&
                                !operation.repeater_animation->keyframes.empty()) {
                                // Sampled per frame: [position_x, position_y,
                                // scale_x, scale_y, rotation_degrees]; missing
                                // components keep the static values.
                                AnimatedValue<float> position_x{repeater.position[0]};
                                AnimatedValue<float> position_y{repeater.position[1]};
                                AnimatedValue<float> scale_x{repeater.scale[0]};
                                AnimatedValue<float> scale_y{repeater.scale[1]};
                                AnimatedValue<float> rotation{repeater.rotation_degrees};
                                for (const auto& key : operation.repeater_animation->keyframes) {
                                    const auto easing = detail::animation_keyframe_easing(
                                        *operation.repeater_animation, key);
                                    if (!key.value.empty() && !std::isfinite(key.value[0]))
                                        throw std::runtime_error(
                                            "repeater animation components must be finite");
                                    if (!key.value.empty()) position_x.add_keyframe(key.frame, key.value[0], easing);
                                    if (key.value.size() > 1) position_y.add_keyframe(key.frame, key.value[1], easing);
                                    if (key.value.size() > 2) scale_x.add_keyframe(key.frame, key.value[2], easing);
                                    if (key.value.size() > 3) scale_y.add_keyframe(key.frame, key.value[3], easing);
                                    if (key.value.size() > 4) rotation.add_keyframe(key.frame, key.value[4], easing);
                                }
                                rp.position = {position_x.evaluate(ctx.effective_frame()),
                                               position_y.evaluate(ctx.effective_frame())};
                                const auto scale_sample_x = scale_x.evaluate(ctx.effective_frame());
                                const auto scale_sample_y = scale_y.evaluate(ctx.effective_frame());
                                rp.scale = {std::abs(scale_sample_x) < 1e-6f
                                        ? std::copysign(1e-6f, scale_sample_x)
                                        : scale_sample_x,
                                    std::abs(scale_sample_y) < 1e-6f
                                        ? std::copysign(1e-6f, scale_sample_y)
                                        : scale_sample_y};
                                rp.rotation_degrees = rotation.evaluate(ctx.effective_frame());
                            }
                            std::vector<PathShape> expanded;
                            expanded.reserve(rendered_paths.size() * rp.copies);
                            for (const auto& source : rendered_paths) {
                                auto copies = apply_path_operators(source,
                                    {{PathOperatorKind::Repeater, rp, operation.seed}});
                                expanded.insert(expanded.end(), copies.begin(), copies.end());
                            }
                            rendered_paths = std::move(expanded);
                        } else {
                            throw std::runtime_error(
                                "imported animated paths only support Trim Paths and Repeater operators");
                        }
                    }
                    if (rendered_paths.size() > 4096)
                        throw std::runtime_error(
                            "imported path operator expansion exceeds 4096 sibling paths");
                    for (std::size_t copy_index = 0;
                         copy_index < rendered_paths.size(); ++copy_index) {
                        PathParams copy_path;
                        copy_path.color = base_color;
                        for (const auto& command : rendered_paths[copy_index].commands)
                            copy_path.commands.push_back(command);
                        copy_path.stroke = rendered_paths[copy_index].stroke;
                        copy_path.fill = rendered_paths[copy_index].fill;
                        copy_path.closed = rendered_paths[copy_index].closed;
                        const std::string node_name = rendered_paths.size() == 1
                            ? "svg_path_" + std::to_string(path_index)
                            : "svg_path_" + std::to_string(path_index) + "_copy_" +
                                  std::to_string(copy_index);
                        builder.path(node_name, std::move(copy_path));
                    }
                }
                break;
            }
            PathParams params;
            for (const auto& command : shape.path) {
                const Vec2 point{command.point[0], command.point[1]};
                const Vec2 control1{command.control1[0], command.control1[1]};
                const Vec2 control2{command.control2[0], command.control2[1]};
                switch (command.type) {
                    case ShapePathCommandTypePlan::MoveTo:
                        params.commands.push_back(PathCommand::move_to(point)); break;
                    case ShapePathCommandTypePlan::LineTo:
                        params.commands.push_back(PathCommand::line_to(point)); break;
                    case ShapePathCommandTypePlan::QuadraticTo:
                        params.commands.push_back(PathCommand::quadratic_to(control1, point)); break;
                    case ShapePathCommandTypePlan::CubicTo:
                        params.commands.push_back(PathCommand::cubic_to(control1, control2, point)); break;
                    case ShapePathCommandTypePlan::Close:
                        params.commands.push_back(PathCommand::close());
                        params.closed = true;
                        break;
                }
            }
            if (active_gradient) {
                params.fill.enabled = true;
                params.fill.type = fill_type_for_gradient(active_gradient->type);
                params.fill.gradient = *active_gradient;
            } else if (shape.gradient_mesh) {
                params.fill = Fill::mesh(*shape.gradient_mesh);
            } else if (shape.field) {
                params.fill = Fill::field2d(*sampled_field, base_color, shape.field_ramp,
                    shape.field_render_scale,
                    static_cast<std::uint32_t>(std::max(1.0f, std::ceil(size.x))),
                    static_cast<std::uint32_t>(std::max(1.0f, std::ceil(size.y))));
            } else if (shape.fill_color) {
                // Path fill is independent from layer.color, which modulates
                // stroke alpha in draw_path(). Do not tint stroke with fill RGBA.
                params.fill = Fill::solid_color(Color{
                    (*shape.fill_color)[0], (*shape.fill_color)[1],
                    (*shape.fill_color)[2], (*shape.fill_color)[3]});
            }
            params.stroke = stroke.to_path_stroke();
            params.pos = {0.0f, 0.0f, 0.0f};
            if (shape.operators.empty()) {
        builder.path("shape", std::move(params));
    } else {
        PathShape source_path;
        source_path.commands = params.commands;
        source_path.stroke = params.stroke;
        source_path.fill = params.fill;
        source_path.closed = params.closed;
        std::vector<PathOperatorSpec> specs;
        for (const auto& op : shape.operators) {
            if (op.kind == PathOperatorKindPlan::RoundCorners) {
                specs.push_back({PathOperatorKind::RoundCorners,
                    RoundCornersParams{op.radius}, 0});
            } else if (op.kind == PathOperatorKindPlan::ZigZag) {
                specs.push_back({PathOperatorKind::ZigZag,
                    ZigZagParams{op.size, op.points_per_unit, op.smooth}, 0});
            } else if (op.kind == PathOperatorKindPlan::OffsetPath) {
                specs.push_back({PathOperatorKind::OffsetPath,
                    OffsetPathParams{op.amount, op.miter_limit}, 0});
            } else if (op.kind == PathOperatorKindPlan::PuckerBloat) {
                specs.push_back({PathOperatorKind::PuckerBloat,
                    PuckerBloatParams{op.amount, {op.anchor[0], op.anchor[1]}}, 0});
            } else if (op.kind == PathOperatorKindPlan::Twist) {
                specs.push_back({PathOperatorKind::Twist,
                    TwistParams{op.degrees, {op.center[0], op.center[1]}}, 0});
            } else if (op.kind == PathOperatorKindPlan::WigglePath) {
                specs.push_back({PathOperatorKind::WigglePath,
                    WigglePathParams{op.amount, op.frequency}, op.seed});
            } else if (op.kind == PathOperatorKindPlan::Morph) {
                MorphPathParams morph;
                morph.progress = op.morph_progress;
                morph.target.reserve(op.morph_target.size());
                for (const auto& command : op.morph_target) {
                    const Vec2 point{command.point[0], command.point[1]};
                    const Vec2 c1{command.control1[0], command.control1[1]};
                    const Vec2 c2{command.control2[0], command.control2[1]};
                    switch (command.type) {
                        case ShapePathCommandTypePlan::MoveTo: morph.target.push_back(PathCommand::move_to(point)); break;
                        case ShapePathCommandTypePlan::LineTo: morph.target.push_back(PathCommand::line_to(point)); break;
                        case ShapePathCommandTypePlan::QuadraticTo: morph.target.push_back(PathCommand::quadratic_to(c1, point)); break;
                        case ShapePathCommandTypePlan::CubicTo: morph.target.push_back(PathCommand::cubic_to(c1, c2, point)); break;
                        case ShapePathCommandTypePlan::Close: morph.target.push_back(PathCommand::close()); break;
                    }
                }
                if (op.morph_animation) {
                    chronon3d::AnimatedValue<float> progress{op.morph_progress};
                    for (const auto& key : op.morph_animation->keyframes) {
                        if (key.value.size() != 1)
                            throw std::runtime_error("morph animation keyframes require scalar progress");
                        progress.add_keyframe(
                            key.frame, key.value.front(),
                            detail::animation_keyframe_easing(*op.morph_animation, key));
                    }
                    morph.progress = std::clamp(
                        progress.evaluate(ctx.effective_frame()), 0.0f, 1.0f);
                }
                specs.push_back({PathOperatorKind::Morph, std::move(morph), 0});
            } else if (op.kind == PathOperatorKindPlan::Merge) {
                MergeParams merge;
                for (const auto& planned_path : op.paths) {
                    std::vector<PathCommand> subpath;
                    for (const auto& command : planned_path) {
                        const Vec2 point{command.point[0], command.point[1]};
                        const Vec2 c1{command.control1[0], command.control1[1]};
                        const Vec2 c2{command.control2[0], command.control2[1]};
                        switch (command.type) {
                            case ShapePathCommandTypePlan::MoveTo: subpath.push_back(PathCommand::move_to(point)); break;
                            case ShapePathCommandTypePlan::LineTo: subpath.push_back(PathCommand::line_to(point)); break;
                            case ShapePathCommandTypePlan::QuadraticTo: subpath.push_back(PathCommand::quadratic_to(c1, point)); break;
                            case ShapePathCommandTypePlan::CubicTo: subpath.push_back(PathCommand::cubic_to(c1, c2, point)); break;
                            case ShapePathCommandTypePlan::Close: subpath.push_back(PathCommand::close()); break;
                        }
                    }
                    merge.paths.push_back(std::move(subpath));
                }
                specs.push_back({PathOperatorKind::Merge, std::move(merge), 0});
            } else if (op.kind == PathOperatorKindPlan::Trim) {
                TrimPathParams trim{op.trim_start, op.trim_end, op.trim_offset};
                if (op.trim_animation) {
                    chronon3d::AnimatedValue<float> start{op.trim_start};
                    chronon3d::AnimatedValue<float> end{op.trim_end};
                    chronon3d::AnimatedValue<float> offset{op.trim_offset};
                    for (const auto& key : op.trim_animation->keyframes) {
                        const auto easing = detail::animation_keyframe_easing(
                            *op.trim_animation, key);
                        if (!key.value.empty()) start.add_keyframe(key.frame, key.value[0], easing);
                        if (key.value.size() > 1) end.add_keyframe(key.frame, key.value[1], easing);
                        if (key.value.size() > 2) offset.add_keyframe(key.frame, key.value[2], easing);
                    }
                    trim.start = std::clamp(start.evaluate(ctx.effective_frame()), 0.0f, 1.0f);
                    trim.end = std::clamp(end.evaluate(ctx.effective_frame()), trim.start, 1.0f);
                    trim.offset = offset.evaluate(ctx.effective_frame());
                }
                specs.push_back({PathOperatorKind::Trim,
                    trim, 0});
            } else {
                const auto& repeater = op.repeater;
                RepeaterParams rp;
                rp.copies = repeater.copies;
                rp.position = {repeater.position[0], repeater.position[1]};
                rp.scale = {repeater.scale[0], repeater.scale[1]};
                rp.rotation_degrees = repeater.rotation_degrees;
                rp.anchor = {repeater.anchor[0], repeater.anchor[1]};
                rp.opacity_decay = repeater.opacity_decay;
                if (op.repeater_animation &&
                    !op.repeater_animation->keyframes.empty()) {
                    // Sampled per frame: [position_x, position_y, scale_x,
                    // scale_y, rotation_degrees]; missing components keep the
                    // static values. Copies stay static so the operator's
                    // per-frame node count stays inside the authored bound.
                    AnimatedValue<float> position_x{repeater.position[0]};
                    AnimatedValue<float> position_y{repeater.position[1]};
                    AnimatedValue<float> scale_x{repeater.scale[0]};
                    AnimatedValue<float> scale_y{repeater.scale[1]};
                    AnimatedValue<float> rotation{repeater.rotation_degrees};
                    for (const auto& key : op.repeater_animation->keyframes) {
                        const auto easing = detail::animation_keyframe_easing(
                            *op.repeater_animation, key);
                        if (key.value.size() > 0 && !std::isfinite(key.value[0]))
                            throw std::runtime_error(
                                "repeater animation components must be finite");
                        if (key.value.size() > 0) position_x.add_keyframe(key.frame, key.value[0], easing);
                        if (key.value.size() > 1) position_y.add_keyframe(key.frame, key.value[1], easing);
                        if (key.value.size() > 2) scale_x.add_keyframe(key.frame, key.value[2], easing);
                        if (key.value.size() > 3) scale_y.add_keyframe(key.frame, key.value[3], easing);
                        if (key.value.size() > 4) rotation.add_keyframe(key.frame, key.value[4], easing);
                    }
                    const auto position_sample_x = position_x.evaluate(ctx.effective_frame());
                    const auto position_sample_y = position_y.evaluate(ctx.effective_frame());
                    const auto scale_sample_x = scale_x.evaluate(ctx.effective_frame());
                    const auto scale_sample_y = scale_y.evaluate(ctx.effective_frame());
                    rp.position = {position_sample_x, position_sample_y};
                    // Degenerate scale collapses every copy to a point; keep a
                    // tiny signed epsilon so the transform stays invertible.
                    rp.scale = {std::abs(scale_sample_x) < 1e-6f
                            ? std::copysign(1e-6f, scale_sample_x)
                            : scale_sample_x,
                        std::abs(scale_sample_y) < 1e-6f
                            ? std::copysign(1e-6f, scale_sample_y)
                            : scale_sample_y};
                    rp.rotation_degrees = rotation.evaluate(ctx.effective_frame());
                }
                specs.push_back({PathOperatorKind::Repeater, rp, 0});
            }
        }
        auto expanded = apply_path_operators(source_path, specs);
        for (std::size_t i = 0; i < expanded.size(); ++i) {
            PathParams copy = params;
            copy.commands = std::move(expanded[i].commands);
            copy.stroke = std::move(expanded[i].stroke);
            copy.fill = std::move(expanded[i].fill);
            copy.color.a *= expanded[i].opacity;
            builder.path("shape_repeater_" + std::to_string(i), std::move(copy));
        }
    }
            break;
        }
        case ShapeContentTypePlan::DeviceFrame: {
            const float width = size.x;
            const float height = size.y;
            if (layer.position_dimensions < 2)
                builder.pin_to(Anchor::Center);
            const bool phone = shape.device_frame_kind == DeviceFrameKindPlan::Phone;
            const float bezel = std::min(width, height) * (phone ? 0.035f : 0.02f);
            const Color shell_color{0.035f, 0.045f, 0.06f, 1.0f};
            const Color chrome_color{0.055f, 0.065f, 0.085f, 1.0f};
            RoundedRectParams shell;
            shell.size = size;
            shell.radius = std::min(width, height) * (phone ? 0.12f : 0.055f);
            shell.color = shell_color;
            shell.fill = graphics::FillStyle::solid(shell_color);
            shell.stroke = stroke;
            shell.pos = {-width * 0.5f, -height * 0.5f, 0.0f};
            builder.rounded_rect("device_shell", std::move(shell));

            RoundedRectParams screen;
            screen.size = {std::max(1.0f, width - bezel * 2.0f),
                           std::max(1.0f, height - bezel * 2.0f)};
            const Vec2 screen_size = screen.size;
            screen.radius = std::min(screen.size.x, screen.size.y) *
                (phone ? 0.085f : 0.025f);
            screen.color = base_color;
            screen.fill = fill;
            screen.pos = {-width * 0.5f + bezel,
                          -height * 0.5f + bezel, 0.0f};
            builder.rounded_rect("device_screen", std::move(screen));

            if (shape.device_frame_chrome && phone) {
                const float notch_width = std::min(screen_size.x * 0.70f, width * 0.30f);
                const float notch_height = std::min(screen_size.y * 0.20f,
                    std::max(2.0f, height * 0.035f));
                RoundedRectParams notch;
                notch.size = {notch_width, notch_height};
                notch.radius = notch_height * 0.5f;
                notch.color = chrome_color;
                notch.fill = graphics::FillStyle::solid(chrome_color);
                notch.pos = {-notch_width * 0.5f,
                    -height * 0.5f + bezel, 0.0f};
                builder.rounded_rect("device_notch", std::move(notch));
            } else if (shape.device_frame_chrome) {
                const float toolbar_height = std::min(screen_size.y,
                    std::max(8.0f, height * 0.13f));
                RoundedRectParams toolbar;
                toolbar.size = {width - bezel * 2.0f, toolbar_height};
                toolbar.radius = std::min(4.0f, bezel);
                toolbar.color = chrome_color;
                toolbar.fill = graphics::FillStyle::solid(chrome_color);
                toolbar.pos = {-width * 0.5f + bezel,
                    -height * 0.5f + bezel, 0.0f};
                builder.rounded_rect("browser_toolbar", std::move(toolbar));
                const std::array<Color, 3> lights{
                    Color{0.96f, 0.28f, 0.25f, 1.0f},
                    Color{0.98f, 0.72f, 0.20f, 1.0f},
                    Color{0.23f, 0.78f, 0.36f, 1.0f}};
                for (std::size_t light = 0; light < lights.size(); ++light) {
                    CircleParams dot;
                    dot.radius = std::min(toolbar_height * 0.22f,
                        std::max(1.0f, bezel * 0.20f));
                    dot.color = lights[light];
                    dot.fill = graphics::FillStyle::solid(lights[light]);
                    dot.pos = {-width * 0.5f + bezel +
                        dot.radius + static_cast<float>(light) * dot.radius * 3.2f,
                        -height * 0.5f + bezel + toolbar_height * 0.5f, 0.0f};
                    builder.circle("browser_traffic_light_" +
                        std::to_string(light), std::move(dot));
                }
            }
            break;
        }
    }
    if (layer.style && layer.style->backdrop) {
        const auto& backdrop = *layer.style->backdrop;
        builder.backdrop_blur(BackdropBlurParams{
            .blur = backdrop.blur,
            .brightness = backdrop.brightness,
            .saturation = backdrop.saturation,
            .rim_light = backdrop.rim_light,
            .refraction = backdrop.refraction,
            .surface_noise = backdrop.surface_noise});
    }

}

} // namespace chronon3d::render_plan::detail
