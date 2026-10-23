#include "render_plan_compiler_detail.hpp"
#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d::render_plan::detail {
std::optional<PlanDecodeError> attach_svg_element_animations(
    ShapeContentPlan& shape, std::size_t layer_index,
    const std::vector<std::size_t>& current_text_layers,
    std::vector<std::pair<chronon3d::assets::SvgTextRun, LayerPlan>>& pending_svg_text_layers) {
        for (const auto& binding : shape.svg_element_animations) {
            std::vector<std::size_t> exact_text_matches;
            std::vector<std::size_t> group_text_matches;
            for (const auto pending_index : current_text_layers) {
                const auto& run = pending_svg_text_layers[pending_index].first;
                if (run.source_id == binding.target_id)
                    exact_text_matches.push_back(pending_index);
                if (std::find(run.group_ids.begin(), run.group_ids.end(), binding.target_id) !=
                    run.group_ids.end())
                    group_text_matches.push_back(pending_index);
            }
            const bool one_clustered_text_run = exact_text_matches.size() > 1 &&
                std::all_of(exact_text_matches.begin(), exact_text_matches.end(),
                    [&](std::size_t index) {
                        const auto& first = pending_svg_text_layers[exact_text_matches.front()].first;
                        const auto& current = pending_svg_text_layers[index].first;
                        const bool same_path = first.text_path_id &&
                            current.text_path_id == first.text_path_id;
                        const bool same_positioned_run = first.positioned_text_cluster &&
                            current.positioned_text_cluster;
                        return (same_path || same_positioned_run) &&
                            current.source_id == first.source_id &&
                            current.text_element_id == first.text_element_id;
                    });
            if (exact_text_matches.size() > 1 && !one_clustered_text_run)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                    "].shape.element_animations",
                    "SVG text target id is ambiguous: " + binding.target_id,
                    "svg_target_ambiguous", "render-plan-svg"};
            const auto& text_matches = exact_text_matches.empty()
                ? group_text_matches : exact_text_matches;
            if (!text_matches.empty()) {
                if (binding.property != "opacity")
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                        "].shape.element_animations",
                        "SVG text currently supports only opacity bindings",
                        "svg_target_property_invalid", "render-plan-svg"};
                for (const auto pending_index : text_matches) {
                    auto& text_layer = pending_svg_text_layers[pending_index].second;
                    if (!text_layer.animation) text_layer.animation = AnimationPlan{};
                    auto track = binding.animation;
                    track.property = "opacity";
                    text_layer.animation->tracks.push_back(std::move(track));
                }
                continue;
            }
            std::vector<ImportedSubpathPlan*> exact_matches;
            std::vector<ImportedSubpathPlan*> group_matches;
            for (auto& subpath : shape.imported_subpaths) {
                if (subpath.source_id == binding.target_id)
                    exact_matches.push_back(&subpath);
                if (std::find(subpath.source_group_ids.begin(),
                              subpath.source_group_ids.end(), binding.target_id) !=
                    subpath.source_group_ids.end())
                    group_matches.push_back(&subpath);
            }
            if (exact_matches.size() > 1) {
                return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                    "].shape.element_animations",
                    "SVG element target id is ambiguous after <use> expansion: " + binding.target_id,
                    "svg_target_ambiguous", "render-plan-svg"};
            }
            auto matches = exact_matches.empty() ? group_matches : exact_matches;
            if (matches.empty()) {
                return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                    "].shape.element_animations",
                    "SVG element animation target does not resolve: " + binding.target_id,
                    "svg_target_missing", "render-plan-svg"};
            }
            for (auto* subpath : matches) {
                std::optional<AnimationTrackPlan>* destination = nullptr;
                std::string canonical_property;
                if (binding.property == "opacity") {
                    destination = &subpath->svg_opacity_animation;
                    canonical_property = "svg_opacity";
                } else if (binding.property == "transform.translate") {
                    destination = &subpath->svg_translation_animation;
                    canonical_property = "svg_translation";
                } else if (binding.property == "transform.scale") {
                    destination = &subpath->svg_scale_animation;
                    canonical_property = "svg_scale";
                } else if (binding.property == "transform.rotation") {
                    destination = &subpath->svg_rotation_animation;
                    canonical_property = "svg_rotation";
                } else if (binding.property == "fill.color") {
                    if (!subpath->fill_color)
                        return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                            "].shape.element_animations",
                            "SVG fill.color target requires a solid fill", "svg_target_property_invalid",
                            "render-plan-svg"};
                    destination = &subpath->fill_color_animation;
                    canonical_property = "fill_color";
                } else if (binding.property == "fill.opacity") {
                    if (!subpath->fill_color && !subpath->fill_gradient)
                        return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                            "].shape.element_animations",
                            "SVG fill.opacity target requires a fill", "svg_target_property_invalid",
                            "render-plan-svg"};
                    destination = &subpath->fill_opacity_animation;
                    canonical_property = "fill_opacity";
                } else if (binding.property == "stroke.color") {
                    if (!subpath->stroke)
                        return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                            "].shape.element_animations",
                            "SVG stroke.color target requires a stroke", "svg_target_property_invalid",
                            "render-plan-svg"};
                    destination = &subpath->stroke_color_animation;
                    canonical_property = "stroke_color";
                } else if (binding.property == "stroke.opacity") {
                    if (!subpath->stroke)
                        return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                            "].shape.element_animations",
                            "SVG stroke.opacity target requires a stroke", "svg_target_property_invalid",
                            "render-plan-svg"};
                    destination = &subpath->stroke_opacity_animation;
                    canonical_property = "stroke_opacity";
                } else if (binding.property == "stroke.width") {
                    if (!subpath->stroke)
                        return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                            "].shape.element_animations",
                            "SVG stroke.width target requires a stroke", "svg_target_property_invalid",
                            "render-plan-svg"};
                    destination = &subpath->stroke_width_animation;
                    canonical_property = "stroke_width";
                }
                if (!destination)
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                        "].shape.element_animations",
                        "Unsupported SVG element animation property: " + binding.property,
                        "svg_target_property_invalid", "render-plan-svg"};
                if (*destination)
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                        "].shape.element_animations",
                        "SVG target property already has an animation: " + binding.property,
                        "svg_animation_conflict", "render-plan-svg"};
                auto track = binding.animation;
                track.property = canonical_property;
                *destination = std::move(track);
            }
        }
    return std::nullopt;
}
} // namespace chronon3d::render_plan::detail
