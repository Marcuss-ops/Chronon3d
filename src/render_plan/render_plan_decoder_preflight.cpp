#include <chronon3d/render_plan/animation_property_registry.hpp>
#include "render_plan_decoder_detail.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <utility>

namespace chronon3d::render_plan::detail {

std::optional<chronon3d::render_plan::PlanDecodeError>
find_unregistered_animation_property(const nlohmann::json& root) {
    if (!root.is_object() || !root.contains("layers") || !root.at("layers").is_array())
        return std::nullopt;

    const auto& layers = root.at("layers");
    for (std::size_t layer_index = 0; layer_index < layers.size(); ++layer_index) {
        const auto& layer = layers[layer_index];
        if (!layer.is_object() || !layer.contains("animation")) continue;
        const auto& animation = layer.at("animation");
        if (!animation.is_object() || !animation.contains("tracks") ||
            !animation.at("tracks").is_array()) continue;

        const auto& tracks = animation.at("tracks");
        for (std::size_t track_index = 0; track_index < tracks.size(); ++track_index) {
            const auto& track = tracks[track_index];
            if (!track.is_object() || !track.contains("property") ||
                !track.at("property").is_string()) continue;

            const auto property = track.at("property").get<std::string>();
            if (chronon3d::render_plan::find_animation_property(property)) continue;

            std::string message = "unregistered property '" + property + "'";
            if (const auto nearest = chronon3d::render_plan::nearest_animation_property_name(property);
                !nearest.empty())
                message += "; did you mean '" + std::string{nearest} + "'?";
            return chronon3d::render_plan::PlanDecodeError{
                "layers[" + std::to_string(layer_index) + "].animation.tracks[" +
                    std::to_string(track_index) + "].property",
                std::move(message)};
        }
    }
    return std::nullopt;
}

} // namespace chronon3d::render_plan::detail
