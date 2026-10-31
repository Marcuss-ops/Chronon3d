#pragma once

#include <chronon3d/render_plan/render_plan.hpp>
#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace chronon3d::render_plan::detail::lottie_expansion_support {

std::optional<std::string> attach_lottie_track_matte(
    const nlohmann::json& authored, std::size_t layer_position,
    const std::unordered_map<std::int64_t, std::string>& ids,
    const std::vector<std::int64_t>& indices,
    const nlohmann::json& authored_layers, LayerPlan& target);
std::optional<std::string> lottie_image_asset_path(
    const LayerPlan& source, const nlohmann::json& composition_document,
    const nlohmann::json& authored_layer, std::string& logical_path,
    std::array<float, 2>& dimensions,
    std::shared_ptr<const std::vector<std::byte>>& embedded_payload);
std::optional<std::string> center_lottie_image_transform(
    LayerPlan& layer, const nlohmann::json& composition,
    float anchor_x, float anchor_y, const std::array<float, 2>& dimensions);
std::optional<std::string> center_lottie_layer_position(
    LayerPlan& layer, const nlohmann::json& composition);
std::optional<std::string> preserve_nested_lottie_local_position(
    LayerPlan& layer, const nlohmann::json& composition);
std::optional<std::string> decode_lottie_precomp_time_remap(
    const nlohmann::json& authored_layer, const nlohmann::json& composition,
    double composition_offset, double time_scale, Frame start,
    TimeRemapPlan& remap);

} // namespace chronon3d::render_plan::detail::lottie_expansion_support
