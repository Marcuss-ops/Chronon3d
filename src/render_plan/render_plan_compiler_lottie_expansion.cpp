#include "render_plan_compiler_detail.hpp"
#include "render_plan_compiler_lottie_detail.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace chronon3d::render_plan::detail {
using json = nlohmann::json;

void shift_lottie_track(AnimationTrackPlan& track, std::int64_t offset) {
    for (auto& key : track.keyframes) {
        const auto frame = key.frame.integral() + offset;
        if (frame < 0 || frame > 1000000)
            throw std::runtime_error("nested Lottie keyframe time exceeds [0,1000000]");
        key.frame = Frame{frame};
    }
}

void shift_lottie_tracks(LayerPlan& layer, std::int64_t offset) {
    const auto shift = [offset](std::optional<AnimationTrackPlan>& value) {
        if (value) shift_lottie_track(*value, offset);
    };
    if (layer.animation)
        for (auto& track : layer.animation->tracks) shift_lottie_track(track, offset);
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
                    const auto frame = key.frame.integral() + offset;
                    if (frame < 0 || frame > 1000000)
                        throw std::runtime_error("nested Lottie gradient time exceeds [0,1000000]");
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

std::optional<std::string> lottie_layer_window(
    const json& composition, const json& layer,
    std::int64_t composition_offset, std::int64_t clip_start,
    std::int64_t clip_end, Frame& start, Frame& duration) {
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
    const auto first = composition_offset + static_cast<std::int64_t>(layer_in);
    const auto last = composition_offset + static_cast<std::int64_t>(layer_out);
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
        const auto doc = json::parse(input);
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
        std::function<std::optional<std::string>(const json&, std::int64_t,
            std::int64_t, std::int64_t, std::optional<std::string>,
            std::size_t, bool)> expand_composition;
        expand_composition = [&](const json& composition,
            std::int64_t composition_offset, std::int64_t clip_start,
            std::int64_t clip_end, std::optional<std::string> inherited_parent,
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
                        composition_offset, clip_start, clip_end, start, duration))
                    return *error;
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
                    shift_lottie_tracks(layer, composition_offset);
                    if (result.size() >= 256)
                        return std::string("Lottie expansion exceeds 256 scene layers");
                    result.push_back(std::move(layer));
                    continue;
                }
                if (type != 0)
                    return std::string("Lottie currently imports 2D shape layers and precompositions; other layer types are unsupported");
                if (authored.contains("tm") || authored.contains("ef") ||
                    authored.contains("tt") || authored.contains("exp"))
                    return std::string("Lottie precomp time remap, effects, mattes, and expressions are unsupported");
                if (blend_value != 0)
                    return std::string("Lottie precomp blend modes require isolated group compositing");
                const double stretch = authored.value("sr", 1.0);
                const double start_time = authored.value("st", 0.0);
                if (!std::isfinite(stretch) || stretch != 1.0 ||
                    !std::isfinite(start_time) || std::floor(start_time) != start_time ||
                    std::abs(start_time) > 1000000.0)
                    return std::string("Lottie precomps currently require integer startTime and timeStretch=1");
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
                if (carrier.opacity.value_or(1.0f) != 1.0f ||
                    (carrier.animation && std::any_of(carrier.animation->tracks.begin(),
                        carrier.animation->tracks.end(), [](const auto& track) {
                            return track.property == "opacity";
                        })))
                    return std::string("Lottie precomp group opacity requires isolated group compositing");
                bool position_animated{}, anchor_animated{}, scale_animated{}, rotation_animated{}, opacity_animated{};
                std::vector<float> ignored;
                const auto& transform = authored["ks"];
                if (!lottie_read_transform_property(transform, "p", ignored, position_animated) ||
                    !lottie_read_transform_property(transform, "a", ignored, anchor_animated) ||
                    !lottie_read_transform_property(transform, "s", ignored, scale_animated) ||
                    !lottie_read_transform_property(transform, "r", ignored, rotation_animated) ||
                    !lottie_read_transform_property(transform, "o", ignored, opacity_animated))
                    return std::string("Lottie precomp transform properties are malformed");
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
                shift_lottie_tracks(carrier, composition_offset);
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
                nested["ip"] = 0;
                nested["op"] = doc.value("op", 1000000.0);
                const auto nested_offset = composition_offset +
                    static_cast<std::int64_t>(start_time);
                const auto nested_clip_start = start.integral();
                const auto nested_clip_end = start.integral() + duration.integral();
                const auto child_error = expand_composition(nested, nested_offset,
                    nested_clip_start, nested_clip_end, ids.at(indices[i]), depth + 1, false);
                active_assets.erase(ref);
                if (child_error) return *child_error;
            }
            return std::nullopt;
        };
        const auto root_ip = doc.value("ip", 0.0);
        const std::int64_t root_offset = base_start - static_cast<std::int64_t>(root_ip);
        if (auto error = expand_composition(doc, root_offset, base_start, base_end,
                source.parent, 0, true)) return *error;
        if (result.empty()) return std::string("Lottie contains no visible supported shape layers");
        expanded_layers = std::move(result);
        return std::nullopt;
    } catch (const std::exception& error) {
        return std::string("malformed Lottie JSON: ") + error.what();
    }
}


}  // namespace chronon3d::render_plan::detail
