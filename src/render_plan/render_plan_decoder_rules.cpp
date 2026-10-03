#include "render_plan_decoder_detail.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d::render_plan::detail {

bool is_3d_property(const std::string& property) {
    // Closed 3D gate: these are the camera-backed layer properties. Other
    // primitive tracks remain valid in flat mode and must not opt in silently.
    return property == "position_z" || property == "rotation_x" ||
           property == "rotation_y";
}

std::optional<std::string> find_3d_track(const nlohmann::json& layer) {
    const auto inspect_tracks = [](const nlohmann::json& tracks)
        -> std::optional<std::string> {
        if (!tracks.is_array()) return std::nullopt;
        for (const auto& track : tracks) {
            if (track.is_object() && track.contains("property") &&
                is_3d_property(track.at("property").get<std::string>())) {
                return track.at("property").get<std::string>();
            }
        }
        return std::nullopt;
    };

    if (layer.contains("animation") && layer.at("animation").is_object() &&
        layer.at("animation").contains("tracks")) {
        if (auto property = inspect_tracks(layer.at("animation").at("tracks")))
            return property;
    }
    if (layer.contains("text_animators") && layer.at("text_animators").is_array()) {
        for (const auto& animator : layer.at("text_animators")) {
            if (animator.is_object() && animator.contains("properties")) {
                if (auto property = inspect_tracks(animator.at("properties")))
                    return property;
            }
        }
    }
    return std::nullopt;
}

std::optional<PlanDecodeError> apply_layout_stagger(std::vector<LayerPlan>& layers,
                                                    int canvas_width,
                                                    int canvas_height) {
    struct Group {
        std::vector<std::size_t> members;
        std::optional<LayerPlan::StaggerPlan> stagger;
        std::string kind;
        std::string id;
    };
    std::map<std::pair<std::string, std::string>, Group> groups;
    for (std::size_t i = 0; i < layers.size(); ++i) {
        auto collect = [&](std::string kind, const std::string& id,
                           const std::optional<LayerPlan::StaggerPlan>& stagger) -> std::optional<PlanDecodeError> {
            auto& group = groups[{kind, id}];
            group.kind = kind;
            group.id = id;
            group.members.push_back(i);
            if (!stagger) return std::nullopt;
            if (!group.stagger) {
                group.stagger = stagger;
                return std::nullopt;
            }
            const auto& a = *group.stagger;
            const auto& b = *stagger;
            if (a.per_item_frames != b.per_item_frames || a.order != b.order || a.seed != b.seed ||
                a.track_property != b.track_property || a.easing != b.easing)
                return PlanDecodeError{"layers[" + std::to_string(i) + "]",
                    "all members of a layout group must declare the same stagger", "stagger_group_mismatch",
                    "render-plan-layout-stagger"};
            return std::nullopt;
        };
        if (layers[i].flow_layout) {
            if (auto error = collect("flow", layers[i].flow_layout->group_id,
                                     layers[i].flow_layout->stagger)) return error;
        }
        if (layers[i].grid_layout) {
            if (auto error = collect("grid", layers[i].grid_layout->group_id,
                                     layers[i].grid_layout->stagger)) return error;
        }
        if (layers[i].stagger_group) {
            if (auto error = collect("run", layers[i].stagger_group->group_id,
                                     layers[i].stagger_group)) return error;
        }
    }
    const auto splitmix64 = [](std::uint64_t value) {
        value += 0x9e3779b97f4a7c15ULL;
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31);
    };
    for (const auto& [key, group] : groups) {
        (void)key;
        if (!group.stagger || group.stagger->per_item_frames == 0) continue;
        const auto& stagger = *group.stagger;
        std::vector<std::size_t> order(group.members.size());
        for (std::size_t rank = 0; rank < order.size(); ++rank) order[rank] = rank;
        if (stagger.order == "reverse") {
            std::reverse(order.begin(), order.end());
        } else if (stagger.order == "center_out") {
            std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                const auto da = std::abs(static_cast<std::int64_t>(2 * a) -
                                         static_cast<std::int64_t>(order.size() - 1));
                const auto db = std::abs(static_cast<std::int64_t>(2 * b) -
                                         static_cast<std::int64_t>(order.size() - 1));
                return da < db;
            });
        } else if (stagger.order == "random") {
            std::uint64_t state = stagger.seed;
            for (std::size_t i = order.size(); i > 1; --i) {
                state = splitmix64(state);
                const auto j = static_cast<std::size_t>(state % i);
                std::swap(order[i - 1], order[j]);
            }
        } else if (stagger.order == "spatial_x" || stagger.order == "spatial_y" ||
                   stagger.order == "radial" || stagger.order == "from_edges") {
            const auto position = [&](std::size_t rank) -> const LayerPlan& {
                return layers[group.members[rank]];
            };
            double center_x = 0.0;
            double center_y = 0.0;
            for (const auto rank : order) {
                center_x += position(rank).position[0];
                center_y += position(rank).position[1];
            }
            if (!order.empty()) {
                center_x /= static_cast<double>(order.size());
                center_y /= static_cast<double>(order.size());
            }
            const auto key = [&](std::size_t rank) {
                const auto& layer = position(rank);
                const double x = layer.position[0];
                const double y = layer.position[1];
                if (stagger.order == "spatial_x") return x;
                if (stagger.order == "spatial_y") return y;
                if (stagger.order == "radial")
                    return std::hypot(x - center_x, y - center_y);
                return std::min({x, y, static_cast<double>(canvas_width) - x,
                                 static_cast<double>(canvas_height) - y});
            };
            std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                return key(a) < key(b);
            });
        }
        std::vector<std::size_t> rank_by_member(group.members.size());
        for (std::size_t rank = 0; rank < order.size(); ++rank) rank_by_member[order[rank]] = rank;
        for (std::size_t member = 0; member < group.members.size(); ++member) {
            const auto rank = rank_by_member[member];
            const std::uint64_t offset = static_cast<std::uint64_t>(stagger.per_item_frames) * rank;
            auto& layer = layers[group.members[member]];
            const auto apply_track = [&](auto& track) -> std::optional<PlanDecodeError> {
                if (track.property != stagger.track_property) return std::nullopt;
                for (const auto& keyframe : track.keyframes) {
                    if (keyframe.frame.integral() > 1000000 - static_cast<std::int64_t>(offset))
                        return PlanDecodeError{"layers[" + std::to_string(group.members[member]) + "]",
                            "staggered keyframe exceeds the supported frame limit", "stagger_frame_overflow",
                            "render-plan-layout-stagger"};
                }
                for (auto& keyframe : track.keyframes)
                    keyframe.frame += Frame{static_cast<std::int64_t>(offset)};
                if (!stagger.easing.empty()) {
                    if (track.spring || track.easing == "spring" ||
                        std::any_of(track.keyframes.begin(), track.keyframes.end(),
                            [](const auto& keyframe) {
                                return keyframe.spring.has_value() || keyframe.easing == "spring";
                            }))
                        return PlanDecodeError{"layers[" + std::to_string(group.members[member]) + "]",
                            "stagger easing cannot override a spring track", "stagger_spring_easing_conflict",
                            "render-plan-layout-stagger"};
                    track.easing = stagger.easing;
                }
                return std::nullopt;
            };
            if (layer.animation) {
                for (auto& track : layer.animation->tracks)
                    if (auto error = apply_track(track)) return error;
            }
            for (auto& animator : layer.text_animators) {
                for (auto& track : animator.properties)
                    if (auto error = apply_track(track)) return error;
            }
        }
    }
    return std::nullopt;
}

