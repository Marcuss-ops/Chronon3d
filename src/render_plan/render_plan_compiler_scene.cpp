#include "render_plan_compiler_detail.hpp"
#include <chronon3d/render_plan/color_utils.hpp>
#include "light_attenuation.hpp"
#include <chronon3d/effects/effect_param_animation.hpp>
#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace chronon3d::render_plan::detail {
namespace {

LayoutFlow make_layout_flow(const LayerPlan::FlowLayoutPlan& plan) {
    LayoutFlow flow;
    flow.group_id = plan.group_id;
    flow.main_axis = plan.axis == "column" ? Axis::Column : Axis::Row;
    flow.gap = plan.gap;
    flow.wrap = plan.wrap;
    flow.cell_size = {plan.cell_size[0], plan.cell_size[1]};
    flow.align = plan.align == "center" ? AlignItems::Center
        : plan.align == "end" ? AlignItems::End
        : plan.align == "stretch" ? AlignItems::Stretch : AlignItems::Start;
    flow.justify = plan.justify == "center" ? JustifyContent::Center
        : plan.justify == "end" ? JustifyContent::End
        : plan.justify == "space_between" ? JustifyContent::SpaceBetween
        : plan.justify == "space_around" ? JustifyContent::SpaceAround
        : plan.justify == "space_evenly" ? JustifyContent::SpaceEvenly
        : JustifyContent::Start;
    return flow;
}

LayoutGrid make_layout_grid(const LayerPlan::GridLayoutPlan& plan) {
    LayoutGrid grid;
    grid.group_id = plan.group_id;
    grid.columns = plan.columns;
    grid.rows = plan.rows;
    grid.fit = plan.fit == "fixed_rows" ? GridFit::FixedRows
        : plan.fit == "fixed_columns" ? GridFit::FixedColumns : GridFit::Auto;
    grid.gap = {plan.gap[0], plan.gap[1]};
    grid.cell_size = {plan.cell_size[0], plan.cell_size[1]};
    return grid;
}

void apply_scene_layer_safe_area(LayerBuilder& builder, const LayerPlan& layer, const RenderPlan& plan, std::size_t index, const std::vector<std::optional<chronon3d::Vec2>>& prepared_text_bounds) {
    if (layer.safe_insets) {
                const auto& insets = *layer.safe_insets;
                Vec2 content_size{};
                if (layer.type == LayerType::Text &&
                    prepared_text_bounds[index]) {
                    content_size = *prepared_text_bounds[index];
                    if (layer.style && layer.style->background) {
                        const auto& padding_style = *layer.style->background;
                        if (padding_style.padding_dimensions == 2) {
                            content_size.x += 2.0f * padding_style.padding[0];
                            content_size.y += 2.0f * padding_style.padding[1];
                        }
                    }
                } else if (layer.type == LayerType::Shape &&
                           layer.size_dimensions == 2) {
                    content_size = {layer.size[0], layer.size[1]};
                } else if ((layer.type == LayerType::Image ||
                            layer.type == LayerType::Video) &&
                           layer.size_dimensions == 2) {
                    content_size = {layer.size[0], layer.size[1]};
                } else if (layer.type == LayerType::Particles &&
                           layer.particle_emitter) {
                    content_size = layer.particle_emitter->emitter_size;
                } else if (layer.type == LayerType::Light && layer.light) {
                    content_size = Vec2{layer.light->radius * 2.0f};
                }
                content_size.x *= std::abs(layer.scale[0]);
                content_size.y *= std::abs(layer.scale[1]);
                const float angle = glm::radians(layer.rotation[2]);
                const Vec2 rotated_size{
                    std::abs(std::cos(angle)) * content_size.x +
                        std::abs(std::sin(angle)) * content_size.y,
                    std::abs(std::sin(angle)) * content_size.x +
                        std::abs(std::cos(angle)) * content_size.y};
                const float half_canvas_width =
                    static_cast<float>(plan.canvas.width) * 0.5f;
                const float half_canvas_height =
                    static_cast<float>(plan.canvas.height) * 0.5f;
                builder.keep_in_safe_area(chronon3d::SafeArea{
                    .top = insets[0] - half_canvas_height,
                    .bottom = half_canvas_height + insets[2] + rotated_size.y,
                    .left = insets[3] - half_canvas_width,
                    .right = half_canvas_width + insets[1] + rotated_size.x});
    }
}

void apply_scene_layer_shadows(LayerBuilder& builder, const LayerPlan& layer, const RenderPlan& plan, const chronon3d::FrameContext& ctx) {
    // style.shadow/shadows is a LAYER style, not a shape style:
    // a drop shadow is one generic effect-stack entry
    // (DropShadowParams) behind the builder.effect() API, and
    // shape, image and video layers composite through the same
    // stack. Gating it on LayerType::Shape is what made a card
    // frame (a shape) able to cast a shadow while the clip
    // inside it could not.
    //
    // Contract of the widened gate: an enabled effect is an
    // RGBA post effect, and any RGBA post effect resolves the
    // layer off the DirectYUV lane (rgba_post_effects ->
    // RequiresFullFrame in call; Auto falls back to the
    // faithful full-frame graph, Require hard-fails rather than
    // approximating). A video/image layer with a shadow
    // therefore renders on the graph lane, exactly like a
    // shadowed shape does today.
    if ((layer.type == LayerType::Shape || layer.type == LayerType::Video ||
         layer.type == LayerType::Image || layer.type == LayerType::Color) &&
        layer.style) {
        std::size_t shadow_index = 0;
        const auto add_shadow = [&](const ShadowStyle& shadow) {
            const std::string prefix = "shadow_" + std::to_string(shadow_index++);
            const auto color = parse_hex_color(shadow.color, 1.0f);
            if (!color)
                throw std::runtime_error("layer '" + layer.id + "' has invalid shadow color");
            if (shadow.offset_dimensions != 2)
                throw std::runtime_error("layer '" + layer.id + "' shadow requires a two-dimensional offset");
            const float opacity = detail::shadow_scalar_at(
                layer, prefix + "_opacity", ctx.effective_frame(),
                shadow.opacity.value_or(1.0f), plan.canvas.fps)
                .value_or(shadow.opacity.value_or(1.0f));
            const float blur = detail::shadow_scalar_at(
                layer, prefix + "_blur", ctx.effective_frame(),
                shadow.blur.value_or(0.0f), plan.canvas.fps)
                .value_or(shadow.blur.value_or(0.0f));
            const float offset_x = detail::shadow_scalar_at(
                layer, prefix + "_offset_x", ctx.effective_frame(),
                shadow.offset[0], plan.canvas.fps).value_or(shadow.offset[0]);
            const float offset_y = detail::shadow_scalar_at(
                layer, prefix + "_offset_y", ctx.effective_frame(),
                shadow.offset[1], plan.canvas.fps).value_or(shadow.offset[1]);
            const auto animated_color = detail::shadow_color_at(
                layer, prefix + "_color", ctx.effective_frame(),
                *color, plan.canvas.fps);
            Color final_color = animated_color.value_or(*color);
            final_color.a *= opacity;
            if (!std::isfinite(opacity) || opacity < 0.0f || opacity > 1.0f ||
                !std::isfinite(blur) || blur < 0.0f ||
                !std::isfinite(offset_x) || !std::isfinite(offset_y))
                throw std::runtime_error("animated shadow parameters are outside supported bounds");
            builder.effect(DropShadowParams{
                .offset = Vec2{offset_x, offset_y},
                .color = final_color,
                .radius = blur});
        };
        if (layer.style->shadow) add_shadow(*layer.style->shadow);
        for (const auto& shadow : layer.style->shadows) add_shadow(shadow);
    }
}

void apply_scene_layer_effects(LayerBuilder& builder, const LayerPlan& layer, const RenderPlan& plan, std::size_t index, const std::vector<std::vector<chronon3d::EffectParams>>& prepared_effects, const chronon3d::FrameContext& ctx) {
    for (std::size_t effect_index=0; effect_index<prepared_effects[index].size(); ++effect_index) {
        const auto& effect=prepared_effects[index][effect_index];
        const auto* effect_plan=effect_index<layer.effects.size()?&layer.effects[effect_index]:nullptr;
        const auto time_control=effect_plan?effect_plan->posterize_fps:std::nullopt;
        std::optional<TemporalCurve1D> time_curve;
        if(effect_plan&&effect_plan->time_curve)
            time_curve=detail::make_effect_time_curve(*effect_plan->time_curve);
        const double composition_fps=static_cast<double>(plan.canvas.fps.numerator)/plan.canvas.fps.denominator;
        const double duration_seconds=static_cast<double>(plan.canvas.duration.value)/composition_fps;
        std::string_view canonical_effect_id;
        const auto effect_type = effects::detect_effect_type(effect);
        for (const auto& entry : effects::builtin_effect_catalog())
            if (entry.type == effect_type) { canonical_effect_id = entry.id; break; }
        std::optional<EffectParams> animated_effect;
        if (!canonical_effect_id.empty()) {
            for (const auto& param_track : layer.effect_param_tracks) {
                if (param_track.effect_id != canonical_effect_id) continue;
                if (!animated_effect) animated_effect.emplace(effect);
                const auto base = effects::get_animatable_effect_param(
                    *animated_effect, param_track.effect_id, param_track.param);
                if (!base)
                    throw std::runtime_error("effect parameter track is incompatible with the compiled effect parameter type");
                const float value = detail::evaluate_scalar_track(
                    param_track.track, ctx.effective_frame(), *base, plan.canvas.fps);
                const auto* bounds = effects::find_animatable_effect_param(
                    param_track.effect_id, param_track.param);
                if (!bounds || !std::isfinite(value))
                    throw std::runtime_error("effect parameter track produced a non-finite sample");
                if (!effects::set_animatable_effect_param(
                        *animated_effect, param_track.effect_id, param_track.param,
                        std::clamp(value, bounds->minimum, bounds->maximum)))
                    throw std::runtime_error("failed to apply compiled effect parameter track");
            }
        }
        const auto& dispatch_effect = animated_effect ? *animated_effect : effect;
        std::optional<EffectParams> time_evolved_effect;
        if (const auto* roughen = std::get_if<RoughenEdgesParams>(&dispatch_effect);
            roughen && roughen->evolution_speed != 0.0f) {
            time_evolved_effect.emplace(dispatch_effect);
            auto& sampled = std::get<RoughenEdgesParams>(*time_evolved_effect);
            sampled.evolution += sampled.evolution_speed *
                static_cast<float>(ctx.effective_frame() /
                    (static_cast<double>(plan.canvas.fps.numerator) /
                     plan.canvas.fps.denominator));
            sampled.evolution_speed = 0.0f;
        }
        const auto& frame_effect = time_evolved_effect
            ? *time_evolved_effect : dispatch_effect;
        std::visit([&](const auto& params) {
            using T = std::decay_t<decltype(params)>;
            if constexpr (!std::is_same_v<T, std::monostate>)
                builder.effect(params,time_control,time_curve,duration_seconds,composition_fps);
        }, frame_effect);
    }
}

void apply_scene_layer_motion_blur(LayerBuilder& builder, const LayerPlan& layer, const RenderPlan& plan, std::size_t index, const std::vector<FollowPathEvaluators>& follow_path_evaluators, const chronon3d::FrameContext& ctx) {
    if (const auto motion_blur = detail::motion_blur_params_at(
            layer, ctx.effective_frame(), plan.canvas.fps,
            layer.follow_path ? &follow_path_evaluators[index] : nullptr)) {
        builder.effect(*motion_blur);
    }
}

void apply_scene_layer_masks(LayerBuilder& builder, const LayerPlan& layer, const RenderPlan& plan, const chronon3d::FrameContext& ctx) {
    if (layer.path_mask || !layer.masks.empty()) {
        const auto convert_mask_path = [](const auto& input) {
            std::vector<PathCommand> output;
            output.reserve(input.size());
            for (const auto& command : input) {
                const Vec2 point{command.point[0], command.point[1]};
                const Vec2 c1{command.control1[0], command.control1[1]};
                const Vec2 c2{command.control2[0], command.control2[1]};
                switch (command.type) {
                    case ShapePathCommandTypePlan::MoveTo: output.push_back(PathCommand::move_to(point)); break;
                    case ShapePathCommandTypePlan::LineTo: output.push_back(PathCommand::line_to(point)); break;
                    case ShapePathCommandTypePlan::QuadraticTo: output.push_back(PathCommand::quadratic_to(c1, point)); break;
                    case ShapePathCommandTypePlan::CubicTo: output.push_back(PathCommand::cubic_to(c1, c2, point)); break;
                    case ShapePathCommandTypePlan::Close: output.push_back(PathCommand::close()); break;
                }
            }
            return output;
        };
        const auto build_operation = [&](const LayerPlan::PathMaskPlan& source) {
            MaskOperation operation;
            operation.type = source.type == "path" ? MaskType::Path
                : source.type == "rect" ? MaskType::Rect
                : source.type == "rounded_rect" ? MaskType::RoundedRect
                : source.type == "field" ? MaskType::Field2D
                : source.type == "text" ? MaskType::Text
                : MaskType::Ellipse;
            operation.mode = source.mode == "add" ? MaskCombineMode::Add
                : source.mode == "subtract" ? MaskCombineMode::Subtract
                : source.mode == "intersect" ? MaskCombineMode::Intersect
                : MaskCombineMode::Difference;
            operation.pos = Vec3{source.position[0], source.position[1], 0.0f};
            operation.size = Vec2{source.size[0], source.size[1]};
            operation.radius = source.radius;
            operation.path = convert_mask_path(source.path);
            operation.field = source.field;
            operation.text_source = source.text_source;
            operation.feather = source.feather_track
                ? std::clamp(detail::evaluate_scalar_track(*source.feather_track,
                    ctx.effective_frame(), source.feather, plan.canvas.fps), 0.0f, 1000000.0f)
                : source.feather;
            operation.expansion = source.expansion_track
                ? std::clamp(detail::evaluate_scalar_track(*source.expansion_track,
                    ctx.effective_frame(), source.expansion, plan.canvas.fps), -1000000.0f, 1000000.0f)
                : source.expansion;
            operation.opacity = source.opacity_track
                ? std::clamp(detail::evaluate_scalar_track(*source.opacity_track,
                    ctx.effective_frame(), source.opacity, plan.canvas.fps), 0.0f, 1.0f)
                : source.opacity;
            operation.inverted = source.inverted;
            Vec2 path_center{0.0f, 0.0f};
            if (source.type == "path" && source.size[0] == 0.0f && source.size[1] == 0.0f) {
                float min_x = std::numeric_limits<float>::max();
                float min_y = std::numeric_limits<float>::max();
                float max_x = std::numeric_limits<float>::lowest();
                float max_y = std::numeric_limits<float>::lowest();
                for (const auto& command : operation.path) {
                    if (command.type == PathCommandType::Close) continue;
                    const auto include = [&](Vec2 point) {
                        min_x = std::min(min_x, point.x); min_y = std::min(min_y, point.y);
                        max_x = std::max(max_x, point.x); max_y = std::max(max_y, point.y);
                    };
                    include(command.p0);
                    if (command.type == PathCommandType::QuadraticTo || command.type == PathCommandType::CubicTo)
                        include(command.p1);
                    if (command.type == PathCommandType::CubicTo) include(command.p2);
                }
                path_center = Vec2{(min_x + max_x) * 0.5f, (min_y + max_y) * 0.5f};
                operation.size = Vec2{max_x - min_x, max_y - min_y};
                operation.pos += Vec3{path_center, 0.0f};
                for (auto& command : operation.path) {
                    if (command.type == PathCommandType::Close) continue;
                    command.p0 -= path_center;
                    if (command.type == PathCommandType::QuadraticTo || command.type == PathCommandType::CubicTo)
                        command.p1 -= path_center;
                    if (command.type == PathCommandType::CubicTo) command.p2 -= path_center;
                }
            }
            if (source.morph_animation) {
                AnimatedValue<float> progress{0.0f};
                for (const auto& key : source.morph_animation->keyframes)
                    progress.add_keyframe(key.frame, key.value.front(),
                        detail::animation_keyframe_easing(*source.morph_animation, key));
                MorphPathParams morph;
                morph.progress = std::clamp(progress.evaluate(ctx.effective_frame()), 0.0f, 1.0f);
                morph.target = convert_mask_path(source.morph_target);
                if (source.type == "path" && source.size[0] == 0.0f && source.size[1] == 0.0f)
                    for (auto& command : morph.target) {
                        if (command.type == PathCommandType::Close) continue;
                        command.p0 -= path_center;
                        if (command.type == PathCommandType::QuadraticTo || command.type == PathCommandType::CubicTo)
                            command.p1 -= path_center;
                        if (command.type == PathCommandType::CubicTo) command.p2 -= path_center;
                    }
                PathShape path;
                path.commands = std::move(operation.path);
                operation.path = apply_path_operators(path, {
                    PathOperatorSpec{PathOperatorKind::Morph, std::move(morph), 0}}).front().commands;
                if (source.type == "path" && source.size[0] == 0.0f &&
                    source.size[1] == 0.0f) {
                    float min_x = std::numeric_limits<float>::max();
                    float min_y = std::numeric_limits<float>::max();
                    float max_x = std::numeric_limits<float>::lowest();
                    float max_y = std::numeric_limits<float>::lowest();
                    for (const auto& command : operation.path) {
                        if (command.type == PathCommandType::Close) continue;
                        const auto include = [&](Vec2 point) {
                            min_x = std::min(min_x, point.x);
                            min_y = std::min(min_y, point.y);
                            max_x = std::max(max_x, point.x);
                            max_y = std::max(max_y, point.y);
                        };
                        include(command.p0);
                        if (command.type == PathCommandType::QuadraticTo ||
                            command.type == PathCommandType::CubicTo)
                            include(command.p1);
                        if (command.type == PathCommandType::CubicTo)
                            include(command.p2);
                    }
                    operation.size = Vec2{max_x - min_x, max_y - min_y};
                }
            }
            return operation;
        };
        std::vector<LayerPlan::PathMaskPlan> plan_masks;
        if (layer.path_mask) plan_masks.push_back(*layer.path_mask);
        plan_masks.insert(plan_masks.end(), layer.masks.begin(), layer.masks.end());
        Mask composite;
        const auto primary = build_operation(plan_masks.front());
        composite.initial_mode = primary.mode;
        composite.type = primary.type;
        composite.pos = primary.pos;
        composite.size = primary.size;
        composite.radius = primary.radius;
        composite.path = primary.path;
        composite.field = primary.field;
        composite.text_source = primary.text_source;
        composite.feather = primary.feather;
        composite.expansion = primary.expansion;
        composite.opacity = primary.opacity;
        composite.inverted = primary.inverted;
        for (std::size_t index = 1; index < plan_masks.size(); ++index)
            composite.operations.push_back(build_operation(plan_masks[index]));
        builder.mask_composite(std::move(composite));
    }
}

void apply_scene_layer_video_position(LayerBuilder& builder, const LayerPlan& layer, const RenderPlan& plan) {
    if (layer.type == LayerType::Video &&
        layer.position_dimensions >= 2) {
        const float sx = std::abs(layer.scale[0]) > 1e-6f
            ? std::abs(layer.scale[0]) : 1.0f;
        const float sy = std::abs(layer.scale[1]) > 1e-6f
            ? std::abs(layer.scale[1]) : 1.0f;
        const float displayed_w = layer.size_dimensions == 2
            ? layer.size[0] : static_cast<float>(plan.canvas.width) * sx;
        const float displayed_h = layer.size_dimensions == 2
            ? layer.size[1] : static_cast<float>(plan.canvas.height) * sy;
        builder.position({
            layer.position[0] + displayed_w * 0.5f -
                static_cast<float>(plan.canvas.width) * 0.5f,
            layer.position[1] + displayed_h * 0.5f -
                static_cast<float>(plan.canvas.height) * 0.5f,
            layer.position[2]});
    }
}

} // namespace
std::function<chronon3d::Scene(const chronon3d::FrameContext&)>
make_scene_evaluator(SceneMaterializationInputs inputs) {
    return [plan = std::move(inputs.plan),
            resolved_video_paths = std::move(inputs.resolved_video_paths),
            resolved_particle_sprite_paths = std::move(inputs.resolved_particle_sprite_paths),
            prepared_texts = std::move(inputs.prepared_texts),
            prepared_text_counters = std::move(inputs.prepared_text_counters),
            prepared_text_bounds = std::move(inputs.prepared_text_bounds),
            prepared_effects = std::move(inputs.prepared_effects),
            font_engine = std::move(inputs.font_engine),
            has_3d_layer = inputs.has_3d_layer,
            camera_evaluator = std::move(inputs.camera_evaluator),
            follow_path_evaluators = std::move(inputs.follow_path_evaluators),
            resolved_ocio_config_path = std::move(inputs.resolved_ocio_config_path)](
               const chronon3d::FrameContext& ctx) {

        SceneBuilder scene(ctx);
        scene.font_engine(font_engine.get());
            configure_scene_camera_and_lighting(
                scene, plan, has_3d_layer, camera_evaluator, ctx);

        // Point/spot/directional sources are scene-level, not layer geometry.
        std::vector<rendering::PointLightSource> point_sources;
        for (std::size_t index = 0; index < plan.layers.size(); ++index) {
            const auto& layer = plan.layers[index];
            const auto author_layer = [&](LayerBuilder& builder) {
                builder.font_engine(font_engine.get());
                if (layer.type != LayerType::Light && layer.type != LayerType::Adjustment) {
                    if (layer.casts_shadows) builder.casts_shadows(*layer.casts_shadows);
                    if (layer.accepts_shadows) builder.accepts_shadows(*layer.accepts_shadows);
                }
                if (layer.flow_layout)
                    builder.flow_layout(make_layout_flow(*layer.flow_layout));
                if (layer.grid_layout)
                    builder.grid_layout(make_layout_grid(*layer.grid_layout));
                // Image textures carry their own color.  2.5D projection
                // changes their pose, not their albedo, so keep them out
                // of the scene lighting pass (which otherwise grays light
                // product imagery and its transparent edge pixels).
                if ((layer.type == LayerType::Image || layer.type == LayerType::Shape ||
                     layer.type == LayerType::Mesh) && layer.enable_3d)
                    builder.accepts_lights(false).accepts_shadows(false);
                switch (layer.type) {
                    case LayerType::Image: {
                        append_image_layer_for_scene(
                            builder, layer, plan, resolved_ocio_config_path);
                        break;
                    }
                    case LayerType::Video: {
                        append_video_layer_for_scene(
                            builder, layer, plan, index, resolved_video_paths,
                            resolved_ocio_config_path);
                        break;
                    }
                    case LayerType::Color: {
                        append_color_layer_for_scene(builder, layer, plan, ctx);
                        break;
                    }
                    case LayerType::Particles: {
                        append_particles_layer_for_scene(
                            builder, layer, plan, index, resolved_particle_sprite_paths, ctx);
                        break;
                    }
                    case LayerType::Light: {
                        append_light_layer_for_scene(
                            builder, layer, plan, ctx, point_sources);
                        break;
                    }
                    case LayerType::Mesh: {
                        append_mesh_layer_for_scene(builder, layer, ctx);
                        break;
                    }
                    case LayerType::Shape: {
                        append_shape_layer_for_scene(builder, layer, plan, ctx);
                        break;
                    }
                    case LayerType::Text: {
                        append_text_layer_for_scene(
                            builder, layer, plan, index, prepared_texts,
                            prepared_text_counters, prepared_text_bounds, font_engine);
                        break;
                    }
                    case LayerType::Adjustment:
                        break;
                }
                detail::apply_layer_primitives(
                    builder, layer, plan.canvas.width, plan.canvas.height, plan.canvas.fps,
                    layer.follow_path ? &follow_path_evaluators[index] : nullptr);
                if (layer.type == LayerType::Particles && layer.particle_emitter)
                    builder.blend(layer.particle_emitter->blend_mode);
                apply_scene_layer_safe_area(builder, layer, plan, index, prepared_text_bounds);
                apply_scene_layer_shadows(builder, layer, plan, ctx);
                apply_scene_layer_effects(builder, layer, plan, index, prepared_effects, ctx);
                apply_scene_layer_motion_blur(builder, layer, plan, index, follow_path_evaluators, ctx);
                apply_scene_layer_masks(builder, layer, plan, ctx);
                apply_scene_layer_video_position(builder, layer, plan);
            };
            const bool full_canvas_backdrop = has_3d_layer &&
                layer.type != LayerType::Text &&
                layer.type != LayerType::Shape &&

                layer.position_dimensions == 0 &&
                (layer.size_dimensions != 2 ||
                 (layer.size[0] >= static_cast<float>(plan.canvas.width) &&
                  layer.size[1] >= static_cast<float>(plan.canvas.height)));
            // Full-canvas color, image and video plates are backdrops, not
            // camera-facing cards. Keep them in screen space around any
            // projected 3D content, independent of the concrete media type.
            if (layer.type == LayerType::Adjustment)
                scene.adjustment_layer(layer.id, author_layer);
            else if (layer.screen_space || full_canvas_backdrop)
                scene.screen_layer(layer.id, author_layer);
            else scene.layer(layer.id, author_layer);
        }
        for (const auto& source : point_sources) scene.point_light_source(source);
        return scene.build();
        
    };
}
} // namespace chronon3d::render_plan::detail
