#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/render_plan/easing_contract.hpp>
#include <chronon3d/animation/core/animated_value.hpp>
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

void append_path_shape(chronon3d::LayerBuilder& builder,
                       const LayerPlan& layer, const RenderPlan& plan,
                       const chronon3d::FrameContext& ctx,
                       const ShapeContentPlan& shape, Vec2 size, Color base_color,
                       const std::optional<graphics::GradientDefinition>& active_gradient,
                       const std::optional<graphics::Field2D>& sampled_field,
                       const graphics::StrokeStyle& stroke) {
    if (!shape.imported_subpaths.empty()) {
        append_imported_svg_paths(builder, plan, ctx, shape, base_color);
        return;
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
            configure_shape_path_gradient_fill(params.fill, *active_gradient);
        } else if (shape.gradient_mesh) {
            params.fill = Fill::mesh(*shape.gradient_mesh);
        } else if (shape.field) {
            params.fill = Fill::field2d(*sampled_field, base_color, shape.field_ramp,
                shape.field_render_scale,
                static_cast<std::uint32_t>(std::max(1.0f, std::ceil(size.x))),
                static_cast<std::uint32_t>(std::max(1.0f, std::ceil(size.y))));
        } else if (shape.fill_color) {
            // Path fill RGB/alpha lives in its Fill payload; node.color
            // supplies only the layer-opacity multiplier to draw_path().
            params.fill = Fill::solid_color(base_color);
        } else {
            params.fill.enabled = false;
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
        } else if (op.kind == PathOperatorKindPlan::Merge ||
                   op.kind == PathOperatorKindPlan::BooleanUnion) {
            std::vector<std::vector<PathCommand>> planned_paths;
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
                planned_paths.push_back(std::move(subpath));
            }
            if (op.kind == PathOperatorKindPlan::BooleanUnion)
                specs.push_back({PathOperatorKind::BooleanUnion,
                    BooleanUnionParams{std::move(planned_paths)}, 0});
            else
                specs.push_back({PathOperatorKind::Merge,
                    MergeParams{std::move(planned_paths)}, 0});
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
            if (op.repeater_animation) {
                AnimatedValue<float> px{repeater.position[0]}, py{repeater.position[1]};
                AnimatedValue<float> sx{repeater.scale[0]}, sy{repeater.scale[1]};
                AnimatedValue<float> rotation{repeater.rotation_degrees};
                for (const auto& key : op.repeater_animation->keyframes) {
                    const auto easing = detail::animation_keyframe_easing(
                        *op.repeater_animation, key);
                    if (!key.value.empty()) px.add_keyframe(key.frame, key.value[0], easing);
                    if (key.value.size() > 1) py.add_keyframe(key.frame, key.value[1], easing);
                    if (key.value.size() > 2) sx.add_keyframe(key.frame, key.value[2], easing);
                    if (key.value.size() > 3) sy.add_keyframe(key.frame, key.value[3], easing);
                    if (key.value.size() > 4) rotation.add_keyframe(key.frame, key.value[4], easing);
                }
                rp.position = {px.evaluate(ctx.effective_frame()),
                               py.evaluate(ctx.effective_frame())};
                rp.scale = {sx.evaluate(ctx.effective_frame()),
                            sy.evaluate(ctx.effective_frame())};
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

}

} // namespace chronon3d::render_plan::detail
