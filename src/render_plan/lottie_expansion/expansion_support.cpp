#include "render_plan_compiler_detail.hpp"
#include "render_plan_compiler_lottie_detail.hpp"
#include "lottie_expansion_support.hpp"
#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/render_plan/easing_contract.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace chronon3d::render_plan::detail::lottie_expansion_support {
using json = nlohmann::json;


std::optional<TrackMatteTypePlan> lottie_track_matte_type(const json& layer) {
    if (!layer.contains("tt")) return std::nullopt;
    if (!layer["tt"].is_number_integer())
        throw std::runtime_error("Lottie track matte type must be an integer");
    switch (layer["tt"].get<int>()) {
        case 1: return TrackMatteTypePlan::Alpha;
        case 2: return TrackMatteTypePlan::AlphaInverted;
        case 3: return TrackMatteTypePlan::Luma;
        case 4: return TrackMatteTypePlan::LumaInverted;
        default: throw std::runtime_error("Lottie track matte type is outside supported values 1..4");
    }
}

std::optional<std::string> attach_lottie_track_matte(
    const json& authored, std::size_t layer_position,
    const std::unordered_map<std::int64_t, std::string>& ids,
    const std::vector<std::int64_t>& indices, const json& authored_layers,
    LayerPlan& target) {
    target.track_matte.reset();
    const auto matte_type = lottie_track_matte_type(authored);
    if (!matte_type) return std::nullopt;
    std::int64_t matte_index{};
    if (authored.contains("tp")) {
        if (!authored["tp"].is_number_integer())
            return std::string("Lottie track matte tp must be an integer layer index");
        matte_index = authored["tp"].get<std::int64_t>();
    } else {
        // ThorVG follows Bodymovin's adjacent-layer convention: the consumer
        // uses the immediately preceding authored layer as its matte source.
        if (layer_position == 0)
            return std::string("Lottie track matte has no preceding source layer");
        matte_index = indices[layer_position - 1];
    }
    const auto matte_id = ids.find(matte_index);
    if (matte_id == ids.end() || matte_index == indices[layer_position])
        return std::string("Lottie track matte source layer is missing or self-referential");
    const auto source_position = static_cast<std::size_t>(std::distance(
        indices.begin(), std::find(indices.begin(), indices.end(), matte_index)));
    if (source_position >= authored_layers.size() ||
        (authored_layers[source_position].value("ty", -1) != 4 &&
         authored_layers[source_position].value("ty", -1) != 2) ||
        authored_layers[source_position].value("hd", false))
        return std::string("Lottie track matte source must be a visible 2D shape or image layer");
    target.track_matte = TrackMattePlan{
        .source = matte_id->second,
        .type = *matte_type};
    return std::nullopt;
}

