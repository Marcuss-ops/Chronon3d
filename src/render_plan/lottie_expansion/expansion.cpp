#include "render_plan_compiler_detail.hpp"
#include "render_plan_compiler_lottie_detail.hpp"
#include "lottie_expansion.hpp"
#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/render_plan/easing_contract.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

namespace chronon3d::render_plan::detail {

using json = nlohmann::json;

namespace {

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









}  // namespace

bool map_lottie_timeline_frame(double input, double offset, double scale,
                               std::int64_t& output) {
    if (!std::isfinite(input) || !std::isfinite(offset) || !std::isfinite(scale) ||
        scale <= 0.0)
        return false;
    const double mapped = offset + input * scale;
    if (!std::isfinite(mapped) || mapped < 0.0 || mapped > 1000000.0)
        return false;
    const double rounded = std::round(mapped);
    if (std::abs(mapped - rounded) > 1e-8) return false;
    output = static_cast<std::int64_t>(rounded);
    return true;
}

void shift_lottie_track(AnimationTrackPlan& track, double offset, double scale) {
    for (auto& key : track.keyframes) {
        std::int64_t frame{};
        if (!map_lottie_timeline_frame(static_cast<double>(key.frame.integral()),
                                       offset, scale, frame))
            throw std::runtime_error("time-stretched Lottie keyframe is outside [0,1000000] or maps to a fractional frame");
        key.frame = Frame{frame};
    }
}

void shift_lottie_tracks(LayerPlan& layer, double offset, double scale) {
    const auto shift = [offset, scale](std::optional<AnimationTrackPlan>& value) {
        if (value) shift_lottie_track(*value, offset, scale);
    };
    if (layer.animation)
        for (auto& track : layer.animation->tracks) shift_lottie_track(track, offset, scale);
    for (auto& animator : layer.text_animators) {
        for (auto& track : animator.properties) shift_lottie_track(track, offset, scale);
        for (auto& selector : animator.selectors) {
            shift_lottie_track(selector.start, offset, scale);
            shift_lottie_track(selector.end, offset, scale);
            shift_lottie_track(selector.offset, offset, scale);
            shift_lottie_track(selector.amount, offset, scale);
        }
    }
    for (auto& track : layer.effect_param_tracks)
        shift_lottie_track(track.track, offset, scale);
    if (layer.time_remap) shift(layer.time_remap->animation);
    if (layer.shape) {
        for (auto& operation : layer.shape->operators) {
            shift(operation.morph_animation);
            shift(operation.repeater_animation);
            shift(operation.trim_animation);
        }
        for (auto& subpath : layer.shape->imported_subpaths) {
            shift(subpath.fill_color_animation);
            shift(subpath.fill_opacity_animation);
            shift(subpath.stroke_color_animation);
            shift(subpath.stroke_opacity_animation);
            shift(subpath.stroke_width_animation);
            shift(subpath.svg_opacity_animation);
            shift(subpath.svg_translation_animation);
            shift(subpath.svg_scale_animation);
            shift(subpath.svg_rotation_animation);
            shift(subpath.morph_animation);
            if (subpath.fill_gradient_animation)
                for (auto& key : subpath.fill_gradient_animation->keyframes) {
                    std::int64_t frame{};
                    if (!map_lottie_timeline_frame(
                            static_cast<double>(key.frame.integral()), offset, scale, frame))
                        throw std::runtime_error("time-stretched Lottie gradient keyframe is outside [0,1000000] or maps to a fractional frame");
                    key.frame = Frame{frame};
                }
            if (subpath.lottie_group_transform) {
                auto& transform = *subpath.lottie_group_transform;
                shift(transform.position_animation);
                shift(transform.scale_animation);
                shift(transform.rotation_animation);
                shift(transform.opacity_animation);
            }
        }
    }
    const auto shift_mask = [&](LayerPlan::PathMaskPlan& mask) {
        shift(mask.feather_track);
        shift(mask.expansion_track);
        shift(mask.opacity_track);
        shift(mask.morph_animation);
    };
    if (layer.path_mask) shift_mask(*layer.path_mask);
    for (auto& mask : layer.masks) shift_mask(mask);
}

std::optional<BlendMode> lottie_blend_mode(int mode) {
    switch (mode) {
        case 0: return BlendMode::Normal;
        case 1: return BlendMode::Multiply;
        case 2: return BlendMode::Screen;
        case 3: return BlendMode::Overlay;
        case 4: return BlendMode::Darken;
        case 5: return BlendMode::Lighten;
        case 6: return BlendMode::ColorDodge;
        case 7: return BlendMode::ColorBurn;
        case 8: return BlendMode::HardLight;
        case 9: return BlendMode::SoftLight;
        case 10: return BlendMode::Difference;
        case 11: return BlendMode::Exclusion;
        case 12: return BlendMode::Hue;
        case 13: return BlendMode::Saturation;
        case 14: return BlendMode::Color;
        case 15: return BlendMode::Luminosity;
        case 16: return BlendMode::Add;
        default: return std::nullopt;
    }
}

std::uint32_t lottie_group_id(const std::string& layer_id) {
    std::uint32_t hash = 2166136261u;
    for (const unsigned char byte : layer_id) {
        hash ^= byte;
        hash *= 16777619u;
    }
    return hash == 0 ? 1u : hash;
}

std::optional<std::string> apply_lottie_isolated_opacity(
    LayerPlan& layer, float opacity,
    std::optional<AnimationTrackPlan> animation = std::nullopt) {
    if (!std::isfinite(opacity) || opacity < 0.0f || opacity > 1.0f)
        return std::string("Lottie layer opacity must be finite and in [0,1]");
    if (opacity >= 1.0f && !animation) {
        layer.opacity = 1.0f;
        return std::nullopt;
    }
    if (layer.type != LayerType::Shape || !layer.shape ||
        layer.shape->imported_subpaths.empty())
        return std::string("Lottie isolated layer opacity requires imported shape paths");
    if (animation) {
        if (animation->property != "opacity" || animation->keyframes.size() != 2)
            return std::string("Lottie isolated layer opacity supports exactly two keyframes");
        for (const auto& key : animation->keyframes) {
            if (key.value.size() != 1 || !std::isfinite(key.value[0]) ||
                key.value[0] < 0.0f || key.value[0] > 1.0f ||
                (key.interpolation &&
                 *key.interpolation != interpolation_name::kLinear &&
                 *key.interpolation != interpolation_name::kHold))
                return std::string("Lottie isolated opacity keyframes must be bounded linear/hold scalars");
        }
        if (animation->keyframes[1].frame <= animation->keyframes[0].frame)
            return std::string("Lottie isolated opacity keyframe times must increase");
    }

    const std::uint32_t id = lottie_group_id(layer.id);
    for (auto& path : layer.shape->imported_subpaths) {
        if (path.svg_groups.empty() && path.svg_group)
            path.svg_groups.push_back(*path.svg_group);
        ImportedSubpathPlan::SvgGroupIsolationPlan group{id, opacity};
        group.opacity_animation = animation;
        path.svg_groups.insert(path.svg_groups.begin(), std::move(group));
        path.svg_group = path.svg_groups.back();
    }
    layer.opacity = 1.0f;
    if (layer.animation) {
        auto& tracks = layer.animation->tracks;
        tracks.erase(std::remove_if(tracks.begin(), tracks.end(),
            [](const auto& track) { return track.property == "opacity"; }), tracks.end());
        if (tracks.empty()) layer.animation.reset();
    }
    return std::nullopt;
}

std::optional<std::string> lottie_layer_window(
    const json& composition, const json& layer,
    double composition_offset, double time_scale,
    std::int64_t clip_start, std::int64_t clip_end,
    Frame& start, Frame& duration) {
    const double comp_in = composition.value("ip", 0.0);
    const double comp_out = composition.value("op", comp_in + 1.0);
    const double layer_in = layer.value("ip", comp_in);
    const double layer_out = layer.value("op", comp_out);
    if (!std::isfinite(comp_in) || !std::isfinite(comp_out) ||
        !std::isfinite(layer_in) || !std::isfinite(layer_out) ||
        std::floor(comp_in) != comp_in || std::floor(comp_out) != comp_out ||
        std::floor(layer_in) != layer_in || std::floor(layer_out) != layer_out ||
        comp_in < 0.0 || layer_in < comp_in || layer_out > comp_out ||
        !(layer_out > layer_in) || comp_out > 1000000.0)
        return std::string("Lottie composition and layer in/out points must be integral, ordered, and bounded to the composition");
    if (!std::isfinite(composition_offset) || !std::isfinite(time_scale) ||
        time_scale <= 0.0 || time_scale > 1000.0)
        return std::string("Lottie precomp time scale is outside the supported positive range");
    const double mapped_first = composition_offset + layer_in * time_scale;
    const double mapped_last = composition_offset + layer_out * time_scale;
    if (!std::isfinite(mapped_first) || !std::isfinite(mapped_last) ||
        mapped_first < 0.0 || mapped_last > 1000000.0)
        return std::string("time-stretched Lottie layer window is outside [0,1000000]");
    const auto first = static_cast<std::int64_t>(std::ceil(mapped_first - 1e-9));
    const auto last = static_cast<std::int64_t>(std::ceil(mapped_last - 1e-9));
    const auto visible_first = std::max(first, clip_start);
    const auto visible_last = std::min(last, clip_end);
    if (visible_last <= visible_first)
        return std::string("Lottie layer has no visible frames after nested composition clipping");
    start = Frame{visible_first};
    duration = Frame{visible_last - visible_first};
    return std::nullopt;
}

std::optional<std::string> expand_static_lottie(
    const chronon3d::render_plan::LayerPlan& source,
    const std::filesystem::path& path,
    std::vector<chronon3d::render_plan::LayerPlan>& expanded_layers) {
    std::error_code ec;
    const auto bytes = std::filesystem::file_size(path, ec);
    if (ec || bytes > 10ULL * 1024ULL * 1024ULL)
        return std::string("Lottie source is missing or exceeds the 10 MiB input limit");
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::string("Lottie source could not be opened");
    try {
        auto doc = json::parse(input);
        // Older Bodymovin exports may omit root dimensions. The source layer
        // size is the canonical fallback available at this lowering boundary.
        if (!doc.contains("w") && source.size_dimensions >= 2)
            doc["w"] = source.size[0];
        if (!doc.contains("h") && source.size_dimensions >= 2)
            doc["h"] = source.size[1];
        if (!doc.is_object() || doc.value("v", std::string{}).empty() ||
            !doc.contains("layers") || !doc["layers"].is_array() ||
            doc["layers"].empty() || doc["layers"].size() > 256 ||
            (doc.contains("assets") && !doc["assets"].is_array()))
            return std::string("Lottie requires a version, 1..256 layers, and an asset array");
        std::vector<LayerPlan> result;
        result.reserve(64);
        std::unordered_set<std::string> active_assets;
        std::size_t generated_count = 0;
        const std::int64_t base_start = source.start_frame
            ? source.start_frame->integral() : 0;
        const std::int64_t base_end = source.duration_frames
            ? base_start + source.duration_frames->integral() : 1000000;
        const auto make_id = [&]() {
            if (++generated_count > 256)
                throw std::runtime_error("Lottie expansion exceeds 256 scene layers");
            return source.id + "__lottie_" + std::to_string(generated_count - 1);
        };
        std::function<std::optional<std::string>(const json&, double, double,
            std::int64_t, std::int64_t, std::optional<std::string>,
            std::optional<chronon3d::render_plan::TimeRemapPlan>,
            std::size_t, bool)> expand_composition;
        expand_composition = [&](const json& composition,
            double composition_offset, double time_scale, std::int64_t clip_start,
            std::int64_t clip_end, std::optional<std::string> inherited_parent,
            std::optional<chronon3d::render_plan::TimeRemapPlan> inherited_time_remap,
            std::size_t depth, bool root) -> std::optional<std::string> {
            if (depth > 8 || !composition.is_object() ||
                !composition.contains("layers") || !composition["layers"].is_array() ||
                composition["layers"].size() > 256)
                return std::string("Lottie precomposition nesting/layer count exceeds its bounded contract");
            const auto& authored_layers = composition["layers"];
            std::unordered_map<std::int64_t, std::string> ids;
            std::vector<std::int64_t> indices(authored_layers.size());
            for (std::size_t i = 0; i < authored_layers.size(); ++i) {
                const auto& authored = authored_layers[i];
                if (!authored.is_object()) return std::string("Lottie layers must be objects");
                std::int64_t index = static_cast<std::int64_t>(i + 1);
                if (authored.contains("ind")) {
                    if (!authored["ind"].is_number_integer())
                        return std::string("Lottie layer ind must be an integer");
                    index = authored["ind"].get<std::int64_t>();
                }
                if (index < 0 || index > 1000000 || ids.contains(index))
                    return std::string("Lottie layer ind values must be unique and within [0,1000000]");
                indices[i] = index;
                std::string id;
                if (root && authored_layers.size() == 1 && authored.value("ty", -1) == 4)
                    id = source.id;
                else
                    id = make_id();
                ids.emplace(index, std::move(id));
            }
            for (std::size_t reverse = authored_layers.size(); reverse > 0; --reverse) {
                const std::size_t i = reverse - 1;
                const auto& authored = authored_layers[i];
                if (authored.value("hd", false)) continue;
                Frame start{}, duration{};
                if (auto error = lottie_layer_window(composition, authored,
                        composition_offset, time_scale, clip_start, clip_end, start, duration))
                    return *error;
                if (inherited_time_remap) {
                    const double comp_in = composition.value("ip", 0.0);
                    const double comp_out = composition.value("op", 0.0);
                    const double layer_in = authored.value("ip", comp_in);
                    const double layer_out = authored.value("op", comp_out);
                    if (layer_in != comp_in || layer_out != comp_out)
                        return std::string("time-remapped precomps currently require child layers visible for the full source composition");
                    start = Frame{clip_start};
                    duration = Frame{clip_end - clip_start};
                }
                std::optional<std::string> parent = inherited_parent;
                if (authored.contains("parent")) {
                    if (!authored["parent"].is_number_integer())
                        return std::string("Lottie parent references must be integer layer indices");
                    const auto found = ids.find(authored["parent"].get<std::int64_t>());
                    if (found == ids.end() || found->second == ids.at(indices[i]))
                        return std::string("Lottie parent reference is missing or self-referential");
                    parent = found->second;
                }
                const int blend_value = authored.value("bm", 0);
                const auto blend = lottie_blend_mode(blend_value);
                if (!blend) return std::string("Lottie blend mode is outside the supported Bodymovin range");
                const auto type = authored.value("ty", -1);
                if (type == 2) {
                    if (authored.contains("tm") || authored.contains("exp"))
                        return std::string("Lottie image layers with local time-remap or expressions are unsupported");
                    LayerPlan layer = source;
                    layer.id = ids.at(indices[i]);
                    layer.type = LayerType::Image;
                    layer.parent = parent;
                    layer.start_frame = start;
                    layer.duration_frames = duration;
                    layer.animation.reset();
                    layer.effects.clear();
                    layer.masks.clear();
                    layer.path_mask.reset();
                    layer.blend_mode = blend;
                    std::array<float, 2> dimensions{};
                    std::shared_ptr<const std::vector<std::byte>> embedded_payload;
                    if (auto error = lottie_image_asset_path(
                            source, doc, authored, layer.asset, dimensions,
                            embedded_payload)) return *error;
                    layer.size = dimensions;
                    layer.size_dimensions = 2;
                    layer.embedded_image_payload = std::move(embedded_payload);
                    layer.shape.reset();
                    float anchor_x = 0.0f, anchor_y = 0.0f;
                    if (auto error = decode_lottie_layer_transform(
                            layer, authored, anchor_x, anchor_y)) return *error;
                    if (auto error = center_lottie_image_transform(
                            layer, composition, anchor_x, anchor_y, dimensions)) return *error;
                    if (auto error = attach_lottie_track_matte(
                            authored, i, ids, indices, authored_layers, layer)) return *error;
                    if (auto error = decode_lottie_layer_effects(authored, layer)) return *error;
                    if (inherited_time_remap) layer.time_remap = inherited_time_remap;
                    else shift_lottie_tracks(layer, composition_offset, time_scale);
                    if (result.size() >= 256)
                        return std::string("Lottie expansion exceeds 256 scene layers");
                    result.push_back(std::move(layer));
                    continue;
                }
                if (type == 5) {
                    if (inherited_time_remap)
                        return std::string("text layers inside time-remapped precomps are unsupported");
                    if (authored.value("ddd", 0) != 0 || authored.contains("tm") ||
                        authored.contains("exp"))
                        return std::string("Lottie text layers with 3D transforms, time remap, or expressions are unsupported");
                    if (!authored.contains("t") || !authored["t"].is_object())
                        return std::string("Lottie text layer has no text document");
                    const auto& text_data = authored["t"];
                    if (text_data.contains("p") &&
                        (!text_data["p"].is_object() || !text_data["p"].empty()))
                        return std::string("Lottie text-on-path is not supported");
                    if (text_data.contains("m")) {
                        if (!text_data["m"].is_object() ||
                            text_data["m"].value("g", 1) != 1 ||
                            !text_data["m"].contains("a"))
                            return std::string("Lottie text grouping/alignment mode is unsupported");
                        std::vector<float> alignment;
                        bool alignment_animated{};
                        if (!lottie_read_transform_property(text_data["m"], "a", alignment,
                                                            alignment_animated) ||
                            alignment_animated || alignment.size() != 2 ||
                            std::abs(alignment[0]) > 1e-6f || std::abs(alignment[1]) > 1e-6f)
                            return std::string("Lottie text animator alignment offsets are unsupported");
                    }
                    if (!text_data.contains("d") || !text_data["d"].is_object() ||
                        !text_data["d"].contains("k") || !text_data["d"]["k"].is_array() ||
                        text_data["d"]["k"].empty() || text_data["d"]["k"].size() > 256)
                        return std::string("Lottie text document keyframes are missing or exceed the 256-keyframe bound");
                    const auto& documents = text_data["d"]["k"];
                    std::vector<std::int64_t> document_times;
                    document_times.reserve(documents.size());
                    bool first_text_segment = true;
                    for (std::size_t doc_index = 0; doc_index < documents.size(); ++doc_index) {
                        const auto& entry = documents[doc_index];
                        if (!entry.is_object() || !entry.contains("t") || !entry["t"].is_number() ||
                            !entry.contains("s") || !entry["s"].is_object())
                            return std::string("Lottie text document keyframe is malformed");
                        const double frame = entry["t"].get<double>();
                        if (!std::isfinite(frame) || std::floor(frame) != frame ||
                            frame < 0.0 || frame > 1000000.0 ||
                            (doc_index > 0 && frame <= static_cast<double>(document_times.back())))
                            return std::string("Lottie text document times must be increasing integral frames in [0,1000000]");
                        if (doc_index + 1 < documents.size() &&
                            (!entry.contains("h") || !entry["h"].is_number_integer() ||
                             entry["h"].get<int>() != 1))
                            return std::string("Lottie text document keyframes must use hold interpolation");
                        std::int64_t mapped_frame{};
                        if (!map_lottie_timeline_frame(frame, composition_offset,
                                time_scale, mapped_frame))
                            return std::string("time-stretched Lottie text keyframe maps outside [0,1000000] or to a fractional frame");
                        document_times.push_back(mapped_frame);
                    }
                    if (documents.size() > 1 && documents[0].value("t", 0.0) > 0.0)
                        return std::string("Lottie animated text must define its initial document at composition frame 0");
                    for (std::size_t doc_index = 0; doc_index < documents.size(); ++doc_index) {
                        const std::int64_t segment_start = std::max(start.integral(),
                            document_times[doc_index]);
                        const std::int64_t segment_end = std::min(
                            start.integral() + duration.integral(),
                            doc_index + 1 < documents.size()
                                ? document_times[doc_index + 1]
                                : start.integral() + duration.integral());
                        if (segment_end <= segment_start) continue;
                        LayerPlan layer = source;
                        layer.id = first_text_segment ? ids.at(indices[i]) : make_id();
                        first_text_segment = false;
                        layer.type = LayerType::Text;
                        layer.parent = parent;
                        layer.start_frame = Frame{segment_start};
                        layer.duration_frames = Frame{segment_end - segment_start};
                        layer.animation.reset();
                        layer.text.clear();
                        layer.text_spans.clear();
                        layer.style.reset();
                        layer.font.clear();
                        layer.shape.reset();
                        layer.effects.clear();
                        layer.masks.clear();
                        layer.path_mask.reset();
                        layer.blend_mode = blend;
                        float anchor_x = 0.0f, anchor_y = 0.0f;
                        if (auto error = decode_lottie_layer_transform(
                                layer, authored, anchor_x, anchor_y)) return *error;
                        // Chronon's text frame spans the full canvas. Its left/right
                        // alignment is therefore relative to the frame edge, while
                        // Bodymovin positions the corresponding text edge at the
                        // layer position. Move the frame origin so those anchors
                        // agree; centered text already shares the same anchor.
                        const int text_align = documents[doc_index]["s"].value("j", 0);
                        const float text_frame_origin = text_align == 0
                            ? static_cast<float>(composition.value("w", 0.0)) * 0.5f
                            : text_align == 1
                                ? -static_cast<float>(composition.value("w", 0.0)) * 0.5f
                                : 0.0f;
                        if (std::abs(text_frame_origin) > 1e-6f) {
                            if (layer.position_dimensions >= 2)
                                layer.position[0] += text_frame_origin;
                            if (layer.animation) {
                                for (auto& track : layer.animation->tracks) {
                                    if (track.property != "position") continue;
                                    for (auto& key : track.keyframes) {
                                        if (key.value.size() < 2)
                                            return std::string("Lottie text position keyframes require at least two components");
                                        key.value[0] += text_frame_origin;
                                    }
                                }
                            }
                        }
                        if (auto error = decode_lottie_text_document(
                                source, doc, documents[doc_index]["s"], layer)) return *error;
                        // Chronon's full-canvas text frame is vertically centered;
                        // Bodymovin's layer position is a typographic baseline.
                        // Convert the centered frame origin to the baseline used by
                        // the Lottie text renderer (the bounded text contract uses
                        // a baseline offset of 0.3457 em from the centered frame).
                        float baseline_offset = layer.style
                            ? layer.style->font_size.value_or(0.0f) * 0.3457f : 0.0f;
                        // Lottie positions multiline text from the first line's
                        // baseline. Chronon's full-canvas text layout centers the
                        // complete block, moving that baseline up by half of the
                        // default 1.2em line step for each additional line. Undo
                        // that shift when converting the Bodymovin baseline to
                        // Chronon's centered text origin.
                        if (documents[doc_index]["s"].contains("t") &&
                            documents[doc_index]["s"]["t"].is_string() && layer.style) {
                            const auto& content = documents[doc_index]["s"]["t"].get_ref<const std::string&>();
                            const auto line_breaks = static_cast<float>(std::count_if(
                                content.begin(), content.end(), [](char value) {
                                    return value == '\r' || value == '\n';
                                }));
                            baseline_offset -= line_breaks *
                                layer.style->font_size.value_or(0.0f) * 0.60f;
                        }
                        if (std::abs(baseline_offset) > 1e-6f &&
                            std::isfinite(baseline_offset)) {
                            if (layer.position_dimensions >= 2)
                                layer.position[1] -= baseline_offset;
                            if (layer.animation) {
                                for (auto& track : layer.animation->tracks) {
                                    if (track.property != "position") continue;
                                    for (auto& key : track.keyframes) {
                                        if (key.value.size() < 2)
                                            return std::string("Lottie text position keyframes require at least two components");
                                        key.value[1] -= baseline_offset;
                                    }
                                }
                            }
                        }
                        if (!root) {
                            if (auto error = preserve_nested_lottie_local_position(layer, doc))
                                return *error;
                        } else if (layer.animation && std::any_of(
                            layer.animation->tracks.begin(), layer.animation->tracks.end(),
                            [](const AnimationTrackPlan& track) {
                                return track.property == "position";
                            })) {
                            if (auto error = center_lottie_layer_position(layer, composition))
                                return *error;
                        }
                        if (auto error = decode_lottie_text_animators(text_data, layer)) return *error;
                        if (auto error = attach_lottie_track_matte(
                                authored, i, ids, indices, authored_layers, layer)) return *error;
                        if (auto error = decode_lottie_layer_effects(authored, layer)) return *error;
                        shift_lottie_tracks(layer, composition_offset, time_scale);
                        if (result.size() >= 256)
                            return std::string("Lottie expansion exceeds 256 scene layers");
                        result.push_back(std::move(layer));
                    }
                    continue;
                }
                if (type == 4) {
                    LayerPlan layer = source;
                    layer.id = ids.at(indices[i]);
                    layer.type = LayerType::Shape;
                    layer.parent = parent;
                    layer.start_frame = start;
                    layer.duration_frames = duration;
                    layer.animation.reset();
                    layer.effects.clear();
                    layer.masks.clear();
                    layer.path_mask.reset();
                    layer.blend_mode = blend;
                    if (auto error = attach_lottie_track_matte(
                            authored, i, ids, indices, authored_layers, layer)) return *error;
                    if (auto error = decode_lottie_layer_effects(authored, layer)) return *error;
                    if (!layer.shape) return std::string("Lottie source layer has no shape payload");
                    layer.shape->type = ShapeContentTypePlan::Path;
                    layer.shape->path.clear();
                    layer.shape->imported_subpaths.clear();
                    // Keep RenderPlan-level path operators authored alongside
                    // the imported asset (for example a repeater). The asset
                    // reference itself is restored on one expanded layer by
                    // expand_lottie_assets so it remains in the asset manifest.
                    layer.shape->svg_asset.reset();
                    if (auto error = expand_static_lottie_layer(layer, composition, i))
                        return *error;
                    const bool animated_position = layer.animation && std::any_of(
                        layer.animation->tracks.begin(), layer.animation->tracks.end(),
                        [](const AnimationTrackPlan& track) { return track.property == "position"; });
                    if (!root) {
                        if (auto error = preserve_nested_lottie_local_position(layer, doc))
                            return *error;
                    } else if (animated_position) {
                        if (auto error = center_lottie_layer_position(layer, composition)) return *error;
                    }
                    if (inherited_time_remap) layer.time_remap = inherited_time_remap;
                    else shift_lottie_tracks(layer, composition_offset, time_scale);
                    if (result.size() >= 256)
                        return std::string("Lottie expansion exceeds 256 scene layers");
                    result.push_back(std::move(layer));
                    continue;
                }
                if (type != 0)
                    return std::string("Lottie currently imports 2D shape layers and precompositions; other layer types are unsupported");
                if (inherited_time_remap || authored.contains("ef") ||
                    authored.contains("tt") || authored.contains("exp"))
                    return std::string("nested time-remapped precomps, effects, mattes, and expressions are unsupported");
                const bool precomp_group_blend = blend_value != 0;
                const double stretch = authored.value("sr", 1.0);
                const double start_time = authored.value("st", 0.0);
                if (!std::isfinite(stretch) || stretch <= 0.0 || stretch > 1000.0 ||
                    !std::isfinite(start_time) || std::floor(start_time) != start_time ||
                    std::abs(start_time) > 1000000.0)
                    return std::string("Lottie precomp startTime must be integral and timeStretch must be in (0,1000]");
                std::optional<chronon3d::render_plan::TimeRemapPlan> child_time_remap;
                if (authored.contains("tm")) {
                    if (std::abs(stretch - 1.0) > 1e-9 || std::abs(start_time) > 1e-9)
                        return std::string("Lottie precomp time-remap currently requires unit timeStretch and zero startTime");
                    chronon3d::render_plan::TimeRemapPlan remap;
                    if (auto error = decode_lottie_precomp_time_remap(
                            authored, composition, composition_offset, time_scale,
                            start, remap)) return *error;
                    child_time_remap = std::move(remap);
                }
                const double nested_scale = time_scale * stretch;
                if (!std::isfinite(nested_scale) || nested_scale <= 0.0 || nested_scale > 1000.0)
                    return std::string("nested Lottie precomp time scale exceeds its bounded positive range");
                LayerPlan carrier = source;
                carrier.id = ids.at(indices[i]);
                carrier.type = LayerType::Color;
                carrier.shape.reset();
                carrier.color = {0.0f, 0.0f, 0.0f, 0.0f};
                carrier.parent = parent;
                carrier.start_frame = start;
                carrier.duration_frames = duration;
                carrier.animation.reset();
                carrier.effects.clear();
                carrier.masks.clear();
                carrier.path_mask.reset();
                carrier.blend_mode = BlendMode::Normal;
                if (!authored.contains("ks") || !authored["ks"].is_object())
                    return std::string("Lottie precomp transform is missing");
                float anchor_x = 0.0f, anchor_y = 0.0f;
                if (auto error = decode_lottie_layer_transform(
                        carrier, authored, anchor_x, anchor_y)) return *error;
                bool position_animated{}, anchor_animated{}, scale_animated{}, rotation_animated{}, opacity_animated{};
                std::vector<float> ignored;
                const auto& transform = authored["ks"];
                if (!lottie_read_transform_property(transform, "p", ignored, position_animated) ||
                    !lottie_read_transform_property(transform, "a", ignored, anchor_animated) ||
                    !lottie_read_transform_property(transform, "s", ignored, scale_animated) ||
                    !lottie_read_transform_property(transform, "r", ignored, rotation_animated) ||
                    !lottie_read_transform_property(transform, "o", ignored, opacity_animated))
                    return std::string("Lottie precomp transform properties are malformed");
                const bool precomp_group_opacity =
                    std::abs(carrier.opacity.value_or(1.0f) - 1.0f) > 1e-6f || opacity_animated;
                float precomp_opacity = carrier.opacity.value_or(1.0f);
                std::optional<AnimationTrackPlan> precomp_opacity_animation;
                if (opacity_animated && carrier.animation) {
                    const auto opacity_track = std::find_if(
                        carrier.animation->tracks.begin(), carrier.animation->tracks.end(),
                        [](const auto& track) { return track.property == "opacity"; });
                    if (opacity_track == carrier.animation->tracks.end())
                        return std::string("Lottie precomp opacity animation could not be decoded");
                    LayerPlan opacity_track_holder;
                    opacity_track_holder.animation.emplace();
                    opacity_track_holder.animation->tracks.push_back(*opacity_track);
                    shift_lottie_tracks(opacity_track_holder, composition_offset, time_scale);
                    precomp_opacity_animation =
                        std::move(opacity_track_holder.animation->tracks.front());
                    if (precomp_opacity_animation->keyframes.empty() ||
                        precomp_opacity_animation->keyframes.front().value.empty())
                        return std::string("Lottie precomp opacity animation has no initial value");
                    precomp_opacity =
                        precomp_opacity_animation->keyframes.front().value.front();
                }
                if (!std::isfinite(precomp_opacity) || precomp_opacity < 0.0f ||
                    precomp_opacity > 1.0f)
                    return std::string("Lottie precomp opacity must be in [0,1]");
                if (precomp_group_opacity) {
                    carrier.opacity = 1.0f;
                    // Opacity belongs to the isolated precomp surface. Leaving
                    // this track on the transparent parent carrier would let
                    // hierarchy resolution multiply it into every child.
                    if (carrier.animation) {
                        auto& tracks = carrier.animation->tracks;
                        tracks.erase(std::remove_if(tracks.begin(), tracks.end(),
                            [](const auto& track) { return track.property == "opacity"; }),
                            tracks.end());
                        if (tracks.empty()) carrier.animation.reset();
                    }
                }
                if ((scale_animated || rotation_animated) &&
                    (std::abs(anchor_x) > 1e-6f || std::abs(anchor_y) > 1e-6f))
                    return std::string("animated precomp scale/rotation with non-zero anchor is unsupported");
                const float angle = carrier.rotation[2] * 0.01745329251994329577f;
                const float sx = carrier.scale[0], sy = carrier.scale[1];
                const float dx = std::cos(angle) * sx * anchor_x -
                    std::sin(angle) * sy * anchor_y;
                const float dy = std::sin(angle) * sx * anchor_x +
                    std::cos(angle) * sy * anchor_y;
                if (position_animated) {
                    if (carrier.animation)
                        for (auto& track : carrier.animation->tracks)
                            if (track.property == "position")
                                for (auto& key : track.keyframes) {
                                    key.value[0] -= dx;
                                    key.value[1] -= dy;
                                }
                } else {
                    carrier.position[0] -= dx;
                    carrier.position[1] -= dy;
                }
                // A root precomp without an authored parent is an absolute
                // canvas transform and Color layers do not receive the
                // Shape/Text center conversion in scene lowering. A root
                // precomp with a parent is already a local transform in that
                // parent's coordinate basis, so keep its authored position.
                if (root && !parent) {
                    const float canvas_width = composition.value("w", 0.0f);
                    const float canvas_height = composition.value("h", 0.0f);
                    if (!std::isfinite(canvas_width) || !std::isfinite(canvas_height) ||
                        canvas_width <= 0.0f || canvas_height <= 0.0f)
                        return std::string("Lottie root composition dimensions are invalid");
                    if (position_animated) {
                        if (carrier.animation)
                            for (auto& track : carrier.animation->tracks)
                                if (track.property == "position")
                                    for (auto& key : track.keyframes) {
                                        if (key.value.size() < 2)
                                            return std::string("Lottie precomp position keyframes require two components");
                                        key.value[0] -= canvas_width * 0.5f;
                                        key.value[1] -= canvas_height * 0.5f;
                                    }
                    } else {
                        carrier.position[0] -= canvas_width * 0.5f;
                        carrier.position[1] -= canvas_height * 0.5f;
                    }
                }
                shift_lottie_tracks(carrier, composition_offset, time_scale);
                if (result.size() >= 256)
                    return std::string("Lottie expansion exceeds 256 scene layers");
                result.push_back(std::move(carrier));
                if (!authored.contains("refId") || !authored["refId"].is_string() ||
                    !doc.contains("assets") || !doc["assets"].is_array())
                    return std::string("Lottie precomp has no matching asset definition");
                const auto ref = authored["refId"].get<std::string>();
                const auto asset = std::find_if(doc["assets"].begin(), doc["assets"].end(),
                    [&](const auto& item) {
                        return item.is_object() && item.value("id", std::string{}) == ref;
                    });
                if (asset == doc["assets"].end() || !asset->contains("layers") ||
                    !(*asset)["layers"].is_array())
                    return std::string("Lottie precomp refId could not be resolved to a layer asset");
                if (!active_assets.insert(ref).second)
                    return std::string("Lottie precomp cycle detected");
                json nested = doc;
                nested["layers"] = (*asset)["layers"];
                nested["w"] = asset->value("w", composition.value("w", 0));
                nested["h"] = asset->value("h", composition.value("h", 0));
                nested["ip"] = 0;
                nested["op"] = doc.value("op", 1000000.0);
                const auto nested_offset = composition_offset + start_time * time_scale;
                const auto nested_clip_start = start.integral();
                const auto nested_clip_end = start.integral() + duration.integral();
                const auto child_begin = result.size();
                const auto child_error = expand_composition(nested, nested_offset,
                    nested_scale, nested_clip_start, nested_clip_end,
                    ids.at(indices[i]), std::move(child_time_remap), depth + 1, false);
                active_assets.erase(ref);
                if (child_error) return *child_error;
                if ((precomp_group_opacity || precomp_group_blend) &&
                    result.size() > child_begin) {
                    LayerPlan::LottiePrecompGroupPlan group;
                    group.id = ids.at(indices[i]) + "__isolation";
                    group.opacity = precomp_opacity;
                    group.opacity_active = precomp_group_opacity;
                    group.opacity_animation = std::move(precomp_opacity_animation);
                    group.blend_mode = *blend;
                    for (std::size_t child_index = child_begin;
                         child_index < result.size(); ++child_index) {
                        result[child_index].lottie_precomp_groups.insert(
                            result[child_index].lottie_precomp_groups.begin(), group);
                    }
                }
            }
            return std::nullopt;
        };
        const auto root_ip = doc.value("ip", 0.0);
        const double root_offset = static_cast<double>(base_start) - root_ip;
        if (auto error = expand_composition(doc, root_offset, 1.0, base_start, base_end,
                source.parent, std::nullopt, 0, true)) return *error;
        if (result.empty()) return std::string("Lottie contains no visible supported shape layers");
        for (auto& layer : result) {
            if (layer.type != LayerType::Shape || !layer.shape ||
                layer.shape->imported_subpaths.empty()) continue;
            std::optional<AnimationTrackPlan> opacity_animation;
            if (layer.animation) {
                const auto found = std::find_if(layer.animation->tracks.begin(),
                    layer.animation->tracks.end(), [](const auto& track) {
                        return track.property == "opacity";
                    });
                if (found != layer.animation->tracks.end()) opacity_animation = *found;
            }
            const float opacity = layer.opacity.value_or(1.0f);
            if (opacity < 1.0f || opacity_animation) {
                if (auto error = apply_lottie_isolated_opacity(
                        layer, opacity, std::move(opacity_animation))) return *error;
            }
        }
        expanded_layers = std::move(result);
        return std::nullopt;
    } catch (const std::exception& error) {
        return std::string("malformed Lottie JSON: ") + error.what();
    }
}


}  // namespace chronon3d::render_plan::detail
