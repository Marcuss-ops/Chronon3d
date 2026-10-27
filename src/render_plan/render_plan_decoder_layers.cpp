#include "render_plan_decoder_detail.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace chronon3d::render_plan::detail {

namespace {
bool has_explicit_3d_vector(const nlohmann::json& layer, const char* key) {
    return layer.contains(key) && layer.at(key).is_array() &&
           layer.at(key).size() >= 3;
}
} // namespace

std::optional<PlanDecodeError> decode_plan_layers(
    const nlohmann::json& raw_layers, RenderPlan& plan) {
    std::size_t layer_index = 0;
    for (const auto& layer_value : raw_layers) {
        // Raw inline_mesh geometry is valid in V2 and V3; parametric primitives are V3-only.
        if (plan.schema != kRenderPlanSchemaV3 && layer_value.is_object() &&
            layer_value.contains("inline_mesh") && layer_value.at("inline_mesh").is_object() &&
            layer_value.at("inline_mesh").contains("primitive"))
            return PlanDecodeError{"inline_mesh", "inline_mesh primitive requires a V3 render plan",
                                   "inline_mesh_primitive_requires_v3", "render-plan-decoder"};
        auto layer = detail::decode_layer(layer_value);
        const auto transition_error = [&](const std::optional<TransitionPlan>& transition,
                                          const char* slot) -> std::optional<PlanDecodeError> {
            if (!transition) return std::nullopt;
            const auto lifetime = layer.duration_frames.value_or(plan.canvas.duration).integral();
            const auto span = transition->duration_frames.integral() +
                              transition->delay_frames.integral();
            if (transition->duration_frames.integral() < 1 || span > lifetime)
                return PlanDecodeError{
                    "layers[" + std::to_string(layer_index) + "]." + slot,
                    "transition duration plus delay must fit within the layer lifetime",
                    "invalid_transition_duration", "render-plan-transition"};
            return std::nullopt;
        };
        if (const auto error = transition_error(layer.transition_in, "transition_in")) return *error;
        if (const auto error = transition_error(layer.transition_out, "transition_out")) return *error;
        if (layer.safe_insets &&
            ((*layer.safe_insets)[0] + (*layer.safe_insets)[2] >=
                 static_cast<float>(plan.canvas.height) ||
             (*layer.safe_insets)[1] + (*layer.safe_insets)[3] >=
                 static_cast<float>(plan.canvas.width))) {
            return PlanDecodeError{
                "layers[" + std::to_string(layer_index) + "].safe_insets",
                "safe insets must leave a non-empty canvas area",
                "invalid_safe_insets", "render-plan-decoder"};
        }
        const bool explicit_3d_vector =
            has_explicit_3d_vector(layer_value, "position") ||
            has_explicit_3d_vector(layer_value, "scale") ||
            has_explicit_3d_vector(layer_value, "rotation");
        const auto offending_track = find_3d_track(layer_value);
        if (layer.depth_role && has_explicit_3d_vector(layer_value, "position") &&
            std::abs(layer.position[2]) > 0.0f) {
            return PlanDecodeError{
                "layers[" + std::to_string(layer_index) + "].depth_role",
                "use depth_role + depth_offset, or explicit position_z, not both",
                "depth_role_position_conflict", "render-plan-decoder"};
        }
        if (!layer.enable_3d && (explicit_3d_vector || offending_track)) {
            std::string reason = "3D transform data requires enable_3d=true";
            if (offending_track) {
                reason += "; offending track: " + *offending_track;
            }
            return PlanDecodeError{
                "layers[" + std::to_string(layer_index) + "].enable_3d",
                std::move(reason),
                "3d_requires_explicit_enable",
                "render-plan-decoder"};
        }
        if (detail::invalid_logical_path(layer.asset)) {
            return PlanDecodeError{"layers[].asset",
                "asset references must be relative logical paths"};
        }
        if (detail::invalid_logical_path(layer.source)) {
            return PlanDecodeError{"layers[].source",
                "source references must be relative logical paths"};
        }
        if (detail::invalid_logical_path(layer.font)) {
            return PlanDecodeError{"layers[].style.font",
                "font references must be relative logical paths"};
        }
        if (layer.type == LayerType::Text) {
            if (!layer.style) {
                return PlanDecodeError{"layers[].style",
                    "text layers require a concrete style object"};
            }
            if (layer.font.empty()) {
                return PlanDecodeError{"layers[].style.font",
                    "text layers require an explicit prepared font asset"};
            }
            if (!layer.style->font_size || *layer.style->font_size <= 0.0f) {
                return PlanDecodeError{"layers[].style.font_size",
                    "text layers require a positive concrete font_size"};
            }
            if (layer.style->fill.empty()) {
                return PlanDecodeError{"layers[].style.fill",
                    "text layers require a concrete fill color"};
            }
            if (layer.text_counter) {
                const auto& counter = layer.text_counter->counter;
                if (counter.start_frame.integral() + counter.duration_frames.integral() >
                    plan.canvas.duration.integral())
                    return PlanDecodeError{
                        "layers[" + std::to_string(layer_index) + "].text_counter.counter",
                        "counter start_frame plus duration_frames must fit the canvas duration",
                        "text_counter_outside_canvas", "render-plan-text-counter"};
            }
        }
        plan.layers.push_back(std::move(layer));
        ++layer_index;
    }

    for (std::size_t index = 0; index < plan.layers.size(); ++index) {
        auto& layer = plan.layers[index];
        if (!layer.follow_path) continue;
        auto& driver = *layer.follow_path;
        if (!driver.path_id.empty()) {
            const auto source = std::find_if(plan.layers.begin(), plan.layers.end(),
                [&](const LayerPlan& candidate) { return candidate.id == driver.path_id; });
            if (source == plan.layers.end() || !source->shape || source->shape->path.size() < 2)
                return PlanDecodeError{"layers[" + std::to_string(index) + "].follow_path.path_id",
                    "path_id must name a layer with inline path commands", "follow_path_source_missing",
                    "render-plan-follow-path"};
            driver.path_commands = source->shape->path;
        }
        if (driver.path_commands.size() < 2)
            return PlanDecodeError{"layers[" + std::to_string(index) + "].follow_path",
                "follow_path requires path commands", "follow_path_path_missing", "render-plan-follow-path"};
        if (layer.animation) {
            for (const auto& track : layer.animation->tracks) {
                if (track.property == "position" || track.property == "position_x" ||
                    track.property == "position_y" || track.property == "position_z")
                    return PlanDecodeError{"layers[" + std::to_string(index) + "].animation",
                        "follow_path owns position; remove explicit position tracks", "follow_path_position_conflict",
                        "render-plan-follow-path"};
            }
        }
    }

    if (const auto stagger_error = apply_layout_stagger(
            plan.layers, plan.canvas.width, plan.canvas.height))
        return *stagger_error;

    for (auto& particle_layer : plan.layers) {
        if (!particle_layer.particle_emitter ||
            particle_layer.particle_emitter->shape != EmitterShape::Path) continue;
        const auto source=std::find_if(plan.layers.begin(),plan.layers.end(),[&](const LayerPlan& candidate) {
            return candidate.id==particle_layer.particle_emitter->path_id;
        });
        if(source==plan.layers.end() || !source->shape || source->shape->type!=ShapeContentTypePlan::Path)
            return PlanDecodeError{"layers[].particle_emitter.path_id",
                "path emitter source must name a path shape layer",
                "particle_path_source_missing","render-plan-relations"};
        auto& points=particle_layer.particle_emitter->path_points;
        points.clear();
        chronon3d::Vec2 current{0.0f,0.0f}, subpath_start{0.0f,0.0f};
        bool has_current=false;
        const auto append=[&](chronon3d::Vec2 point) {
            const auto differs=[&] {
                const float dx=points.back().x-point.x,dy=points.back().y-point.y;
                return std::sqrt(dx*dx+dy*dy)>1.0e-5f;
            };
            if(points.empty() || differs()) points.push_back(point);
        };
        for(const auto& command:source->shape->path) {
            const chronon3d::Vec2 end{command.point[0],command.point[1]};
            if(command.type==ShapePathCommandTypePlan::MoveTo) {
                current=end; subpath_start=end; has_current=true; append(end);
            } else if(command.type==ShapePathCommandTypePlan::LineTo && has_current) {
                current=end; append(end);
            } else if(command.type==ShapePathCommandTypePlan::QuadraticTo && has_current) {
                const chronon3d::Vec2 control{command.control1[0],command.control1[1]}, start=current;
                for(int step=1;step<=16;++step) { const float t=step/16.0f,u=1.0f-t; append(u*u*start+2*u*t*control+t*t*end); }
                current=end;
            } else if(command.type==ShapePathCommandTypePlan::CubicTo && has_current) {
                const chronon3d::Vec2 c1{command.control1[0],command.control1[1]},c2{command.control2[0],command.control2[1]},start=current;
                for(int step=1;step<=24;++step) { const float t=step/24.0f,u=1.0f-t; append(u*u*u*start+3*u*u*t*c1+3*u*t*t*c2+t*t*t*end); }
                current=end;
            } else if(command.type==ShapePathCommandTypePlan::Close && has_current) {
                append(subpath_start); current=subpath_start;
            }
        }
        if(points.size()<2)
            return PlanDecodeError{"layers[].particle_emitter.path_id",
                "resolved path must contain at least two distinct points",
                "particle_path_degenerate","render-plan-relations"};
    }

    for (std::size_t i = 0; i < plan.layers.size(); ++i) {
        if (plan.layers[i].ocio_input_space.empty()) continue;
        if (!plan.color_management)
            return PlanDecodeError{"layers[" + std::to_string(i) + "].ocio_input_space",
                "ocio_input_space requires the plan color_management contract",
                "missing_color_management", "render-plan-color"};
        if (!plan.layers[i].input_color_space.empty() &&
            plan.layers[i].input_color_space != "srgb")
            return PlanDecodeError{"layers[" + std::to_string(i) + "].input_color_space",
                "use either input_color_space or ocio_input_space, not both",
                "conflicting_input_color_spaces", "render-plan-color"};
    }

    if (const auto relation_error = validate_render_plan_layer_relations(plan))
        return *relation_error;
    return std::nullopt;
}

} // namespace chronon3d::render_plan::detail