std::optional<std::string> decode_lottie_embedded_image(
    std::string_view data_url, std::string& identity,
    std::shared_ptr<const std::vector<std::byte>>& payload) {
    constexpr std::size_t kMaxEncodedBytes = 10U * 1024U * 1024U;
    constexpr std::size_t kMaxDecodedBytes = 8U * 1024U * 1024U;
    std::string_view suffix;
    std::string extension;
    if (data_url.starts_with("data:image/png;base64,")) {
        suffix = data_url.substr(22);
        extension = "png";
    } else if (data_url.starts_with("data:image/jpeg;base64,")) {
        suffix = data_url.substr(23);
        extension = "jpg";
    } else {
        return std::string("Lottie embedded images must use base64 PNG or JPEG data URLs");
    }
    if (suffix.empty() || suffix.size() > kMaxEncodedBytes || suffix.size() % 4U != 0U)
        return std::string("Lottie embedded image payload is empty, malformed, or exceeds 10 MiB encoded");
    const auto decode_digit = [](char ch) -> int {
        if (ch >= 'A' && ch <= 'Z') return ch - 'A';
        if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
        if (ch >= '0' && ch <= '9') return ch - '0' + 52;
        if (ch == '+') return 62;
        if (ch == '/') return 63;
        return -1;
    };
    std::vector<std::byte> decoded;
    decoded.reserve((suffix.size() / 4U) * 3U);
    for (std::size_t offset = 0; offset < suffix.size(); offset += 4U) {
        const bool last = offset + 4U == suffix.size();
        const int a = decode_digit(suffix[offset]);
        const int b = decode_digit(suffix[offset + 1U]);
        const bool pad2 = suffix[offset + 2U] == '=';
        const bool pad3 = suffix[offset + 3U] == '=';
        const int c = pad2 ? 0 : decode_digit(suffix[offset + 2U]);
        const int d = pad3 ? 0 : decode_digit(suffix[offset + 3U]);
        if (a < 0 || b < 0 || c < 0 || d < 0 || (!last && (pad2 || pad3)) ||
            (pad2 && !pad3) || (pad2 && (b & 0x0f) != 0) ||
            (pad3 && !pad2 && (c & 0x03) != 0))
            return std::string("Lottie embedded image payload has invalid base64 encoding");
        const auto word = (static_cast<std::uint32_t>(a) << 18U) |
            (static_cast<std::uint32_t>(b) << 12U) |
            (static_cast<std::uint32_t>(c) << 6U) | static_cast<std::uint32_t>(d);
        decoded.push_back(static_cast<std::byte>((word >> 16U) & 0xffU));
        if (!pad2) decoded.push_back(static_cast<std::byte>((word >> 8U) & 0xffU));
        if (!pad3) decoded.push_back(static_cast<std::byte>(word & 0xffU));
        if (decoded.size() > kMaxDecodedBytes)
            return std::string("Lottie embedded image payload exceeds 8 MiB decoded");
    }
    const bool png_signature = extension == "png" && decoded.size() >= 8U &&
        decoded[0] == std::byte{0x89} && decoded[1] == std::byte{'P'} &&
        decoded[2] == std::byte{'N'} && decoded[3] == std::byte{'G'} &&
        decoded[4] == std::byte{0x0d} && decoded[5] == std::byte{0x0a} &&
        decoded[6] == std::byte{0x1a} && decoded[7] == std::byte{0x0a};
    const bool jpeg_signature = extension == "jpg" && decoded.size() >= 3U &&
        decoded[0] == std::byte{0xff} && decoded[1] == std::byte{0xd8} &&
        decoded[2] == std::byte{0xff};
    if (!png_signature && !jpeg_signature)
        return std::string("Lottie embedded image bytes do not match their declared PNG/JPEG MIME type");
    const auto* data = reinterpret_cast<const char*>(decoded.data());
    const auto digest = chronon3d::assets::sha256_string(
        std::string_view(data, decoded.size()));
    identity = "chronon3d-embedded:" + digest.hex() + "." + extension;
    payload = std::make_shared<const std::vector<std::byte>>(std::move(decoded));
    return std::nullopt;
}

