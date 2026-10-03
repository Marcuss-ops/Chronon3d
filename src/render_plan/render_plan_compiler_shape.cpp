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
    if (active_gradient && shape.field)
        throw std::runtime_error("shape '" + layer.id +
            "' cannot combine a gradient fill with Field2D");
    std::optional<graphics::FillStyle> fill;
    if (active_gradient) {
        fill = graphics::FillStyle::solid(base_color);
        fill->gradient = *active_gradient;
    } else if (shape.fill_color) {
        fill = graphics::FillStyle::solid(base_color);
    }
    if (shape.field) {
        fill = graphics::FillStyle::solid(base_color);
        fill->field = *shape.field;
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
                    if (source.morph_animation &&
                        source.morph_animation->keyframes.size() >= 2 &&
                        source.morph_target.size() == imported_path.commands.size()) {
                        AnimatedValue<float> progress{0.0f};
                        for (const auto& key : source.morph_animation->keyframes) {
                            if (key.value.size() != 1)
                                throw std::runtime_error("imported morph animation requires scalar progress");
                            progress.add_keyframe(key.frame, key.value.front(),
                                detail::animation_keyframe_easing(*source.morph_animation, key));
                        }
                        const float amount = std::clamp(
                            progress.evaluate(ctx.effective_frame()), 0.0f, 1.0f);
                        for (std::size_t command_index = 0;
                             command_index < imported_path.commands.size(); ++command_index) {
                            const auto& target = source.morph_target[command_index];
                            auto& command = imported_path.commands[command_index];
                            const Vec2 target_point{target.point[0], target.point[1]};
                            const Vec2 target_c1{target.control1[0], target.control1[1]};
                            const Vec2 target_c2{target.control2[0], target.control2[1]};
                            command.p0 = glm::mix(command.p0, target_point, amount);
                            command.p1 = glm::mix(command.p1, target_c1, amount);
                            command.p2 = glm::mix(command.p2, target_c2, amount);
                        }
                    }
                    if (source.fill_color) {
                        const auto& rgba = *source.fill_color;
                        imported_path.fill = Fill::solid_color(
                            Color{rgba[0], rgba[1], rgba[2], rgba[3]});
                    } else if (source.fill_gradient) {
                        imported_path.fill.enabled = true;
                        imported_path.fill.type = fill_type_for_gradient(
                            source.fill_gradient->type);
                        imported_path.fill.gradient = *source.fill_gradient;
                    } else {
                        imported_path.fill.enabled = false;
                    }
                    if (source.stroke && source.stroke->solid_color) {
                        const auto& rgba = *source.stroke->solid_color;
                        auto imported_stroke = graphics::StrokeStyle::solid(
                            Color{rgba[0], rgba[1], rgba[2], rgba[3]},
                            source.stroke->width.value_or(1.0f));
                        imported_stroke.dash_array = source.stroke->dash_array;
                        imported_stroke.dash_offset = source.stroke->dash_offset;
                        imported_path.stroke = imported_stroke.to_path_stroke();
                    } else {
                        imported_path.stroke.enabled = false;
                    }
                    builder.path("svg_path_" + std::to_string(path_index),
                                 std::move(imported_path));
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
            } else if (shape.field) {
                params.fill = Fill::field2d(*shape.field, base_color);
            } else if (shape.fill_color) {
                // Path fill RGB/alpha lives in its Fill payload; node.color
                // supplies only the layer-opacity multiplier to draw_path().
                params.fill = Fill::solid_color(base_color);
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
