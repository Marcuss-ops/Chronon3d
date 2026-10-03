#include <chronon3d/render_plan/render_plan.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace chronon3d::render_plan {

std::optional<PlanDecodeError> validate_render_plan_layer_relations(
    const RenderPlan& plan) {
    const bool has_v3_relations = std::any_of(
        plan.layers.begin(), plan.layers.end(), [](const LayerPlan& layer) {
            return layer.parent.has_value() || layer.track_matte.has_value() ||
                   layer.time_remap.has_value();
        });
    if (has_v3_relations && plan.schema != kRenderPlanSchemaV3) {
        return PlanDecodeError{
            "layers",
            "parent, track_matte, and time_remap are available only in render-plan V3",
            "v3_layer_feature_in_v2", "render-plan-relations"};
    }
    // Keep direct V2 RenderPlan inputs compatible with the historical API:
    // relation/name checks are new V3 semantics, not retroactive V2 rules.
    if (plan.schema != kRenderPlanSchemaV3) return std::nullopt;

    std::unordered_map<std::string, std::size_t> by_id;
    by_id.reserve(plan.layers.size());
    for (std::size_t index = 0; index < plan.layers.size(); ++index) {
        const auto& id = plan.layers[index].id;
        if (!by_id.emplace(id, index).second) {
            return PlanDecodeError{
                "layers[" + std::to_string(index) + "].id",
                "layer ids must be unique",
                "duplicate_layer_id", "render-plan-relations"};
        }
    }

    for (std::size_t index = 0; index < plan.layers.size(); ++index) {
        const auto& layer = plan.layers[index];
        if (layer.time_remap) {
            if (layer.type != LayerType::Video) {
                return PlanDecodeError{
                    "layers[" + std::to_string(index) + "].time_remap",
                    "time_remap is supported only for video layers",
                    "time_remap_requires_video", "render-plan-relations"};
            }
            if (layer.time_remap->speed &&
                (!std::isfinite(*layer.time_remap->speed) ||
                 *layer.time_remap->speed <= 0.0f ||
                 *layer.time_remap->speed > 1000.0f)) {
                return PlanDecodeError{
                    "layers[" + std::to_string(index) + "].time_remap.speed",
                    "speed must be finite and in (0, 1000]",
                    "invalid_time_remap_speed", "render-plan-relations"};
            }
            if (layer.time_remap->freeze_frame &&
                (layer.time_remap->freeze_frame->integral() < 0 ||
                 layer.time_remap->freeze_frame->integral() > 1000000)) {
                return PlanDecodeError{
                    "layers[" + std::to_string(index) + "].time_remap.freeze_frame",
                    "freeze frame must be in [0, 1000000]",
                    "invalid_time_remap_freeze_frame", "render-plan-relations"};
            }
            if (layer.time_remap->freeze_frame && layer.time_remap->animation) {
                return PlanDecodeError{
                    "layers[" + std::to_string(index) + "].time_remap",
                    "freeze_frame and animation cannot both be specified",
                    "time_remap_freeze_animation_conflict", "render-plan-relations"};
            }
            if (layer.time_remap->animation) {
                const auto& track = *layer.time_remap->animation;
                if (track.keyframes.empty()) {
                    return PlanDecodeError{
                        "layers[" + std::to_string(index) + "].time_remap.animation.keyframes",
                        "time-remap animation requires at least one keyframe",
                        "time_remap_animation_empty", "render-plan-relations"};
                }
                for (const auto& key : track.keyframes) {
                    if (key.frame.integral() < 0 || key.frame.integral() > 1000000 ||
                        key.value.size() != 1 || !std::isfinite(key.value.front()) ||
                        key.value.front() < 0.0f || key.value.front() > 1000000.0f) {
                        return PlanDecodeError{
                            "layers[" + std::to_string(index) + "].time_remap.animation.keyframes",
                            "time-remap keyframes require bounded non-negative frames and scalar values",
                            "invalid_time_remap_keyframe", "render-plan-relations"};
                    }
                }
            }
        }
        if (layer.parent) {
            if (*layer.parent == layer.id) {
                return PlanDecodeError{
                    "layers[" + std::to_string(index) + "].parent",
                    "a layer cannot parent itself",
                    "layer_parent_self_reference", "render-plan-relations"};
            }
            if (!by_id.contains(*layer.parent)) {
                return PlanDecodeError{
                    "layers[" + std::to_string(index) + "].parent",
                    "parent layer id does not exist: " + *layer.parent,
                    "layer_parent_missing", "render-plan-relations"};
            }
        }
        if (layer.track_matte) {
            if (layer.track_matte->source == layer.id) {
                return PlanDecodeError{
                    "layers[" + std::to_string(index) + "].track_matte.source",
                    "a layer cannot use itself as a track-matte source",
                    "track_matte_self_reference", "render-plan-relations"};
            }
            if (!by_id.contains(layer.track_matte->source)) {
                return PlanDecodeError{
                    "layers[" + std::to_string(index) + "].track_matte.source",
                    "track-matte source layer id does not exist: " + layer.track_matte->source,
                    "track_matte_source_missing", "render-plan-relations"};
            }
            const auto source_index = by_id.at(layer.track_matte->source);
            if (plan.layers[source_index].type == LayerType::Adjustment) {
                return PlanDecodeError{
                    "layers[" + std::to_string(index) + "].track_matte.source",
                    "track-matte source must produce pixels; adjustment layers do not",
                    "track_matte_source_not_raster", "render-plan-relations"};
            }
            const auto active_window = [&](const LayerPlan& candidate) {
                const std::int64_t canvas_end = plan.canvas.duration.integral();
                const std::int64_t start = candidate.start_frame
                    ? candidate.start_frame->integral() : 0;
                const std::int64_t duration = candidate.duration_frames
                    ? candidate.duration_frames->integral() : -1;
                const std::int64_t begin = std::clamp<std::int64_t>(start, 0, canvas_end);
                std::int64_t end = canvas_end;
                if (duration >= 0) {
                    // Compute the clipped end without overflowing for
                    // arbitrary signed start frames or large durations.
                    if (start >= canvas_end) {
                        end = canvas_end;
                    } else if (start >= 0) {
                        end = start + std::min(duration, canvas_end - start);
                    } else {
                        const std::int64_t before_canvas = start == INT64_MIN
                            ? INT64_MAX : -start;
                        end = duration < before_canvas
                            ? 0 : std::min(canvas_end, duration - before_canvas);
                    }
                }
                return std::pair{begin, std::max(begin, end)};
            };
            const auto [target_begin, target_end] = active_window(layer);
            const auto [source_begin, source_end] = active_window(plan.layers[source_index]);
            if (target_begin < target_end &&
                (source_begin > target_begin || source_end < target_end)) {
                const std::int64_t first_uncovered = source_begin > target_begin
                    ? target_begin : source_end;
                return PlanDecodeError{
                    "layers[" + std::to_string(index) + "].track_matte.source",
                    "track-matte source '" + layer.track_matte->source +
                        "' is not active for target layer '" + layer.id +
                        "' at frame " + std::to_string(first_uncovered),
                    "track_matte_source_duration_mismatch", "render-plan-relations"};
            }
        }
        const auto validate_text_mask = [&](const LayerPlan::PathMaskPlan& mask,
                                             const std::string& path) -> std::optional<PlanDecodeError> {
            if (mask.type != "text") return std::nullopt;
            const auto source = by_id.find(mask.text_source);
            if (source == by_id.end())
                return PlanDecodeError{path + ".source",
                    "text mask source layer id does not exist: " + mask.text_source,
                    "text_mask_source_missing", "render-plan-relations"};
            if (plan.layers[source->second].type != LayerType::Text)
                return PlanDecodeError{path + ".source",
                    "text mask source must reference a text layer",
                    "text_mask_source_not_text", "render-plan-relations"};
            if (mask.text_source == layer.id)
                return PlanDecodeError{path + ".source",
                    "a layer cannot use itself as a text mask source",
                    "text_mask_self_reference", "render-plan-relations"};
            return std::nullopt;
        };
        if (layer.path_mask) {
            if (auto error = validate_text_mask(*layer.path_mask,
                    "layers[" + std::to_string(index) + "].mask")) return error;
        }
        for (std::size_t mask_index = 0; mask_index < layer.masks.size(); ++mask_index) {
            if (auto error = validate_text_mask(layer.masks[mask_index],
                    "layers[" + std::to_string(index) + "].masks[" +
                    std::to_string(mask_index) + "]")) return error;
        }
        for(const auto& effect:layer.effects) {
            if(effect.kind!=LayerPlan::EffectKindPlan::DisplacementMap) continue;
            if (effect.map_field) continue;
            if(effect.map_layer==layer.id)
                return PlanDecodeError{"layers["+std::to_string(index)+"].effects.map_layer",
                    "a layer cannot use itself as its displacement map",
                    "displacement_map_self_reference","render-plan-relations"};
            if(!by_id.contains(effect.map_layer))
                return PlanDecodeError{"layers["+std::to_string(index)+"].effects.map_layer",
                    "displacement map layer id does not exist: "+effect.map_layer,
                    "displacement_map_source_missing","render-plan-relations"};
            if (plan.layers[by_id.at(effect.map_layer)].type == LayerType::Adjustment)
                return PlanDecodeError{"layers["+std::to_string(index)+"].effects.map_layer",
                    "displacement-map source must produce pixels; adjustment layers do not",
                    "displacement_map_source_not_raster","render-plan-relations"};
        }
    }

    std::vector<std::uint8_t> state(plan.layers.size(), 0);
    const auto visit = [&](const auto& self, std::size_t index) -> bool {
        if (state[index] == 1) return false;
        if (state[index] == 2) return true;
        state[index] = 1;
        if (plan.layers[index].parent) {
            const auto parent = by_id.at(*plan.layers[index].parent);
            if (!self(self, parent)) return false;
        }
        state[index] = 2;
        return true;
    };
    for (std::size_t index = 0; index < plan.layers.size(); ++index) {
        if (!visit(visit, index)) {
            return PlanDecodeError{
                "layers[" + std::to_string(index) + "].parent",
                "layer parent relationships must be acyclic",
                "layer_parent_cycle", "render-plan-relations"};
        }
    }

    // Parent transforms and image-producing references are graph dependencies.
    // Validate them together so cycles that cross parent, matte, text-mask, or
    // displacement-map edges fail during plan inspection instead of graph build.
    std::fill(state.begin(), state.end(), 0);
    const auto visit_dependencies = [&](const auto& self, std::size_t index) -> bool {
        if (state[index] == 1) return false;
        if (state[index] == 2) return true;
        state[index] = 1;
        const auto& layer = plan.layers[index];
        if (layer.parent && !self(self, by_id.at(*layer.parent))) return false;
        if (layer.track_matte && !self(self, by_id.at(layer.track_matte->source))) return false;
        if (layer.path_mask && layer.path_mask->type == "text" &&
            !self(self, by_id.at(layer.path_mask->text_source))) return false;
        for (const auto& mask : layer.masks)
            if (mask.type == "text" && !self(self, by_id.at(mask.text_source))) return false;
        for (const auto& effect : layer.effects) {
            if (effect.kind != LayerPlan::EffectKindPlan::DisplacementMap ||
                effect.map_field)
                continue;
            if (!self(self, by_id.at(effect.map_layer))) return false;
        }
        state[index] = 2;
        return true;
    };
    for (std::size_t index = 0; index < plan.layers.size(); ++index) {
        if (!visit_dependencies(visit_dependencies, index)) {
            return PlanDecodeError{
                "layers[" + std::to_string(index) + "]",
                "layer parent, track-matte, mask-source, and displacement-map dependencies must be acyclic",
                "layer_dependency_cycle", "render-plan-relations"};
        }
    }
    return std::nullopt;
}


} // namespace chronon3d::render_plan
