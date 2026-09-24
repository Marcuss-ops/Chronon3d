#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace render_plan_test {

inline nlohmann::json base_plan(nlohmann::json layers = nlohmann::json::array()) {
    return {
        {"schema", "chronon.render-plan.v3"},
        {"version", 3},
        {"canvas", {{"width", 320}, {"height", 180}, {"fps_num", 30},
                     {"fps_den", 1}, {"duration_frames", 12}}},
        {"layers", std::move(layers)},
        {"output", {{"path", "out.png"}}}
    };
}

inline nlohmann::json shape_layer(nlohmann::json shape) {
    return {
        {"id", "shape"},
        {"type", "shape"},
        {"size", {40.0, 30.0}},
        {"shape", std::move(shape)}
    };
}

inline nlohmann::json gradient() {
    return {
        {"type", "linear"},
        {"start", {0.0, 0.0}},
        {"end", {1.0, 0.0}},
        {"spread", "reflect"},
        {"color_stops", nlohmann::json::array({
            {{"position", 0.0}, {"color", {1.0, 0.0, 0.0, 1.0}}},
            {{"position", 1.0}, {"color", {0.0, 0.0, 1.0, 1.0}}}
        })},
        {"opacity_stops", nlohmann::json::array({
            {{"position", 0.0}, {"opacity", 0.25}},
            {{"position", 1.0}, {"opacity", 0.9}}
        })}
    };
}

inline nlohmann::json v2_legacy_plan() {
    return {
        {"schema", "chronon.render-plan.v2"},
        {"version", 2},
        {"canvas", {{"width", 320}, {"height", 180}, {"fps_num", 30},
                     {"fps_den", 1}, {"duration_frames", 12}}},
        {"layers", nlohmann::json::array({
            {{"id", "legacy-color"}, {"type", "color"},
             {"color", {0.1, 0.2, 0.3, 1.0}}}
        })},
        {"output", {{"path", "out.png"}}}
    };
}

}  // namespace render_plan_test
