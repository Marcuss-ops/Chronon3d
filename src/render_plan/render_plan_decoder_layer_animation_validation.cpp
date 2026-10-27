#include "render_plan_decoder_detail.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace chronon3d::render_plan::detail {

void validate_layer_animation_fields(LayerPlan& layer, const nlohmann::json& value) {
    layer.auto_orient = value.value("auto_orient", false);
    if (layer.auto_orient) {
        bool has_position = false;
        bool has_position_span = false;
        bool has_rotation = false;
        if (layer.animation) {
            for (const auto& track : layer.animation->tracks) {
                has_position |= track.property == "position" || track.property == "position_x" ||
                    track.property == "position_y" || track.property == "position_z";
                has_rotation |= track.property == "rotation" || track.property == "rotation_x" ||
                    track.property == "rotation_y" || track.property == "rotation_z";
            }
        }
        if (!has_position)
            throw std::runtime_error("auto_orient requires an animated position track");
        if (has_rotation)
            throw std::runtime_error("auto_orient cannot be combined with explicit rotation tracks");
        for (const auto& track : layer.animation->tracks)
            if (track.property == "position" || track.property == "position_x" ||
                track.property == "position_y" || track.property == "position_z") {
                if (track.keyframes.size() >= 2 &&
                    track.keyframes.back().frame.integral() > track.keyframes.front().frame.integral())
                    has_position_span = true;
                if (track.keyframes.size() >= 2 &&
                    track.keyframes.back().frame.integral() - track.keyframes.front().frame.integral() > 65535)
                    throw std::runtime_error("auto_orient position track span exceeds 65535 frames");
            }
        if (!has_position_span)
            throw std::runtime_error("auto_orient requires a position track spanning at least two frames");
    }
    if (layer.animation) {
        std::unordered_set<std::string> light_track_properties;
        for (const auto& track : layer.animation->tracks) {
            if (track.property != "light_intensity" && track.property != "light_color" &&
                track.property != "light_direction") continue;
            if (layer.type != LayerType::Light || !layer.light)
                throw std::runtime_error(track.property + " animation requires a light layer");
            if (!light_track_properties.insert(track.property).second)
                throw std::runtime_error("layer has duplicate " + track.property + " animation tracks");
            if (track.property == "light_direction" && layer.light->kind == LightKindPlan::Radial)
                throw std::runtime_error("light_direction animation requires a non-radial light");
            for (const auto& key : track.keyframes) {
                if (track.property == "light_direction") {
                    if (key.value.size() != 3 || std::any_of(key.value.begin(), key.value.end(),
                            [](float component) { return !std::isfinite(component); }))
                        throw std::runtime_error("light_direction keyframes require three finite components");
                    const float length_squared = key.value[0] * key.value[0] +
                        key.value[1] * key.value[1] + key.value[2] * key.value[2];
                    if (!std::isfinite(length_squared) || length_squared <= 1.0e-8f)
                        throw std::runtime_error("light_direction keyframes must be non-zero");
                } else if (track.property == "light_intensity") {
                    if (key.value.size() != 1 || !std::isfinite(key.value.front()) ||
                        key.value.front() < 0.0f || key.value.front() > 16.0f)
                        throw std::runtime_error("light_intensity keyframes must be finite scalars in [0, 16]");
                } else if (key.value.size() != 4 || std::any_of(key.value.begin(), key.value.end(),
                        [](float component) {
                            return !std::isfinite(component) || component < 0.0f || component > 1.0f;
                        })) {
                    throw std::runtime_error("light_color keyframes must be finite RGBA values in [0, 1]");
                }
            }
        }
        for (auto& track : layer.animation->tracks) {
            if (track.property != "fill_color") continue;
            std::stable_sort(track.keyframes.begin(), track.keyframes.end(),
                [](const auto& left, const auto& right) {
                    return left.frame < right.frame;
                });
        }
        const AnimationTrackPlan* fill_color_track = nullptr;
        const AnimationTrackPlan* stroke_width_track = nullptr;
        const AnimationTrackPlan* stroke_color_track = nullptr;
        std::vector<std::string> transform_scalar_tracks;
        std::vector<std::string> shadow_track_properties;
        for (const auto& track : layer.animation->tracks) {
            if (track.property == "stroke_dash_offset") {
                if (!layer.shape || !layer.shape->stroke)
                    throw std::runtime_error("stroke_dash_offset animation requires a shape with a stroke");
                for (const auto& key : track.keyframes)
                    if (key.value.size() != 1 || !std::isfinite(key.value.front()))
                        throw std::runtime_error("stroke_dash_offset keyframes must be finite scalar values");
            } else if (track.property == "skew" || track.property == "skew_axis") {
                if (std::find(transform_scalar_tracks.begin(), transform_scalar_tracks.end(),
                              track.property) != transform_scalar_tracks.end())
                    throw std::runtime_error("layer has duplicate " + track.property + " animation tracks");
                transform_scalar_tracks.push_back(track.property);
                std::vector<std::int64_t> frames;
                for (const auto& key : track.keyframes) {
                    const float low = track.property == "skew" ? -80.0f : -1000000.0f;
                    const float high = track.property == "skew" ? 80.0f : 1000000.0f;
                    if (key.value.size() != 1 || !std::isfinite(key.value.front()) ||
                        key.value.front() < low || key.value.front() > high ||
                        key.frame.integral() < 0 || key.frame.integral() > 1000000)
                        throw std::runtime_error(track.property +
                            " animation requires bounded frames and scalar values");
                    frames.push_back(key.frame.integral());
                }
                std::sort(frames.begin(), frames.end());
                if (std::adjacent_find(frames.begin(), frames.end()) != frames.end())
                    throw std::runtime_error(track.property + " keyframe frames must be unique");
            } else if (track.property == "stroke_width") {
                if (!layer.shape || !layer.shape->stroke)
                    throw std::runtime_error("stroke_width animation requires a shape with a stroke");
                if (stroke_width_track)
                    throw std::runtime_error("layer has duplicate stroke_width animation tracks");
                stroke_width_track = &track;
                std::vector<std::int64_t> frames;
                for (const auto& key : track.keyframes) {
                    if (key.value.size() != 1 || !std::isfinite(key.value.front()) ||
                        key.value.front() < 0.0f || key.frame.integral() < 0 ||
                        key.frame.integral() > 1000000)
                        throw std::runtime_error("stroke_width keyframes require bounded frames and finite non-negative scalar values");
                    frames.push_back(key.frame.integral());
                }
                std::sort(frames.begin(), frames.end());
                if (std::adjacent_find(frames.begin(), frames.end()) != frames.end())
                    throw std::runtime_error("stroke_width keyframe frames must be unique");
            } else if (track.property == "stroke_color") {
                if (!layer.shape || !layer.shape->stroke ||
                    layer.shape->stroke->gradient)
                    throw std::runtime_error("stroke_color animation requires a solid stroke on a shape");
                if (stroke_color_track)
                    throw std::runtime_error("layer has duplicate stroke_color animation tracks");
                stroke_color_track = &track;
                std::vector<std::int64_t> frames;
                for (const auto& key : track.keyframes) {
                    if (key.value.size() != 4 || key.frame.integral() < 0 ||
                        key.frame.integral() > 1000000 ||
                        std::any_of(key.value.begin(), key.value.end(), [](float component) {
                            return !std::isfinite(component) || component < 0.0f || component > 1.0f;
                        }))
                        throw std::runtime_error("stroke_color keyframes require bounded frames and RGBA values in [0, 1]");
                    frames.push_back(key.frame.integral());
                }
                std::sort(frames.begin(), frames.end());
                if (std::adjacent_find(frames.begin(), frames.end()) != frames.end())
                    throw std::runtime_error("stroke_color keyframe frames must be unique");
            } else if (track.property.rfind("shadow_", 0) == 0) {
                if (layer.type != LayerType::Shape || !layer.style)
                    throw std::runtime_error("shadow animation requires a shape layer with style shadows");
                const std::size_t shadow_count = layer.style->shadow ? 1 : layer.style->shadows.size();
                std::string property_suffix;
                std::size_t index = 0;
                bool recognized = false;
                for (std::size_t candidate = 0; candidate < 3 && !recognized; ++candidate) {
                    for (const auto suffix : {"_opacity", "_blur", "_offset_x", "_offset_y", "_color"}) {
                        const std::string prefix = "shadow_" + std::to_string(candidate) + suffix;
                        if (track.property == prefix) {
                            index = candidate;
                            property_suffix = suffix;
                            recognized = true;
                            break;
                        }
                    }
                }
                if (!recognized)
                    throw std::runtime_error("unsupported shadow animation property: " + track.property);
                if (index >= shadow_count)
                    throw std::runtime_error("shadow animation refers to an absent shadow style");
                if (std::find(shadow_track_properties.begin(), shadow_track_properties.end(),
                              track.property) != shadow_track_properties.end())
                    throw std::runtime_error("layer has duplicate shadow animation tracks");
                shadow_track_properties.push_back(track.property);
                std::vector<std::int64_t> frames;
                const bool color_track = property_suffix == "_color";
                for (const auto& key : track.keyframes) {
                    bool valid_value = false;
                    if (color_track) {
                        valid_value = key.value.size() == 4 &&
                            std::all_of(key.value.begin(), key.value.end(), [](float component) {
                                return std::isfinite(component) && component >= 0.0f && component <= 1.0f;
                            });
                    } else if (key.value.size() == 1 && std::isfinite(key.value.front())) {
                        const float v = key.value.front();
                        valid_value = property_suffix == "_opacity" ? (v >= 0.0f && v <= 1.0f)
                            : (property_suffix == "_blur" ? v >= 0.0f : true);
                    }
                    if (!valid_value || key.frame.integral() < 0 || key.frame.integral() > 1000000)
                        throw std::runtime_error("shadow animation keyframes are outside supported bounds");
                    frames.push_back(key.frame.integral());
                }
                std::sort(frames.begin(), frames.end());
                if (std::adjacent_find(frames.begin(), frames.end()) != frames.end())
                    throw std::runtime_error("shadow animation keyframe frames must be unique");
            } else if (track.property == "fill_color") {
                if (fill_color_track)
                    throw std::runtime_error("layer has duplicate fill_color animation tracks");
                fill_color_track = &track;
                if (layer.fill_gradient_animation)
                    throw std::runtime_error(
                        "fill_color and fill_gradient_animation tracks are mutually exclusive");
                if (layer.type == LayerType::Color) {
                    // Color layers use this as their solid paint.
                } else if (layer.type == LayerType::Shape && layer.shape &&
                           (layer.shape->type == ShapeContentTypePlan::Rect ||
                            layer.shape->type == ShapeContentTypePlan::RoundedRect ||
                            layer.shape->type == ShapeContentTypePlan::Ellipse ||
                            layer.shape->type == ShapeContentTypePlan::Star ||
                            layer.shape->type == ShapeContentTypePlan::Polygon ||
                            layer.shape->type == ShapeContentTypePlan::Arrow ||
                            layer.shape->type == ShapeContentTypePlan::DotGrid ||
                            layer.shape->type == ShapeContentTypePlan::RingSegment) &&
                           layer.shape->fill_color && !layer.shape->gradient) {
                    // Only explicit solid fills are animatable; gradients use
                    // the typed fill_gradient_animation track instead.
                } else {
                    throw std::runtime_error(
                        "fill_color animation requires a color layer or a solid filled closed shape");
                }
                std::vector<std::int64_t> frames;
                frames.reserve(track.keyframes.size());
                for (const auto& key : track.keyframes) {
                    if (key.frame.integral() < 0 || key.frame.integral() > 1000000 ||
                        key.value.size() != 4 || std::any_of(key.value.begin(), key.value.end(),
                        [](float component) {
                            return !std::isfinite(component) || component < 0.0f || component > 1.0f;
                        }))
                        throw std::runtime_error("fill_color keyframes require bounded frames and RGBA values in [0, 1]");
                    frames.push_back(key.frame.integral());
                }
                std::sort(frames.begin(), frames.end());
                if (std::adjacent_find(frames.begin(), frames.end()) != frames.end())
                    throw std::runtime_error("fill_color keyframe frames must be unique");
            }
        }
    }

}

}  // namespace chronon3d::render_plan::detail