void collect_final_key_timing_warnings(const nlohmann::json& value,
                                       std::vector<std::string>& warnings,
                                       bool keyframes) {
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it)
            collect_final_key_timing_warnings(it.value(), warnings, it.key() == "keyframes");
    } else if (value.is_array()) {
        if (keyframes && !value.empty() && value.back().is_object() &&
            (value.back().contains("interpolation") || value.back().contains("easing") ||
             value.back().contains("bezier_curve") || value.back().contains("roving") ||
             value.back().contains("temporal_in_dx") || value.back().contains("temporal_in_dy") ||
             value.back().contains("temporal_out_dx") || value.back().contains("temporal_out_dy")))
            warnings.emplace_back("animation_last_key_timing_ignored");
        for (const auto& item : value) collect_final_key_timing_warnings(item, warnings, false);
    }
}

void validate_per_key_timing(const nlohmann::json& value, bool keyframes,
                             bool scalar_audio_gain) {
    if (value.is_object()) {
        const bool audio_gain = value.contains("property") && value.contains("keyframes") &&
            value.value("property", std::string{}) == "gain_db";
        for (auto it = value.begin(); it != value.end(); ++it)
            validate_per_key_timing(it.value(), it.key() == "keyframes",
                scalar_audio_gain || audio_gain);
        return;
    }
    if (!value.is_array()) return;
    if (keyframes) {
        for (const auto& key : value) {
            if (!key.is_object()) continue;
            const auto interpolation = key.value("interpolation", std::string{});
            const auto easing = key.value("easing", std::string{});
            const bool has_curve = key.contains("bezier_curve");
            const bool has_handles = key.contains("temporal_in_dx") || key.contains("temporal_in_dy") ||
                key.contains("temporal_out_dx") || key.contains("temporal_out_dy");
            if (scalar_audio_gain && (key.contains("roving") || has_handles || key.contains("interpolation")))
                throw std::runtime_error("audio gain does not support roving, temporal handles, or per-key interpolation");
            if (key.contains("roving") && !key.at("roving").is_boolean())
                throw std::runtime_error("roving must be a boolean");
            if (!interpolation.empty() && interpolation != "linear" &&
                interpolation != "hold" && interpolation != "bezier" &&
                interpolation != "auto_bezier")
                throw std::runtime_error("keyframe interpolation must be linear, hold, bezier, or auto_bezier");
            const auto finite_handle = [&](const char* name) {
                return !key.contains(name) || (key.at(name).is_number() &&
                    std::isfinite(key.at(name).get<double>()));
            };
            if (!finite_handle("temporal_in_dx") || !finite_handle("temporal_in_dy") ||
                !finite_handle("temporal_out_dx") || !finite_handle("temporal_out_dy"))
                throw std::runtime_error("temporal bezier handles must be finite numbers");
            if (key.contains("temporal_in_dx") != key.contains("temporal_in_dy") ||
                key.contains("temporal_out_dx") != key.contains("temporal_out_dy"))
                throw std::runtime_error("temporal handle dx and dy must be provided together");
            if (key.contains("temporal_in_dx") && key.at("temporal_in_dx").get<double>() > 0.0)
                throw std::runtime_error("temporal_in_dx must be non-positive");
            if (key.contains("temporal_out_dx") && key.at("temporal_out_dx").get<double>() < 0.0)
                throw std::runtime_error("temporal_out_dx must be non-negative");
            if (!interpolation.empty()) {
                if ((interpolation != "bezier" && !easing.empty()) ||
                    (has_curve && interpolation != "bezier") ||
                    (has_curve && has_handles) ||
                    (interpolation == "bezier" && !has_curve && !has_handles))
                    throw std::runtime_error("keyframe interpolation requires only its matching bezier_curve or temporal handles");
            } else if ((easing == "bezier") != has_curve) {
                throw std::runtime_error("per-key easing='bezier' and bezier_curve must be provided together");
            }
            if (key.value("roving", false) &&
                (interpolation == "bezier" || interpolation == "auto_bezier"))
                throw std::runtime_error("roving cannot be combined with Bezier interpolation");
            if (has_handles && (interpolation != "bezier" || has_curve))
                throw std::runtime_error("temporal handles require interpolation='bezier' without bezier_curve");
            if (interpolation == "auto_bezier" &&
                (key.contains("temporal_in_dx") || key.contains("temporal_in_dy") ||
                 key.contains("temporal_out_dx") || key.contains("temporal_out_dy") || has_curve))
                throw std::runtime_error("auto_bezier cannot be combined with authored Bezier data");
            if (interpolation == "auto_bezier" && key.value("roving", false))
                throw std::runtime_error("roving and auto_bezier cannot be combined on a keyframe");
            if (interpolation == "auto_bezier" && key.value("roving", false))
                throw std::runtime_error("roving and auto_bezier cannot be combined on a keyframe");
            if (!easing.empty() && easing != "linear" && easing != "in_quad" &&
                easing != "out_quad" && easing != "in_out_quad" && easing != "in_cubic" &&
                easing != "out_cubic" && easing != "in_out_cubic" && easing != "in_expo" &&
                easing != "out_expo" && easing != "in_out_expo" && easing != "in_sine" &&
                easing != "out_sine" && easing != "in_out_sine" && easing != "in_back" &&
                easing != "out_back" && easing != "in_out_back" && easing != "in_elastic" &&
                easing != "out_elastic" && easing != "in_out_elastic" && easing != "in_bounce" &&
                easing != "out_bounce" && easing != "in_out_bounce" && easing != "smoothstep" &&
                easing != "hold" && easing != "bezier" && easing != "spring")
                throw std::runtime_error("unsupported per-key animation easing: " + easing);
            if (has_curve) {
                const auto& curve = key.at("bezier_curve");
                if (!curve.is_array() || curve.size() != 4)
                    throw std::runtime_error("per-key bezier_curve must contain [x1, y1, x2, y2]");
                const auto x1 = curve.at(0).get<float>();
                const auto y1 = curve.at(1).get<float>();
                const auto x2 = curve.at(2).get<float>();
                const auto y2 = curve.at(3).get<float>();
                if (!std::isfinite(x1) || !std::isfinite(y1) || !std::isfinite(x2) ||
                    !std::isfinite(y2) || x1 < 0.0f || x1 > 1.0f || x2 < 0.0f ||
                    x2 > 1.0f || y1 < -10.0f || y1 > 10.0f || y2 < -10.0f || y2 > 10.0f)
                    throw std::runtime_error("per-key bezier_curve handles are out of range");
            }
        }
    }
    for (const auto& item : value) validate_per_key_timing(item, false, scalar_audio_gain);
}


} // namespace chronon3d::render_plan::detail