std::optional<std::string> lottie_image_asset_path(
    const LayerPlan& source, const json& composition_document,
    const json& authored_layer, std::string& logical_path,
    std::array<float, 2>& dimensions,
    std::shared_ptr<const std::vector<std::byte>>& embedded_payload) {
    if (!authored_layer.contains("refId") || !authored_layer["refId"].is_string() ||
        !composition_document.contains("assets") || !composition_document["assets"].is_array())
        return std::string("Lottie image layer has no asset reference table");
    const auto ref = authored_layer["refId"].get<std::string>();
    const auto asset = std::find_if(composition_document["assets"].begin(),
        composition_document["assets"].end(), [&](const auto& item) {
            return item.is_object() && item.value("id", std::string{}) == ref;
        });
    if (asset == composition_document["assets"].end())
        return std::string("Lottie image refId could not be resolved");
    if (!asset->contains("p") || !(*asset)["p"].is_string())
        return std::string("Lottie image asset is missing its path");
    const auto embedded = asset->value("e", 0);
    if (embedded != 0 && embedded != 1)
        return std::string("Lottie image asset embedded flag must be 0 or 1");
    if (embedded == 1) {
        const std::string data_url = (*asset)["p"].get<std::string>();
        if (auto error = decode_lottie_embedded_image(
                data_url, logical_path, embedded_payload)) return *error;
        const double width = asset->value("w", 0.0);
        const double height = asset->value("h", 0.0);
        if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0 || height <= 0.0 ||
            width > 16384.0 || height > 16384.0 || width * height > 16777216.0)
            return std::string("Lottie embedded image dimensions exceed bounded pixel limits");
        dimensions = {static_cast<float>(width), static_cast<float>(height)};
        return std::nullopt;
    }
    const std::string file = (*asset)["p"].get<std::string>();
    const std::string directory = asset->value("u", std::string{});
    const std::filesystem::path file_path(file);
    const std::filesystem::path directory_path(directory);
    const auto has_traversal = [](const std::filesystem::path& path) {
        return path.is_absolute() || std::any_of(path.begin(), path.end(),
            [](const auto& component) { return component == ".."; });
    };
    if (file.empty() || has_traversal(file_path) || has_traversal(directory_path))
        return std::string("Lottie image path must be a non-empty relative path without traversal");
    if (!source.shape || !source.shape->lottie_asset)
        return std::string("Lottie image layer has no logical source document path");
    const auto lottie_path = std::filesystem::path(*source.shape->lottie_asset);
    const auto relative = (lottie_path.parent_path() / directory_path / file_path).lexically_normal();
    if (relative.empty() || relative.is_absolute() || has_traversal(relative))
        return std::string("Lottie image asset path escapes the mounted asset root");
    const double width = asset->value("w", 0.0);
    const double height = asset->value("h", 0.0);
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0 || height <= 0.0 ||
        width > 32768.0 || height > 32768.0)
        return std::string("Lottie image dimensions must be finite and in (0,32768]");
    logical_path = relative.generic_string();
    dimensions = {static_cast<float>(width), static_cast<float>(height)};
    return std::nullopt;
}

