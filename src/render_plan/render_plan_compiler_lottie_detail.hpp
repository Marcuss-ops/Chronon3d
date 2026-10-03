#pragma once
#include <chronon3d/render_plan/render_plan.hpp>
#include <nlohmann/json_fwd.hpp>
#include <array>
#include <optional>
#include <string>
#include <vector>
namespace chronon3d::render_plan::detail {
bool lottie_identity_transform(const nlohmann::json&);
bool lottie_static_vec2_property(const nlohmann::json&, const char*, std::array<float,2>&);
bool lottie_static_scalar_property(const nlohmann::json&, const char*, float&);
bool lottie_read_transform_property(const nlohmann::json&, const char*, std::vector<float>&, bool&);
bool lottie_decode_transform_track(const nlohmann::json&, const char*, const char*, float, AnimationTrackPlan&);
std::optional<std::string> decode_lottie_layer_transform(LayerPlan&, const nlohmann::json&, float&, float&);
}
