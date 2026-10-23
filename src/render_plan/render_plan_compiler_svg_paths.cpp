#include "render_plan_compiler_detail.hpp"

#include <chronon3d/assets/svg_path_loader.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace chronon3d::render_plan::detail {

std::optional<PlanDecodeError> append_svg_imported_elements(
    ShapeContentPlan& shape,
    const std::vector<chronon3d::assets::SvgStyledElement>& elements,
    double svg_fps,
    std::size_t layer_index) {
    shape.imported_subpaths.clear();
    shape.imported_subpaths.reserve(elements.size());
    std::size_t total_commands = 0;
    for (const auto& element : elements) {
        if (!std::isfinite(element.opacity) || element.opacity < 0.0f ||
            element.opacity > 1.0f ||
            (element.fill && element.fill->enabled &&
             (element.fill->field || element.fill->gradient_mesh ||
              (!element.fill->gradient.color_stops.empty() &&
               element.fill->gradient.type != graphics::GradientType::Linear &&
               element.fill->gradient.type != graphics::GradientType::Radial))) ||
            element.stroke.gradient)
            return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                "SVG opacity must be in [0,1]; only solid path paints are supported",
                "svg_unsupported_style", "render-plan-svg"};
        ImportedSubpathPlan subpath;
        subpath.source_id = element.source_id;
        subpath.source_group_ids = element.group_ids;
        subpath.fill_rule = element.path.fill_rule == PathFillRule::NonZero
            ? ShapeFillRulePlan::NonZero : ShapeFillRulePlan::EvenOdd;
        auto group_chain = element.groups;
        if (group_chain.empty() && element.group)
            group_chain.push_back(*element.group);
        if (group_chain.size() > 16)
            return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                "SVG isolated group nesting exceeds the depth limit of 16",
                "svg_resource_limit", "render-plan-svg"};
        const double svg_fps = svg_fps;
        const auto frame_at_time = [&](float seconds) -> std::optional<std::int64_t> {
            const double frame = static_cast<double>(seconds) * svg_fps;
            const double rounded = std::round(frame);
            if (!std::isfinite(frame) || std::abs(frame - rounded) > 1e-5 ||
                rounded < 0.0 || rounded > 1000000.0) return std::nullopt;
            return static_cast<std::int64_t>(rounded);
        };
        for (const auto& group : group_chain) {
            if (group.id == 0 || !std::isfinite(group.opacity) ||
                group.opacity < 0.0f || group.opacity > 1.0f)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                    "SVG isolated group is invalid: id must be nonzero and opacity in [0,1]",
                    "svg_unsupported_style", "render-plan-svg"};
            ImportedSubpathPlan::SvgGroupIsolationPlan planned_group{
                group.id, group.opacity};
            if (group.opacity_animation) {
                const auto& animation = *group.opacity_animation;
                const auto first_frame = frame_at_time(animation.begin_seconds);
                const auto last_frame = frame_at_time(
                    animation.begin_seconds + animation.duration_seconds);
                if (animation.kind != assets::SvgAnimationKind::Opacity ||
                    animation.components != 1 || !first_frame || !last_frame ||
                    *last_frame <= *first_frame || animation.from[0] < 0.0f ||
                    animation.from[0] > 1.0f || animation.to[0] < 0.0f ||
                    animation.to[0] > 1.0f)
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                        "SVG group opacity animation is invalid or off the composition frame grid",
                        "svg_animation_timing_unsupported", "render-plan-svg"};
                AnimationTrackPlan track;
                track.property = "svg_group_opacity";
                AnimationKeyframePlan from;
                from.frame = Frame{*first_frame};
                from.value = {animation.from[0]};
                from.interpolation = animation.discrete ? easing_name::kHold : easing_name::kLinear;
                AnimationKeyframePlan to;
                to.frame = Frame{*last_frame};
                to.value = {animation.to[0]};
                track.keyframes = {std::move(from), std::move(to)};
                planned_group.opacity_animation = std::move(track);
            }
            subpath.svg_groups.push_back(std::move(planned_group));
        }
        if (!subpath.svg_groups.empty())
            subpath.svg_group = subpath.svg_groups.back();
        for (const auto& geometry : element.vector_masks) {
            std::vector<ShapePathCommandPlan> commands;
            commands.reserve(geometry.commands.size());
            for (const auto& command : geometry.commands) {
                ShapePathCommandPlan lowered;
                const auto lower_point = [&](Vec2 point, std::array<float, 2>& destination) {
                    if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
                        std::abs(point.x) > 100000.0f || std::abs(point.y) > 100000.0f)
                        throw std::runtime_error("SVG element mask coordinate exceeds finite resource bounds");
                    destination = {std::round(point.x * 10000.0f) / 10000.0f,
                                   std::round(point.y * 10000.0f) / 10000.0f};
                };
                switch (command.type) {
                    case PathCommandType::MoveTo: lowered.type = ShapePathCommandTypePlan::MoveTo; lower_point(command.p0, lowered.point); break;
                    case PathCommandType::LineTo: lowered.type = ShapePathCommandTypePlan::LineTo; lower_point(command.p0, lowered.point); break;
                    case PathCommandType::QuadraticTo: lowered.type = ShapePathCommandTypePlan::QuadraticTo; lower_point(command.p0, lowered.point); lower_point(command.p1, lowered.control1); break;
                    case PathCommandType::CubicTo: lowered.type = ShapePathCommandTypePlan::CubicTo; lower_point(command.p0, lowered.point); lower_point(command.p1, lowered.control1); lower_point(command.p2, lowered.control2); break;
                    case PathCommandType::Close: lowered.type = ShapePathCommandTypePlan::Close; break;
                }
                commands.push_back(lowered);
            }
            subpath.svg_local_masks.push_back(std::move(commands));
        }
        if (element.fill && element.fill->enabled) {
            const bool solid_fill = element.fill->gradient.color_stops.empty();
            if (!solid_fill &&
                std::any_of(element.animations.begin(), element.animations.end(), [](const auto& animation) {
                    return animation.kind == assets::SvgAnimationKind::FillColor;
                }))
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                    "SVG fill color animation requires a solid fill",
                    "svg_unsupported_style", "render-plan-svg"};
            if (solid_fill) {
                auto color = element.fill->solid;
                color.a *= element.opacity;
                subpath.fill_color = std::array<float, 4>{color.r, color.g, color.b, color.a};
            } else {
                subpath.fill_gradient = element.fill->gradient;
                for (auto& stop : subpath.fill_gradient->color_stops)
                    stop.color.a *= element.opacity;
            }
        }
        if (element.stroke.enabled) {
            StrokeStyle stroke;
            stroke.width = element.stroke.width;
            stroke.miter_limit = element.stroke.miter_limit;
            stroke.cap = element.stroke.cap == LineCap::Round ? "round"
                : element.stroke.cap == LineCap::Square ? "square" : "butt";
            stroke.join = element.stroke.join == LineJoin::Round ? "round"
                : element.stroke.join == LineJoin::Bevel ? "bevel" : "miter";
            stroke.dash_array = element.stroke.dash_array;
            stroke.dash_offset = element.stroke.dash_offset;
            auto color = element.stroke.color;
            color.a *= element.opacity;
            stroke.solid_color = std::array<float, 4>{color.r, color.g, color.b, color.a};
            subpath.stroke = std::move(stroke);
        }
        for (const auto& svg_animation : element.animations) {
            const auto first_frame = frame_at_time(svg_animation.begin_seconds);
            const auto last_frame = frame_at_time(
                svg_animation.begin_seconds + svg_animation.duration_seconds);
            if (!first_frame || !last_frame || *last_frame <= *first_frame)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                    "SVG animation times must map exactly to integral composition frames",
                    "svg_animation_timing_unsupported", "render-plan-svg"};
            AnimationTrackPlan track;
            switch (svg_animation.kind) {
                case assets::SvgAnimationKind::Opacity: track.property = "svg_opacity"; break;
                case assets::SvgAnimationKind::Translate: track.property = "svg_translation"; break;
                case assets::SvgAnimationKind::Scale: track.property = "svg_scale"; break;
                case assets::SvgAnimationKind::Rotate: track.property = "svg_rotation"; break;
                case assets::SvgAnimationKind::FillColor: track.property = "fill_color"; break;
                case assets::SvgAnimationKind::StrokeColor: track.property = "stroke_color"; break;
                case assets::SvgAnimationKind::StrokeWidth: track.property = "stroke_width"; break;
                case assets::SvgAnimationKind::FillOpacity: track.property = "fill_opacity"; break;
                case assets::SvgAnimationKind::StrokeOpacity: track.property = "stroke_opacity"; break;
            }
            AnimationKeyframePlan from;
            from.frame = Frame{*first_frame};
            from.value.assign(svg_animation.from.begin(),
                svg_animation.from.begin() + svg_animation.components);
            from.interpolation = svg_animation.discrete ? easing_name::kHold : easing_name::kLinear;
            AnimationKeyframePlan to;
            to.frame = Frame{*last_frame};
            to.value.assign(svg_animation.to.begin(),
                svg_animation.to.begin() + svg_animation.components);
            track.keyframes = {std::move(from), std::move(to)};
            if (svg_animation.kind == assets::SvgAnimationKind::Translate ||
                svg_animation.kind == assets::SvgAnimationKind::Scale ||
                svg_animation.kind == assets::SvgAnimationKind::Rotate) {
                const char* operation = svg_animation.kind == assets::SvgAnimationKind::Translate
                    ? "translate" : svg_animation.kind == assets::SvgAnimationKind::Scale
                    ? "scale" : "rotate";
                subpath.svg_transform_animations.push_back(
                    ImportedSubpathPlan::SvgTransformAnimationPlan{
                        operation, std::move(track)});
                continue;
            }
            std::optional<AnimationTrackPlan>* destination = nullptr;
            switch (svg_animation.kind) {
                case assets::SvgAnimationKind::Opacity: destination = &subpath.svg_opacity_animation; break;
                case assets::SvgAnimationKind::Translate: destination = &subpath.svg_translation_animation; break;
                case assets::SvgAnimationKind::Scale: destination = &subpath.svg_scale_animation; break;
                case assets::SvgAnimationKind::Rotate: destination = &subpath.svg_rotation_animation; break;
                case assets::SvgAnimationKind::FillColor: destination = &subpath.fill_color_animation; break;
                case assets::SvgAnimationKind::StrokeColor: destination = &subpath.stroke_color_animation; break;
                case assets::SvgAnimationKind::StrokeWidth: destination = &subpath.stroke_width_animation; break;
                case assets::SvgAnimationKind::FillOpacity: destination = &subpath.fill_opacity_animation; break;
                case assets::SvgAnimationKind::StrokeOpacity: destination = &subpath.stroke_opacity_animation; break;
            }
            if (*destination)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                    "SVG path has multiple animations targeting the same property",
                    "svg_animation_conflict", "render-plan-svg"};
            *destination = std::move(track);
        }
        if (!element.gradient_animation.empty()) {
            if (!subpath.fill_gradient)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                    "SVG gradient animation requires a gradient fill",
                    "svg_unsupported_style", "render-plan-svg"};
            GradientAnimationPlan track;
            track.easing = element.gradient_animation_discrete ? easing_name::kHold : easing_name::kLinear;
            std::int64_t previous_frame = -1;
            for (const auto& key : element.gradient_animation) {
                const auto frame = frame_at_time(key.seconds);
                if (!frame || *frame <= previous_frame)
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                        "SVG gradient animation times must map to unique integral composition frames",
                        "svg_animation_timing_unsupported", "render-plan-svg"};
                previous_frame = *frame;
                track.keyframes.push_back({Frame{*frame}, key.gradient});
            }
            subpath.fill_gradient_animation = std::move(track);
        }
        if (element.morph_animation) {
            const auto& morph = *element.morph_animation;
            if (morph.paths.size() < 2 || morph.paths.size() != morph.key_times.size())
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                    "SVG path morph has inconsistent keyframe geometry", "svg_morph_invalid", "render-plan-svg"};
            AnimationTrackPlan progress;
            progress.property = "morph_path_progress";
            std::int64_t previous_frame = -1;
            for (std::size_t morph_index = 0; morph_index < morph.paths.size(); ++morph_index) {
                const auto frame = frame_at_time(morph.duration_seconds * morph.key_times[morph_index]);
                if (!frame || *frame <= previous_frame)
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                        "SVG path morph times must map to unique integral composition frames",
                        "svg_animation_timing_unsupported", "render-plan-svg"};
                previous_frame = *frame;
                AnimationKeyframePlan key;
                key.frame = Frame{*frame};
                key.value = {static_cast<float>(morph_index)};
                key.interpolation = morph.discrete ? easing_name::kHold : easing_name::kLinear;
                progress.keyframes.push_back(std::move(key));
                if (morph_index == 0) continue;
                std::vector<ShapePathCommandPlan> target;
                target.reserve(morph.paths[morph_index].commands.size());
                for (const auto& command : morph.paths[morph_index].commands) {
                    ShapePathCommandPlan lowered;
                    const auto point = [&](Vec2 value, std::array<float, 2>& out) {
                        out = {std::round(value.x * 10000.0f) / 10000.0f,
                               std::round(value.y * 10000.0f) / 10000.0f};
                    };
                    switch (command.type) {
                        case PathCommandType::MoveTo: lowered.type = ShapePathCommandTypePlan::MoveTo; point(command.p0, lowered.point); break;
                        case PathCommandType::LineTo: lowered.type = ShapePathCommandTypePlan::LineTo; point(command.p0, lowered.point); break;
                        case PathCommandType::QuadraticTo: lowered.type = ShapePathCommandTypePlan::QuadraticTo; point(command.p0, lowered.point); point(command.p1, lowered.control1); break;
                        case PathCommandType::CubicTo: lowered.type = ShapePathCommandTypePlan::CubicTo; point(command.p0, lowered.point); point(command.p1, lowered.control1); point(command.p2, lowered.control2); break;
                        case PathCommandType::Close: lowered.type = ShapePathCommandTypePlan::Close; break;
                    }
                    target.push_back(lowered);
                }
                subpath.morph_keyframe_targets.push_back(std::move(target));
            }
            subpath.morph_animation = std::move(progress);
        }
        for (const auto& command : element.path.commands) {
            ShapePathCommandPlan lowered;
            const auto quantize = [](float value) { return std::round(value * 10000.0f) / 10000.0f; };
            const auto point = [&](Vec2 value, std::array<float, 2>& out) {
                if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
                    std::abs(value.x) > 100000.0f || std::abs(value.y) > 100000.0f)
                    throw std::runtime_error("SVG coordinate exceeds finite resource bounds");
                out = {quantize(value.x), quantize(value.y)};
            };
            switch (command.type) {
                case PathCommandType::MoveTo: lowered.type = ShapePathCommandTypePlan::MoveTo; point(command.p0, lowered.point); break;
                case PathCommandType::LineTo: lowered.type = ShapePathCommandTypePlan::LineTo; point(command.p0, lowered.point); break;
                case PathCommandType::QuadraticTo: lowered.type = ShapePathCommandTypePlan::QuadraticTo; point(command.p0, lowered.point); point(command.p1, lowered.control1); break;
                case PathCommandType::CubicTo: lowered.type = ShapePathCommandTypePlan::CubicTo; point(command.p0, lowered.point); point(command.p1, lowered.control1); point(command.p2, lowered.control2); break;
                case PathCommandType::Close: lowered.type = ShapePathCommandTypePlan::Close; break;
            }
            if (total_commands >= 65536)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                    "SVG total path command limit exceeded", "svg_resource_limit", "render-plan-svg"};
            subpath.path.push_back(lowered);
            ++total_commands;
        }
        shape.imported_subpaths.push_back(std::move(subpath));
    }
    return std::nullopt;
}

} // namespace chronon3d::render_plan::detail