std::optional<std::string> center_lottie_image_transform(
    LayerPlan& layer, const json& composition, float anchor_x, float anchor_y,
    const std::array<float, 2>& dimensions) {
    const float canvas_width = composition.value("w", 0.0f);
    const float canvas_height = composition.value("h", 0.0f);
    if (!std::isfinite(canvas_width) || !std::isfinite(canvas_height) ||
        canvas_width <= 0.0f || canvas_height <= 0.0f)
        return std::string("Lottie image composition dimensions are invalid");

    const bool centered_anchor = std::abs(anchor_x - dimensions[0] * 0.5f) < 1e-4f &&
                                 std::abs(anchor_y - dimensions[1] * 0.5f) < 1e-4f;
    const auto has_track = [&](std::string_view property) {
        return layer.animation && std::any_of(layer.animation->tracks.begin(),
            layer.animation->tracks.end(), [&](const auto& track) {
                return track.property == property;
            });
    };
    if (!centered_anchor && (has_track("position") || has_track("scale") ||
                             has_track("rotation_z")))
        return std::string("animated Lottie image transforms with a non-centered anchor are unsupported");

    if (layer.position_dimensions >= 2) {
        layer.position[0] -= canvas_width * 0.5f;
        layer.position[1] -= canvas_height * 0.5f;
        if (!centered_anchor) {
            const float dx = (dimensions[0] * 0.5f - anchor_x) * layer.scale[0];
            const float dy = (dimensions[1] * 0.5f - anchor_y) * layer.scale[1];
            const float radians = layer.rotation[2] * 0.01745329251994329577f;
            layer.position[0] += std::cos(radians) * dx - std::sin(radians) * dy;
            layer.position[1] += std::sin(radians) * dx + std::cos(radians) * dy;
        }
    }
    if (layer.animation) {
        for (auto& track : layer.animation->tracks) {
            if (track.property != "position") continue;
            for (auto& key : track.keyframes) {
                if (key.value.size() < 2)
                    return std::string("Lottie image position keyframes require at least two components");
                key.value[0] -= canvas_width * 0.5f;
                key.value[1] -= canvas_height * 0.5f;
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> center_lottie_layer_position(
    LayerPlan& layer, const json& composition) {
    const float canvas_width = composition.value("w", 0.0f);
    const float canvas_height = composition.value("h", 0.0f);
    if (!std::isfinite(canvas_width) || !std::isfinite(canvas_height) ||
        canvas_width <= 0.0f || canvas_height <= 0.0f)
        return std::string("Lottie composition dimensions are invalid");
    if (layer.position_dimensions >= 2) {
        layer.position[0] -= canvas_width * 0.5f;
        layer.position[1] -= canvas_height * 0.5f;
    }
    if (layer.animation) {
        for (auto& track : layer.animation->tracks) {
            if (track.property != "position") continue;
            for (auto& key : track.keyframes) {
                if (key.value.size() < 2)
                    return std::string("Lottie position keyframes require at least two components");
                key.value[0] -= canvas_width * 0.5f;
                key.value[1] -= canvas_height * 0.5f;
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> preserve_nested_lottie_local_position(
    LayerPlan& layer, const json& root_composition) {
    const float canvas_width = root_composition.value("w", 0.0f);
    const float canvas_height = root_composition.value("h", 0.0f);
    if (!std::isfinite(canvas_width) || !std::isfinite(canvas_height) ||
        canvas_width <= 0.0f || canvas_height <= 0.0f)
        return std::string("Lottie root composition dimensions are invalid");
    // Shape/Text layer lowering converts its stored position from top-left
    // canvas coordinates to the centered scene basis. A nested Lottie layer's
    // coordinates are local to its parent, so cancel that root-canvas shift;
    // its local position is then composed with the parent's transform once.
    if (layer.position_dimensions >= 2) {
        layer.position[0] += canvas_width * 0.5f;
        layer.position[1] += canvas_height * 0.5f;
    }
    return std::nullopt;
}

std::optional<std::string> decode_lottie_precomp_time_remap(
    const json& authored, const json& parent_composition, double composition_offset,
    double time_scale, Frame container_start,
    chronon3d::render_plan::TimeRemapPlan& remap) {
    const double fps = parent_composition.value("fr", 0.0);
    if (!std::isfinite(fps) || fps <= 0.0 || fps > 1000.0 ||
        !std::isfinite(composition_offset) || !std::isfinite(time_scale) ||
        time_scale <= 0.0 || time_scale > 1000.0)
        return std::string("Lottie time-remap frame rate or timeline mapping is invalid");

    std::vector<float> static_value;
    bool animated = false;
    if (!lottie_read_transform_property(authored, "tm", static_value, animated))
        return std::string("Lottie precomp time-remap property is malformed");
    if (!animated) {
        if (static_value.size() != 1 || static_value.front() < 0.0f ||
            static_value.front() > 1000000.0f)
            return std::string("Lottie static time-remap value must be in [0,1000000] seconds");
        const double source_frame = static_cast<double>(static_value.front()) * fps;
        if (!std::isfinite(source_frame) || source_frame > 1000000.0 ||
            std::floor(source_frame) != source_frame)
            return std::string("Lottie static time-remap value must map to a bounded integral source frame");
        remap.freeze_frame = Frame{static_cast<std::int64_t>(source_frame)};
        return std::nullopt;
    }

    AnimationTrackPlan track;
    if (!lottie_decode_transform_track(authored, "tm", "time_remap", 1.0f, track) ||
        track.keyframes.empty())
        return std::string("Lottie animated time-remap requires bounded scalar keyframes");
    for (auto& key : track.keyframes) {
        if (key.value.size() != 1 || key.value.front() < 0.0f ||
            key.value.front() > 1000000.0f)
            return std::string("Lottie animated time-remap values must be in [0,1000000] seconds");
        const double parent_frame = composition_offset +
            static_cast<double>(key.frame.integral()) * time_scale;
        const double local_frame = parent_frame -
            static_cast<double>(container_start.integral());
        const double source_frame = static_cast<double>(key.value.front()) * fps;
        if (!std::isfinite(local_frame) || !std::isfinite(source_frame) ||
            std::floor(local_frame) != local_frame || std::floor(source_frame) != source_frame ||
            local_frame < -1000000.0 || local_frame > 1000000.0 || source_frame < 0.0 ||
            source_frame > 1000000.0)
            return std::string("Lottie time-remap keyframes must map to bounded integral local/source frames");
        key.frame = Frame{static_cast<std::int64_t>(local_frame)};
        key.value.front() = static_cast<float>(source_frame);
    }
    track.property = "time_remap";
    remap.animation = std::move(track);
    return std::nullopt;
}










} // namespace chronon3d::render_plan::detail::lottie_expansion_support
