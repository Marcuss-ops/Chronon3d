#include "render_plan_compiler_detail.hpp"
#include "render_plan_compiler_lottie_detail.hpp"
#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/scene/model/shape/path.hpp>

#if defined(CHRONON3D_ENABLE_SVG)
#include <chronon3d/assets/svg_path_loader.hpp>
#endif

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d::render_plan::detail {
namespace {
using json = nlohmann::json;
}  // namespace

std::optional<std::string> expand_static_lottie(
    chronon3d::render_plan::LayerPlan& layer_plan,
    const std::filesystem::path& path) {
    std::error_code ec;
    const auto bytes = std::filesystem::file_size(path, ec);
    if (ec || bytes > 10ULL * 1024ULL * 1024ULL)
        return std::string("Lottie source is missing or exceeds the 10 MiB input limit");
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::string("Lottie source could not be opened");
    try {
        auto& shape = *layer_plan.shape;
        const auto doc = json::parse(input);
        if (!doc.is_object() || doc.value("v", std::string{}).empty() ||
            !doc.contains("layers") || !doc["layers"].is_array() ||
            doc["layers"].size() != 1)
            return std::string("supported Lottie requires one shape layer and a version field");
        const auto& layer = doc["layers"][0];
        if (layer.value("ty", -1) != 4 || layer.value("ddd", 0) != 0 ||
            layer.contains("ef") ||
            layer.contains("tt") || layer.contains("tm") || layer.contains("exp"))
            return std::string("Lottie layer type, effects, expressions, or time remap are unsupported");
        if (layer.contains("masksProperties") &&
            (!layer["masksProperties"].is_array() || layer["masksProperties"].size() > 16))
            return std::string("Lottie masks must be an array with at most 16 entries");
        float anchor_x = 0.0f, anchor_y = 0.0f;
        if (auto error = decode_lottie_layer_transform(
                layer_plan, layer, anchor_x, anchor_y)) return error;
        if (!layer.contains("shapes") || !layer["shapes"].is_array() ||
            layer["shapes"].size() > 256)
            return std::string("Lottie shape list is missing or exceeds 256 items");
        struct Affine2D { float a{1}, b{0}, c{0}, d{1}, x{0}, y{0}; };
        using GroupTransformPlan = ImportedSubpathPlan::GroupTransformPlan;
        struct GeometryEntry {
            const json* item{};
            Affine2D transform{};
            float opacity{1.0f};
            std::optional<GroupTransformPlan> group_animation;
        };
        const auto compose = [](const Affine2D& parent, const Affine2D& child) {
            return Affine2D{
                parent.a * child.a + parent.c * child.b,
                parent.b * child.a + parent.d * child.b,
                parent.a * child.c + parent.c * child.d,
                parent.b * child.c + parent.d * child.d,
                parent.a * child.x + parent.c * child.y + parent.x,
                parent.b * child.x + parent.d * child.y + parent.y};
        };
        const auto group_transform = [&](const json& item, Affine2D& result,
                                         float& group_opacity,
                                         std::optional<GroupTransformPlan>& animation) {
            std::vector<float> gp, ga, gs, gr, go;
            bool ap{}, aa{}, as{}, ar{}, ao{};
            if (!lottie_read_transform_property(item, "p", gp, ap) ||
                !lottie_read_transform_property(item, "a", ga, aa) || aa ||
                !lottie_read_transform_property(item, "s", gs, as) ||
                !lottie_read_transform_property(item, "r", gr, ar) ||
                !lottie_read_transform_property(item, "o", go, ao) ||
                (!ap && (gp.size() != 2 && gp.size() != 3)) ||
                ga.size() < 2 || ga.size() > 3 ||
                (!as && (gs.size() != 2 && gs.size() != 3)) ||
                (!ar && gr.size() != 1) || (!ao && go.size() != 1) ||
                (!ao && (go[0] < 0.0f || go[0] > 100.0f))) return false;
            const auto component = [](const std::vector<float>& values, std::size_t index, float fallback) {
                return index < values.size() ? values[index] : fallback;
            };
            const float px = component(gp, 0, 0.0f), py = component(gp, 1, 0.0f);
            const float ax = component(ga, 0, 0.0f), ay = component(ga, 1, 0.0f);
            const float sx = component(gs, 0, 100.0f) / 100.0f;
            const float sy = component(gs, 1, 100.0f) / 100.0f;
            const float rotation = component(gr, 0, 0.0f);
            const float angle = rotation * 0.01745329251994329577f;
            const float cosine = std::cos(angle), sine = std::sin(angle);
            result.a = cosine * sx;
            result.b = sine * sx;
            result.c = -sine * sy;
            result.d = cosine * sy;
            result.x = px - result.a * ax - result.c * ay;
            result.y = py - result.b * ax - result.d * ay;
            group_opacity = component(go, 0, 100.0f) / 100.0f;
            if (ap || as || ar || ao) {
                GroupTransformPlan plan;
                plan.position = {px, py}; plan.anchor = {ax, ay};
                plan.scale = {sx, sy}; plan.rotation_degrees = rotation;
                plan.opacity = group_opacity;
                const auto decode = [&](const char* name, const char* property, float divisor,
                                        std::optional<AnimationTrackPlan>& destination,
                                        std::size_t dimensions) {
                    AnimationTrackPlan track;
                    if (!lottie_decode_transform_track(item, name, property, divisor, track) ||
                        std::any_of(track.keyframes.begin(), track.keyframes.end(),
                            [dimensions](const auto& key) { return key.value.size() != dimensions; }))
                        return false;
                    destination = std::move(track);
                    return true;
                };
                if ((ap && !decode("p", "lottie_group_position", 1.0f,
                                   plan.position_animation, 2)) ||
                    (as && !decode("s", "lottie_group_scale", 100.0f,
                                   plan.scale_animation, 2)) ||
                    (ar && !decode("r", "lottie_group_rotation", 1.0f,
                                   plan.rotation_animation, 1)) ||
                    (ao && !decode("o", "lottie_group_opacity", 100.0f,
                                   plan.opacity_animation, 1))) return false;
                const auto uniform_scale = [](const std::vector<float>& values) {
                    return std::abs(values[0] - values[1]) <= 1e-5f;
                };
                if ((!as && std::abs(sx - sy) > 1e-5f) ||
                    (as && std::any_of(plan.scale_animation->keyframes.begin(),
                        plan.scale_animation->keyframes.end(), [&](const auto& key) {
                            return !uniform_scale(key.value);
                        }))) return false;
                animation = std::move(plan);
            }
            const std::array<float, 6> values{
                result.a, result.b, result.c, result.d, result.x, result.y};
            return std::all_of(values.begin(), values.end(), [](float v) {
                    return std::isfinite(v) && std::abs(v) <= 100000.0f;
                });
        };
        std::vector<GeometryEntry> geometry_shapes;
        std::vector<const json*> fills;
        std::vector<const json*> strokes;
        std::vector<const json*> path_operators;
        std::size_t shape_item_count = 0;
        std::function<bool(const json&, const Affine2D&, float,
                           const std::optional<GroupTransformPlan>&, std::size_t)> collect_shapes;
        collect_shapes = [&](const json& item, const Affine2D& parent_transform,
                             float parent_opacity,
                             const std::optional<GroupTransformPlan>& parent_animation,
                             std::size_t depth) {
            if (!item.is_object() || item.contains("exp") || depth > 16 ||
                ++shape_item_count > 256) return false;
            const auto type = item.value("ty", std::string{});
            if (type == "gr") {
                if (!item.contains("it") || !item["it"].is_array()) return false;
                Affine2D local;
                float local_opacity = 1.0f;
                std::optional<GroupTransformPlan> local_animation;
                bool found_transform = false;
                for (const auto& child : item["it"]) {
                    if (child.is_object() && child.value("ty", std::string{}) == "tr") {
                        if (found_transform || !group_transform(child, local, local_opacity, local_animation)) return false;
                        found_transform = true;
                    }
                }
                if (local_animation && (parent_animation ||
                    std::abs(parent_transform.a - 1.0f) > 1e-6f ||
                    std::abs(parent_transform.d - 1.0f) > 1e-6f ||
                    std::abs(parent_transform.b) > 1e-6f ||
                    std::abs(parent_transform.c) > 1e-6f ||
                    std::abs(parent_transform.x) > 1e-6f ||
                    std::abs(parent_transform.y) > 1e-6f ||
                    std::abs(parent_opacity - 1.0f) > 1e-6f)) return false;
                const auto combined = found_transform && !local_animation
                    ? compose(parent_transform, local) : parent_transform;
                const float combined_opacity = parent_opacity *
                    (local_animation ? 1.0f : local_opacity);
                const auto combined_animation = local_animation ? local_animation : parent_animation;
                for (const auto& child : item["it"]) {
                    if (child.is_object() && child.value("ty", std::string{}) == "tr") continue;
                    if (!collect_shapes(child, combined, combined_opacity,
                                        combined_animation, depth + 1)) return false;
                }
                return true;
            }
            if (type == "sh" || type == "el" || type == "rc") {
                geometry_shapes.push_back({&item, parent_transform, parent_opacity, parent_animation});
                return true;
            }
            if (type == "fl" || type == "gf") { fills.push_back(&item); return true; }
            if (type == "st") { strokes.push_back(&item); return true; }
            if (type == "rp" || type == "rd" || type == "zz" ||
                type == "op" || type == "pb" || type == "tm") {
                path_operators.push_back(&item);
                return true;
            }
            if (type == "tr") return lottie_identity_transform(item);
            return false;
        };
        const Affine2D identity;
        for (const auto& item : layer["shapes"])
            if (!collect_shapes(item, identity, 1.0f, std::nullopt, 0))
                return std::string("Lottie group structure, transform, or shape item is unsupported");
        if (geometry_shapes.empty())
            return std::string("Lottie shape geometry is missing");
        using namespace chronon3d::render_plan;
        for (const auto* raw_operator : path_operators) {
            const auto kind = raw_operator->value("ty", std::string{});
            PathOperatorPlan operation;
            if (kind == "tm") {
                operation.kind = PathOperatorKindPlan::Trim;
                float start_percent = 0.0f;
                float end_percent = 100.0f;
                float offset_degrees = 0.0f;
                const std::array<std::pair<const char*, float*>, 3> fields{{
                    {"s", &start_percent}, {"e", &end_percent}, {"o", &offset_degrees}}};
                std::array<std::optional<AnimationTrackPlan>, 3> animated;
                for (std::size_t field_index = 0; field_index < fields.size(); ++field_index) {
                    const auto [name, value] = fields[field_index];
                    if (!lottie_static_scalar_property(*raw_operator, name, *value)) {
                        AnimationTrackPlan track;
                        const float divisor = field_index == 2 ? 360.0f : 100.0f;
                        if (!lottie_decode_transform_track(*raw_operator, name, "trim", divisor, track))
                            return std::string("Lottie Trim Paths properties must be bounded scalar values or keyframes");
                        for (const auto& key : track.keyframes) {
                            if (key.value.size() != 1 ||
                                (field_index < 2 && (key.value[0] < 0.0f || key.value[0] > 1.0f)) ||
                                (field_index == 2 && std::abs(key.value[0]) > 1000000.0f))
                                return std::string("Lottie Trim Paths keyframes are outside supported bounds");
                        }
                        animated[field_index] = std::move(track);
                    }
                }
                if (start_percent < 0.0f || start_percent > 100.0f ||
                    end_percent < 0.0f || end_percent > 100.0f ||
                    std::abs(offset_degrees) > 360000000.0f)
                    return std::string("Lottie Trim Paths values are outside supported bounds");
                operation.trim_start = start_percent / 100.0f;
                operation.trim_end = end_percent / 100.0f;
                operation.trim_offset = offset_degrees / 360.0f;
                const auto reference = std::find_if(animated.begin(), animated.end(),
                    [](const auto& track) { return track.has_value(); });
                if (reference != animated.end()) {
                    auto combined = **reference;
                    for (auto& key : combined.keyframes) {
                        key.value = {operation.trim_start, operation.trim_end, operation.trim_offset};
                    }
                    for (std::size_t field_index = 0; field_index < animated.size(); ++field_index) {
                        if (!animated[field_index]) continue;
                        const auto& source = animated[field_index]->keyframes;
                        if (source.size() != combined.keyframes.size())
                            return std::string("Lottie Trim Paths tracks must share keyframe timing");
                        for (std::size_t key_index = 0; key_index < source.size(); ++key_index) {
                            auto& target = combined.keyframes[key_index];
                            if (source[key_index].frame.integral() != target.frame.integral() ||
                                source[key_index].interpolation != target.interpolation ||
                                source[key_index].bezier_curve != target.bezier_curve)
                                return std::string("Lottie Trim Paths tracks must share keyframe timing and easing");
                            target.value[field_index] = source[key_index].value.front();
                        }
                    }
                    operation.trim_animation = std::move(combined);
                }
                const auto mode = raw_operator->value("m", 1);
                if (mode != 1)
                    return std::string("Lottie Trim Paths currently supports simultaneous mode only");
            } else if (kind == "rd") {
                if (!lottie_static_scalar_property(*raw_operator, "r", operation.radius) ||
                    operation.radius < 0.0f)
                    return std::string("Lottie round-corners radius must be static and non-negative");
                operation.kind = PathOperatorKindPlan::RoundCorners;
            } else if (kind == "zz") {
                float smooth = 1.0f;
                if (!lottie_static_scalar_property(*raw_operator, "s", operation.size) ||
                    !lottie_static_scalar_property(*raw_operator, "r", operation.points_per_unit) ||
                    !lottie_static_scalar_property(*raw_operator, "pt", smooth) ||
                    operation.size < 0.0f || operation.points_per_unit <= 0.0f ||
                    (smooth != 1.0f && smooth != 2.0f))
                    return std::string("Lottie zig-zag parameters must be static and bounded");
                operation.smooth = smooth == 2.0f;
                operation.kind = PathOperatorKindPlan::ZigZag;
            } else if (kind == "op") {
                float join = 1.0f;
                if (!lottie_static_scalar_property(*raw_operator, "a", operation.amount) ||
                    !lottie_static_scalar_property(*raw_operator, "ml", operation.miter_limit) ||
                    !lottie_static_scalar_property(*raw_operator, "lj", join) ||
                    operation.miter_limit <= 0.0f || join != 1.0f)
                    return std::string("Lottie offset-path parameters require static miter joins");
                operation.kind = PathOperatorKindPlan::OffsetPath;
            } else if (kind == "pb") {
                if (!lottie_static_scalar_property(*raw_operator, "a", operation.amount) ||
                    std::abs(operation.amount) > 100.0f)
                    return std::string("Lottie pucker/bloat amount must be static and within [-100, 100]");
                operation.amount /= 100.0f;
                operation.kind = PathOperatorKindPlan::PuckerBloat;
            } else if (kind == "rp") {
                float copies = 0.0f;
                if (!lottie_static_scalar_property(*raw_operator, "c", copies) || copies < 1.0f ||
                    copies > 256.0f || std::floor(copies) != copies ||
                    !raw_operator->contains("tr") || !(*raw_operator)["tr"].is_object())
                    return std::string("Lottie repeater copy count/transform is unsupported or exceeds 256");
                operation.kind = PathOperatorKindPlan::Repeater;
                operation.repeater.copies = static_cast<std::uint32_t>(copies);
                auto& transform = (*raw_operator)["tr"];
                std::vector<float> rp, ra, rs, rr;
                bool ap{}, aa{}, as{}, ar{};
                if (!lottie_read_transform_property(transform, "p", rp, ap) || ap || rp.size() < 2 || rp.size() > 3 ||
                    !lottie_read_transform_property(transform, "a", ra, aa) || aa || ra.size() < 2 || ra.size() > 3 ||
                    !lottie_read_transform_property(transform, "s", rs, as) || as || rs.size() < 2 || rs.size() > 3 ||
                    !lottie_read_transform_property(transform, "r", rr, ar) || ar || rr.size() != 1)
                    return std::string("Lottie repeater transform must be static position, anchor, scale and rotation");
                operation.repeater.position = {rp[0], rp[1]};
                operation.repeater.anchor = {ra[0], ra[1]};
                operation.repeater.scale = {rs[0] / 100.0f, rs[1] / 100.0f};
                operation.repeater.rotation_degrees = rr[0];
                float start_opacity = 100.0f, end_opacity = 100.0f;
                if ((transform.contains("so") && !lottie_static_scalar_property(transform, "so", start_opacity)) ||
                    (transform.contains("eo") && !lottie_static_scalar_property(transform, "eo", end_opacity)) ||
                    std::abs(start_opacity - 100.0f) > 1e-6f ||
                    end_opacity < 0.0f || end_opacity > 100.0f)
                    return std::string("Lottie repeater opacity range is unsupported");
                operation.repeater.opacity_decay = std::pow(
                    end_opacity / 100.0f, 1.0f / std::max(1.0f, copies - 1.0f));
            }
            shape.operators.push_back(std::move(operation));
        }
        const auto point = [](const json& value, std::array<float, 2>& out) -> bool {
            if (!value.is_array() || value.size() != 2) return false;
            for (std::size_t i = 0; i < 2; ++i) {
                if (!value[i].is_number()) return false;
                const float v = value[i].get<float>();
                if (!std::isfinite(v) || std::abs(v) > 100000.0f) return false;
                out[i] = std::round(v * 10000.0f) / 10000.0f;
            }
            return true;
        };
        if (layer.contains("masksProperties")) {
            layer_plan.masks.clear();
            std::size_t total_mask_path_commands = 0;
            for (const auto& raw_mask : layer["masksProperties"]) {
                if (!raw_mask.is_object() || !raw_mask.contains("mode") ||
                    !raw_mask["mode"].is_string() || !raw_mask.contains("pt") ||
                    !raw_mask["pt"].is_object() || !raw_mask["pt"].contains("a") ||
                    !raw_mask["pt"]["a"].is_number_integer() ||
                    (raw_mask["pt"]["a"].get<int>() != 0 && raw_mask["pt"]["a"].get<int>() != 1) ||
                    !raw_mask["pt"].contains("k") ||
                    (raw_mask.contains("inv") && !raw_mask["inv"].is_boolean()))
                    return std::string("Lottie masks require static or two-keyframe path geometry");
                LayerPlan::PathMaskPlan mask;
                const auto mode = raw_mask["mode"].get<std::string>();
                if (mode == "a") mask.mode = "add";
                else if (mode == "s") mask.mode = "subtract";
                else if (mode == "i") mask.mode = "intersect";
                else return std::string("Lottie mask mode must be add, subtract, or intersect");
                mask.type = "path";
                mask.inverted = raw_mask.value("inv", false);
                const auto decode_mask_geometry = [&](const json& geometry,
                    std::vector<ShapePathCommandPlan>& commands) -> bool {
                    if (!geometry.is_object() || !geometry.contains("v") ||
                        !geometry.contains("i") || !geometry.contains("o") ||
                        !geometry["v"].is_array() || geometry["v"].size() < 2 ||
                        geometry["v"].size() > 65535 || !geometry["i"].is_array() ||
                        !geometry["o"].is_array() || geometry["i"].size() != geometry["v"].size() ||
                        geometry["o"].size() != geometry["v"].size() ||
                        (geometry.contains("c") && !geometry["c"].is_boolean())) return false;
                    for (std::size_t vertex_index = 0; vertex_index < geometry["v"].size(); ++vertex_index) {
                        std::array<float, 2> vertex{}, incoming{}, outgoing{};
                        if (!point(geometry["v"][vertex_index], vertex) ||
                            !point(geometry["i"][vertex_index], incoming) ||
                            !point(geometry["o"][vertex_index], outgoing)) return false;
                        vertex[0] -= anchor_x; vertex[1] -= anchor_y;
                        if (vertex_index == 0) {
                            commands.push_back({ShapePathCommandTypePlan::MoveTo, vertex});
                            continue;
                        }
                        std::array<float, 2> prior{}, prior_out{};
                        if (!point(geometry["v"][vertex_index - 1], prior) ||
                            !point(geometry["o"][vertex_index - 1], prior_out)) return false;
                        prior[0] -= anchor_x; prior[1] -= anchor_y;
                        const std::array<float, 2> c1{prior[0] + prior_out[0], prior[1] + prior_out[1]};
                        const std::array<float, 2> c2{vertex[0] + incoming[0], vertex[1] + incoming[1]};
                        commands.push_back({ShapePathCommandTypePlan::CubicTo, vertex,
                            {std::round(c1[0] * 10000.0f) / 10000.0f,
                             std::round(c1[1] * 10000.0f) / 10000.0f},
                            {std::round(c2[0] * 10000.0f) / 10000.0f,
                             std::round(c2[1] * 10000.0f) / 10000.0f}});
                    }
                    if (geometry.value("c", false))
                        commands.push_back({ShapePathCommandTypePlan::Close});
                    return true;
                };
                const auto& raw_path = raw_mask["pt"]["k"];
                if (raw_mask["pt"]["a"].get<int>() == 0) {
                    if (!decode_mask_geometry(raw_path, mask.path))
                        return std::string("Lottie mask path vertex and tangent arrays are invalid");
                } else {
                    const auto shape_value = [](const json& value) -> const json* {
                        if (!value.is_array() || value.size() != 1 || !value[0].is_object()) return nullptr;
                        return &value[0];
                    };
                    if (!raw_path.is_array() || raw_path.size() != 2 ||
                        !raw_path[0].is_object() || !raw_path[1].is_object() ||
                        !raw_path[0].contains("t") || !raw_path[1].contains("t") ||
                        !raw_path[0]["t"].is_number() || !raw_path[1]["t"].is_number() ||
                        !raw_path[0].contains("s"))
                        return std::string("Lottie animated mask path requires exactly two keyframes");
                    const auto* source_geometry = shape_value(raw_path[0]["s"]);
                    const auto* target_geometry = raw_path[0].contains("e")
                        ? shape_value(raw_path[0]["e"])
                        : raw_path[1].contains("s") ? shape_value(raw_path[1]["s"]) : nullptr;
                    if (!source_geometry || !target_geometry ||
                        !decode_mask_geometry(*source_geometry, mask.path) ||
                        !decode_mask_geometry(*target_geometry, mask.morph_target) ||
                        mask.path.size() != mask.morph_target.size() ||
                        raw_path[0].contains("o") || raw_path[1].contains("i"))
                        return std::string("Lottie animated mask paths require matching topology and linear/hold timing");
                    const double first_time = raw_path[0]["t"].get<double>();
                    const double last_time = raw_path[1]["t"].get<double>();
                    if (!std::isfinite(first_time) || !std::isfinite(last_time) || first_time < 0.0 ||
                        last_time <= first_time || last_time > 1000000.0 ||
                        std::floor(first_time) != first_time || std::floor(last_time) != last_time)
                        return std::string("Lottie mask keyframe times must be increasing integral frames");
                    AnimationTrackPlan progress;
                    progress.property = "mask_morph";
                    AnimationKeyframePlan from;
                    from.frame = Frame{static_cast<std::int64_t>(first_time)};
                    from.value = {0.0f};
                    from.interpolation = raw_path[0].value("h", 0) == 1 ? "hold" : "linear";
                    AnimationKeyframePlan to;
                    to.frame = Frame{static_cast<std::int64_t>(last_time)};
                    to.value = {1.0f};
                    progress.keyframes = {std::move(from), std::move(to)};
                    mask.morph_animation = std::move(progress);
                }
                const std::size_t mask_commands = mask.path.size() + mask.morph_target.size();
                if (mask_commands > 65536 - total_mask_path_commands)
                    return std::string("Lottie total mask path command limit exceeded");
                total_mask_path_commands += mask_commands;
                float opacity_percent = 100.0f;
                if (raw_mask.contains("o") &&
                    !lottie_static_scalar_property(raw_mask, "o", opacity_percent)) {
                    AnimationTrackPlan track;
                    if (!lottie_decode_transform_track(raw_mask, "o", "opacity", 100.0f, track) ||
                        std::any_of(track.keyframes.begin(), track.keyframes.end(),
                            [](const auto& key) {
                                return key.value.size() != 1 || key.value[0] < 0.0f || key.value[0] > 1.0f;
                            }))
                        return std::string("Lottie mask opacity must be bounded and static or animated");
                    mask.opacity_track = std::move(track);
                    opacity_percent = mask.opacity_track->keyframes.front().value.front() * 100.0f;
                }
                if (opacity_percent < 0.0f || opacity_percent > 100.0f)
                    return std::string("Lottie mask opacity must be within [0,100]");
                mask.opacity = opacity_percent / 100.0f;
                if (raw_mask.contains("f")) {
                    std::vector<float> feather;
                    bool animated_feather = false;
                    if (!lottie_read_transform_property(raw_mask, "f", feather, animated_feather) ||
                        animated_feather || (feather.size() != 1 && feather.size() != 2) ||
                        (feather.size() == 2 && std::abs(feather[0] - feather[1]) > 1e-5f))
                        return std::string("Lottie mask feather must be a static scalar or uniform Vec2");
                    mask.feather = feather.front();
                }
                if (raw_mask.contains("x") &&
                    !lottie_static_scalar_property(raw_mask, "x", mask.expansion))
                    return std::string("Lottie mask expansion must be a static scalar value");
                if (!std::isfinite(mask.feather) || !std::isfinite(mask.expansion) ||
                    std::abs(mask.feather) > 1000000.0f || std::abs(mask.expansion) > 1000000.0f)
                    return std::string("Lottie mask feather/expansion exceeds supported bounds");
                layer_plan.masks.push_back(std::move(mask));
            }
        }
        std::size_t total_vertices = 0;
        std::optional<PathOperatorPlan> imported_morph;
        std::vector<std::size_t> geometry_command_starts;
        geometry_command_starts.reserve(geometry_shapes.size());
        const auto apply_geometry_transform = [&](std::size_t start, const Affine2D& transform) {
            for (std::size_t command_index = start; command_index < shape.path.size(); ++command_index) {
                auto& command = shape.path[command_index];
                const auto transform_point = [&](std::array<float, 2>& point_value) {
                    const float x = point_value[0], y = point_value[1];
                    point_value = {transform.a * x + transform.c * y + transform.x,
                                   transform.b * x + transform.d * y + transform.y};
                };
                if (command.type != ShapePathCommandTypePlan::Close) transform_point(command.point);
                if (command.type == ShapePathCommandTypePlan::CubicTo ||
                    command.type == ShapePathCommandTypePlan::QuadraticTo) transform_point(command.control1);
                if (command.type == ShapePathCommandTypePlan::CubicTo) transform_point(command.control2);
            }
        };
        for (const auto& geometry_entry : geometry_shapes) {
          const auto* path_shape = geometry_entry.item;
          const auto shape_type = path_shape->value("ty", std::string{});
          geometry_command_starts.push_back(shape.path.size());
          if (shape_type == "el" || shape_type == "rc") {
              std::array<float, 2> position{}, size{};
              if (!lottie_static_vec2_property(*path_shape, "p", position) ||
                  !lottie_static_vec2_property(*path_shape, "s", size) ||
                  size[0] <= 0.0f || size[1] <= 0.0f)
                  return std::string("Lottie ellipse/rectangle requires static bounded position and size");
              const float half_w = size[0] * 0.5f;
              const float half_h = size[1] * 0.5f;
              float radius = 0.0f;
              if (shape_type == "rc" && path_shape->contains("r") &&
                  !lottie_static_scalar_property(*path_shape, "r", radius))
                  return std::string("Lottie rectangle roundness must be static and bounded");
              if (radius < 0.0f || radius > std::min(half_w, half_h))
                  return std::string("Lottie rectangle roundness exceeds its bounds");
              const auto quantize = [](float value) { return std::round(value * 10000.0f) / 10000.0f; };
              const auto primitive_point = [&](float x, float y) {
                  return std::array<float, 2>{quantize(position[0] + x), quantize(position[1] + y)};
              };
              const auto move_to = [&](float x, float y) {
                  if (shape.path.size() >= 65536) return false;
                  shape.path.push_back({ShapePathCommandTypePlan::MoveTo, primitive_point(x, y)});
                  return true;
              };
              const auto line_to = [&](float x, float y) {
                  if (shape.path.size() >= 65536) return false;
                  shape.path.push_back({ShapePathCommandTypePlan::LineTo, primitive_point(x, y)});
                  return true;
              };
              const auto cubic_to = [&](float c1x, float c1y, float c2x, float c2y,
                                        float x, float y) {
                  if (shape.path.size() >= 65536) return false;
                  shape.path.push_back({ShapePathCommandTypePlan::CubicTo, primitive_point(x, y),
                      primitive_point(c1x, c1y), primitive_point(c2x, c2y)});
                  return true;
              };
              constexpr float kappa = 0.5522847498307936f;
              if (shape_type == "el") {
                  const float rx = half_w, ry = half_h;
                  if (!move_to(rx, 0.0f) ||
                      !cubic_to(rx, kappa * ry, kappa * rx, ry, 0.0f, ry) ||
                      !cubic_to(-kappa * rx, ry, -rx, kappa * ry, -rx, 0.0f) ||
                      !cubic_to(-rx, -kappa * ry, -kappa * rx, -ry, 0.0f, -ry) ||
                      !cubic_to(kappa * rx, -ry, rx, -kappa * ry, rx, 0.0f))
                      return std::string("Lottie total path command limit exceeded");
              } else {
                  const float left = -half_w, right = half_w;
                  const float top = -half_h, bottom = half_h;
                  if (!move_to(left + radius, top))
                      return std::string("Lottie total path command limit exceeded");
                  if (radius == 0.0f) {
                      if (!line_to(right, top) || !line_to(right, bottom) ||
                          !line_to(left, bottom) || !line_to(left, top))
                          return std::string("Lottie total path command limit exceeded");
                  } else {
                      const float control = radius * kappa;
                      if (!line_to(right - radius, top) ||
                          !cubic_to(right - radius + control, top, right, top + radius - control, right, top + radius) ||
                          !line_to(right, bottom - radius) ||
                          !cubic_to(right, bottom - radius + control, right - radius + control, bottom, right - radius, bottom) ||
                          !line_to(left + radius, bottom) ||
                          !cubic_to(left + radius - control, bottom, left, bottom - radius + control, left, bottom - radius) ||
                          !line_to(left, top + radius) ||
                          !cubic_to(left, top + radius - control, left + radius - control, top, left + radius, top))
                          return std::string("Lottie total path command limit exceeded");
                  }
              }
              if (shape.path.size() >= 65536)
                  return std::string("Lottie total path command limit exceeded");
              shape.path.push_back({ShapePathCommandTypePlan::Close});
              apply_geometry_transform(geometry_command_starts.back(), geometry_entry.transform);
              continue;
          }
          if (!path_shape->contains("ks") || !(*path_shape)["ks"].contains("k"))
              return std::string("Lottie path geometry is missing");
          json animated_source;
          std::vector<json> animated_targets;
          AnimationTrackPlan morph_progress;
          bool has_animated_morph = false;
          const json* geometry_data_ptr = &(*path_shape)["ks"]["k"];
          const auto& raw_geometry = *geometry_data_ptr;
          if ((*path_shape)["ks"].value("a", 0) == 1) {
              const auto& keys = raw_geometry;
              if (!keys.is_array() || keys.size() < 2 || keys.size() > 256)
                  return std::string("Lottie L4 path morph requires 2 to 256 bounded keyframes");
              const auto shape_value = [](const json& value) -> const json* {
                  if (!value.is_array() || value.size() != 1 || !value[0].is_object()) return nullptr;
                  return &value[0];
              };
              if (!keys[0].is_object() || !keys[0].contains("s"))
                  return std::string("Lottie L4 first path keyframe is missing its source geometry");
              const auto* from = shape_value(keys[0]["s"]);
              if (!from || !from->contains("v") || !from->contains("i") || !from->contains("o"))
                  return std::string("Lottie L4 path keyframe payload is malformed");
              animated_source = *from;
              geometry_data_ptr = &animated_source;
              std::vector<std::int64_t> key_times;
              key_times.reserve(keys.size());
              for (std::size_t key_index = 0; key_index < keys.size(); ++key_index) {
                  const auto& key = keys[key_index];
                  if (!key.is_object() || !key.contains("t") || !key["t"].is_number())
                      return std::string("Lottie L4 path keyframe time is malformed");
                  const double time = key["t"].get<double>();
                  if (!std::isfinite(time) || time < 0.0 || time > 1000000.0 ||
                      std::floor(time) != time ||
                      (key_index > 0 && time <= static_cast<double>(key_times.back())))
                      return std::string("Lottie L4 path keyframe times must be increasing integral frames");
                  key_times.push_back(static_cast<std::int64_t>(time));
                  if (key_index + 1 == keys.size()) continue;
                  const auto* target = key.contains("e") ? shape_value(key["e"]) :
                      (keys[key_index + 1].contains("s") ? shape_value(keys[key_index + 1]["s"]) : nullptr);
                  if (!target || !target->contains("v") || !target->contains("i") || !target->contains("o"))
                      return std::string("Lottie L4 path keyframe target geometry is malformed");
                  if (!target->at("v").is_array() || target->at("v").size() != from->at("v").size() ||
                      !target->at("i").is_array() || !target->at("o").is_array() ||
                      target->at("i").size() != from->at("i").size() ||
                      target->at("o").size() != from->at("o").size() ||
                      target->value("c", false) != from->value("c", false))
                      return std::string("Lottie L4 morph keyframes must keep matching path topology");
                  animated_targets.push_back(*target);
                  if (key.contains("o") || keys[key_index + 1].contains("i"))
                      return std::string("Lottie L4 currently accepts linear or hold morph interpolation only");
              }
              morph_progress.property = "morph_progress";
              for (std::size_t key_index = 0; key_index < keys.size(); ++key_index) {
                  AnimationKeyframePlan key;
                  key.frame = Frame{key_times[key_index]};
                  key.value = {static_cast<float>(key_index)};
                  key.interpolation = key_index + 1 < keys.size() && keys[key_index].value("h", 0) == 1
                      ? "hold" : "linear";
                  morph_progress.keyframes.push_back(std::move(key));
              }
              has_animated_morph = true;
          }
          const auto& geometry_data = *geometry_data_ptr;
          if (!geometry_data.is_object() || !geometry_data.contains("v") || !geometry_data.contains("i") ||
              !geometry_data.contains("o") || !geometry_data["v"].is_array() ||
              geometry_data["v"].size() < 2 || geometry_data["v"].size() > 65536 ||
              !geometry_data["i"].is_array() || !geometry_data["o"].is_array() ||
              geometry_data["i"].size() != geometry_data["v"].size() ||
              geometry_data["o"].size() != geometry_data["v"].size() ||
              geometry_data["v"].size() > 65536 - total_vertices)
              return std::string("Lottie static path vertex and tangent arrays are invalid or over limit");
          total_vertices += geometry_data["v"].size();
          for (std::size_t i = 0; i < geometry_data["v"].size(); ++i) {
            std::array<float, 2> vertex{}, outgoing{}, incoming{};
            if (!point(geometry_data["v"][i], vertex) || !point(geometry_data["o"][i], outgoing) ||
                !point(geometry_data["i"][i], incoming))
                return std::string("Lottie path contains non-finite or out-of-bounds coordinates");
            if (i == 0) {
                if (shape.path.size() >= 65536)
                    return std::string("Lottie total path command limit exceeded");
                shape.path.push_back({ShapePathCommandTypePlan::MoveTo, vertex});
                continue;
            }
            const auto& prior = geometry_data["v"][i - 1];
            std::array<float, 2> prior_v{}, prior_o{};
            if (!point(prior, prior_v) || !point(geometry_data["o"][i - 1], prior_o))
                return std::string("Lottie path contains invalid outgoing tangent");
            const std::array<float, 2> c1{std::round((prior_v[0] + prior_o[0]) * 10000.0f) / 10000.0f,
                                            std::round((prior_v[1] + prior_o[1]) * 10000.0f) / 10000.0f};
            const std::array<float, 2> c2{std::round((vertex[0] + incoming[0]) * 10000.0f) / 10000.0f,
                                            std::round((vertex[1] + incoming[1]) * 10000.0f) / 10000.0f};
            if (shape.path.size() >= 65536)
                return std::string("Lottie total path command limit exceeded");
            shape.path.push_back({ShapePathCommandTypePlan::CubicTo, vertex, c1, c2});
          }
          if (geometry_data.value("c", false)) {
              if (shape.path.size() >= 65536)
                  return std::string("Lottie total path command limit exceeded");
              shape.path.push_back({ShapePathCommandTypePlan::Close});
          }
          if (has_animated_morph) {
              if (geometry_shapes.size() != 1 || !path_operators.empty())
                  return std::string("Lottie L4 morph requires exactly one geometry path");
              PathOperatorPlan morph;
              morph.kind = PathOperatorKindPlan::Morph;
              morph.morph_animation = std::move(morph_progress);
              const auto decode_morph_target = [&](const json& target_geometry,
                                                   std::vector<ShapePathCommandPlan>& output) -> bool {
                for (std::size_t i = 0; i < target_geometry["v"].size(); ++i) {
                  std::array<float, 2> vertex{}, incoming{}, outgoing{};
                  if (!point(target_geometry["v"][i], vertex) || !point(target_geometry["i"][i], incoming) ||
                      !point(target_geometry["o"][i], outgoing)) return false;
                  if (i == 0) { output.push_back({ShapePathCommandTypePlan::MoveTo, vertex}); continue; }
                  std::array<float, 2> prior{}, prior_out{};
                  if (!point(target_geometry["v"][i - 1], prior) || !point(target_geometry["o"][i - 1], prior_out)) return false;
                  output.push_back({ShapePathCommandTypePlan::CubicTo, vertex,
                    {std::round((prior[0] + prior_out[0]) * 10000.0f) / 10000.0f,
                     std::round((prior[1] + prior_out[1]) * 10000.0f) / 10000.0f},
                    {std::round((vertex[0] + incoming[0]) * 10000.0f) / 10000.0f,
                     std::round((vertex[1] + incoming[1]) * 10000.0f) / 10000.0f}});
                }
                if (target_geometry.value("c", false)) output.push_back({ShapePathCommandTypePlan::Close});
                return true;
              };
              for (const auto& target_geometry : animated_targets) {
                std::vector<ShapePathCommandPlan> target_path;
                if (!decode_morph_target(target_geometry, target_path))
                    return std::string("Lottie L4 morph keyframe has invalid coordinates or tangents");
                morph.morph_keyframe_targets.push_back(std::move(target_path));
              }
              morph.morph_target = morph.morph_keyframe_targets.back();
              for (auto& target_path : morph.morph_keyframe_targets) for (auto& command : target_path) {
                  const auto transform_point = [&](std::array<float, 2>& p) {
                      const float x = p[0], y = p[1];
                      p = {geometry_entry.transform.a*x + geometry_entry.transform.c*y + geometry_entry.transform.x,
                           geometry_entry.transform.b*x + geometry_entry.transform.d*y + geometry_entry.transform.y};
                  };
                  if (command.type != ShapePathCommandTypePlan::Close) transform_point(command.point);
                  if (command.type == ShapePathCommandTypePlan::CubicTo || command.type == ShapePathCommandTypePlan::QuadraticTo) transform_point(command.control1);
                  if (command.type == ShapePathCommandTypePlan::CubicTo) transform_point(command.control2);
              }
              morph.morph_target = morph.morph_keyframe_targets.back();
              imported_morph = std::move(morph);
          }
          apply_geometry_transform(geometry_command_starts.back(), geometry_entry.transform);
        }
        if (anchor_x != 0.0f || anchor_y != 0.0f) {
            // Bake the static pivot into geometry, preserving the native
            // layer transform's position/scale/rotation behavior.
            for (auto& command : shape.path) {
                command.point[0] -= anchor_x; command.point[1] -= anchor_y;
                command.control1[0] -= anchor_x; command.control1[1] -= anchor_y;
                command.control2[0] -= anchor_x; command.control2[1] -= anchor_y;
            }
            if (imported_morph) {
                const auto offset_path = [&](auto& path) {
                  for (auto& command : path) {
                    if (command.type == ShapePathCommandTypePlan::Close) continue;
                    command.point[0] -= anchor_x; command.point[1] -= anchor_y;
                    command.control1[0] -= anchor_x; command.control1[1] -= anchor_y;
                    command.control2[0] -= anchor_x; command.control2[1] -= anchor_y;
                  }
                };
                offset_path(imported_morph->morph_target);
                for (auto& path : imported_morph->morph_keyframe_targets) offset_path(path);
            }
        }
        if (fills.size() != 1 && fills.size() != geometry_shapes.size())
            return std::string("Lottie supports one solid fill or one fill per static geometry");
        if (!strokes.empty() && strokes.size() != 1 && strokes.size() != geometry_shapes.size())
            return std::string("Lottie supports one solid stroke or one stroke per static geometry");
        if (fills.empty() && strokes.empty())
            return std::string("Lottie static geometry requires a solid fill or stroke");

        struct SolidPaint {
            std::array<float, 4> color{};
            std::optional<AnimationTrackPlan> color_animation;
            std::optional<AnimationTrackPlan> opacity_animation;
        };
        const auto decode_lottie_color = [&](const json& style,
                                             SolidPaint& paint,
                                             std::string_view color_property,
                                             std::string_view opacity_property) -> bool {
            if (style.value("r", 1) != 1 || !style.contains("c") ||
                !style["c"].is_object() || !style["c"].contains("k")) return false;
            std::array<float, 4> first_color{};
            const auto decode_rgba = [](const json& value, std::array<float, 4>& color) {
                if (!value.is_array() || value.size() != 4) return false;
                for (std::size_t i = 0; i < 4; ++i) {
                    if (!value[i].is_number()) return false;
                    color[i] = value[i].get<float>();
                    if (!std::isfinite(color[i]) || color[i] < 0.0f || color[i] > 1.0f)
                        return false;
                }
                return true;
            };
            const auto& color_property_json = style["c"];
            if (color_property_json.value("a", 0) == 1) {
                AnimationTrackPlan track;
                const std::string property_name{color_property};
                if (!lottie_decode_transform_track(style, "c", property_name.c_str(), 1.0f, track) ||
                    track.keyframes.empty()) return false;
                for (const auto& key : track.keyframes) {
                    if (key.value.size() != 4 || std::any_of(key.value.begin(), key.value.end(),
                            [](float component) {
                                return !std::isfinite(component) || component < 0.0f || component > 1.0f;
                            })) return false;
                }
                std::copy_n(track.keyframes.front().value.begin(), 4, first_color.begin());
                paint.color_animation = std::move(track);
            } else if (!decode_rgba(color_property_json["k"], first_color)) {
                return false;
            }
            float opacity = 100.0f;
            if (style.contains("o")) {
                if (!lottie_static_scalar_property(style, "o", opacity)) {
                    AnimationTrackPlan track;
                    const std::string property_name{opacity_property};
                    if (!lottie_decode_transform_track(style, "o", property_name.c_str(), 100.0f, track) ||
                        track.keyframes.empty()) return false;
                    for (const auto& key : track.keyframes)
                        if (key.value.size() != 1 || key.value[0] < 0.0f || key.value[0] > 1.0f)
                            return false;
                    opacity = 100.0f;
                    paint.opacity_animation = std::move(track);
                }
            }
            if (opacity < 0.0f || opacity > 100.0f) return false;
            if (!paint.opacity_animation) {
                first_color[3] *= opacity / 100.0f;
                if (paint.color_animation) {
                    for (auto& key : paint.color_animation->keyframes)
                        key.value[3] *= opacity / 100.0f;
                }
            }
            paint.color = first_color;
            return true;
        };
        const auto decode_lottie_gradient = [&](const json& style,
                                                graphics::GradientDefinition& gradient) -> bool {
            if (style.value("r", 1) != 1 || !style.contains("g") ||
                !style["g"].is_object() || !style["g"].contains("p") ||
                !style["g"]["p"].is_number_integer() || !style["g"].contains("k") ||
                !style["g"]["k"].is_object() || style["g"]["k"].value("a", 1) != 0 ||
                !style["g"]["k"].contains("k")) return false;
            const auto count = style["g"]["p"].get<int>();
            const auto& raw_stops = style["g"]["k"]["k"];
            if (count < 2 || count > 128 || !raw_stops.is_array() ||
                raw_stops.size() < static_cast<std::size_t>(count) * 4 ||
                raw_stops.size() > static_cast<std::size_t>(count) * 4 + 256)
                return false;
            std::array<float, 2> start{}, end{};
            if (!lottie_static_vec2_property(style, "s", start) ||
                !lottie_static_vec2_property(style, "e", end)) return false;
            const auto kind = style.value("t", 1);
            if (kind != 1 && kind != 2) return false;
            float opacity = 100.0f;
            if (style.contains("o") && !lottie_static_scalar_property(style, "o", opacity)) return false;
            if (opacity < 0.0f || opacity > 100.0f) return false;
            std::vector<graphics::GradientStop> colors;
            colors.reserve(static_cast<std::size_t>(count));
            const auto bounded_unit = [](float value) {
                return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
            };
            for (int index = 0; index < count; ++index) {
                const auto offset = static_cast<std::size_t>(index) * 4;
                if (!raw_stops[offset].is_number() || !raw_stops[offset + 1].is_number() ||
                    !raw_stops[offset + 2].is_number() || !raw_stops[offset + 3].is_number()) return false;
                const float position = raw_stops[offset].get<float>();
                const float red = raw_stops[offset + 1].get<float>();
                const float green = raw_stops[offset + 2].get<float>();
                const float blue = raw_stops[offset + 3].get<float>();
                if (!bounded_unit(position) || !bounded_unit(red) ||
                    !bounded_unit(green) || !bounded_unit(blue)) return false;
                colors.push_back({position, Color{red, green, blue, opacity / 100.0f}});
            }
            std::vector<graphics::OpacityStop> opacity_stops;
            const auto opacity_values = raw_stops.size() - static_cast<std::size_t>(count) * 4;
            if (opacity_values % 2 != 0) return false;
            for (std::size_t offset = static_cast<std::size_t>(count) * 4;
                 offset < raw_stops.size(); offset += 2) {
                if (!raw_stops[offset].is_number() || !raw_stops[offset + 1].is_number()) return false;
                const float position = raw_stops[offset].get<float>();
                const float value = raw_stops[offset + 1].get<float>();
                if (!bounded_unit(position) || !bounded_unit(value)) return false;
                opacity_stops.push_back({position, value * opacity / 100.0f});
            }
            const Vec2 from{start[0], start[1]}, to{end[0], end[1]};
            if (kind == 1) {
                gradient = graphics::GradientDefinition::linear(from, to, std::move(colors));
            } else {
                const float radius = glm::length(to - from);
                if (!std::isfinite(radius) || radius <= 1e-6f) return false;
                gradient = graphics::GradientDefinition::radial(from, radius, std::move(colors));
            }
            gradient.opacity_stops = std::move(opacity_stops);
            return true;
        };
        std::vector<std::optional<SolidPaint>> fill_paints;
        std::vector<std::optional<graphics::GradientDefinition>> fill_gradients;
        for (const auto* fill : fills) {
            if (fill->value("ty", std::string{}) == "gf") {
                graphics::GradientDefinition gradient;
                if (!decode_lottie_gradient(*fill, gradient))
                    return std::string("Lottie gradient fill is malformed or unsupported");
                fill_paints.push_back(std::nullopt);
                fill_gradients.push_back(std::move(gradient));
            } else {
                SolidPaint paint;
                if (!decode_lottie_color(*fill, paint, "lottie_fill_color", "lottie_fill_opacity"))
                    return std::string("Lottie fill must have bounded RGBA color/opacity values or keyframes");
                fill_paints.push_back(std::move(paint));
                fill_gradients.push_back(std::nullopt);
            }
        }
        std::vector<std::optional<StrokeStyle>> stroke_styles;
        std::vector<std::optional<AnimationTrackPlan>> stroke_color_tracks;
        std::vector<std::optional<AnimationTrackPlan>> stroke_opacity_tracks;
        std::vector<std::optional<AnimationTrackPlan>> stroke_width_tracks;
        for (const auto* stroke : strokes) {
            SolidPaint paint;
            float width = 0.0f;
            std::optional<AnimationTrackPlan> width_animation;
            if (!decode_lottie_color(*stroke, paint, "lottie_stroke_color", "lottie_stroke_opacity"))
                return std::string("Lottie stroke color/opacity values or keyframes are unsupported");
            if (!lottie_static_scalar_property(*stroke, "w", width)) {
                AnimationTrackPlan track;
                if (!lottie_decode_transform_track(*stroke, "w", "lottie_stroke_width", 1.0f, track) ||
                    track.keyframes.empty() || std::any_of(track.keyframes.begin(), track.keyframes.end(),
                        [](const auto& key) { return key.value.size() != 1 || key.value[0] < 0.0f; }))
                    return std::string("Lottie stroke width must be bounded and static or animated");
                width = track.keyframes.front().value.front();
                width_animation = std::move(track);
            }
            if (width < 0.0f ||
                stroke->value("lc", 1) != 1 || stroke->value("lj", 1) != 1 ||
                stroke->value("ml", 4.0f) != 4.0f)
                return std::string("Lottie stroke requires bounded color/opacity/width values and default cap/join");
            StrokeStyle style;
            style.width = width;
            style.solid_color = paint.color;
            if (stroke->contains("d")) {
                if (!(*stroke)["d"].is_array() || (*stroke)["d"].size() > 128)
                    return std::string("Lottie stroke dash list is malformed or exceeds 128 entries");
                for (const auto& dash : (*stroke)["d"]) {
                    if (!dash.is_object() || !dash.contains("n") || !dash.contains("v"))
                        return std::string("Lottie stroke dash entry is malformed");
                    float value = 0.0f;
                    if (!lottie_static_scalar_property(dash, "v", value) || value < 0.0f)
                        return std::string("Lottie stroke dash values must be static and non-negative");
                    const auto kind = dash["n"].get<std::string>();
                    if (kind == "o") style.dash_offset = value;
                    else if (kind == "d" || kind == "g") style.dash_array.push_back(value);
                    else return std::string("unsupported Lottie stroke dash entry");
                }
                if (style.dash_array.empty())
                    return std::string("Lottie stroke dash list has no dash or gap entries");
            }
            stroke_styles.push_back(std::move(style));
            stroke_color_tracks.push_back(std::move(paint.color_animation));
            stroke_opacity_tracks.push_back(std::move(paint.opacity_animation));
            stroke_width_tracks.push_back(std::move(width_animation));
        }
        shape.imported_subpaths.clear();
        shape.imported_subpaths.reserve(geometry_shapes.size());
        for (std::size_t index = 0; index < geometry_shapes.size(); ++index) {
            const std::size_t begin = geometry_command_starts[index];
            const std::size_t end = index + 1 < geometry_command_starts.size()
                ? geometry_command_starts[index + 1] : shape.path.size();
            ImportedSubpathPlan imported;
            imported.path.assign(shape.path.begin() + static_cast<std::ptrdiff_t>(begin),
                                 shape.path.begin() + static_cast<std::ptrdiff_t>(end));
            imported.lottie_group_transform = geometry_shapes[index].group_animation;
            if (!fill_paints.empty()) {
                const auto fill_index = fills.size() == 1 ? 0 : index;
                if (fill_paints[fill_index]) {
                    auto& paint = *fill_paints[fill_index];
                    imported.fill_color = paint.color;
                    (*imported.fill_color)[3] *= geometry_shapes[index].opacity;
                    imported.fill_color_animation = paint.color_animation;
                    if (imported.fill_color_animation) {
                        for (auto& key : imported.fill_color_animation->keyframes)
                            key.value[3] *= geometry_shapes[index].opacity;
                    }
                    imported.fill_opacity_animation = paint.opacity_animation;
                }
                if (fill_gradients[fill_index]) {
                    imported.fill_gradient = *fill_gradients[fill_index];
                    auto& gradient = *imported.fill_gradient;
                    const auto transform_gradient_point = [&](Vec2 point_value) {
                        return Vec2{
                            geometry_shapes[index].transform.a * point_value.x +
                                geometry_shapes[index].transform.c * point_value.y +
                                geometry_shapes[index].transform.x - anchor_x,
                            geometry_shapes[index].transform.b * point_value.x +
                                geometry_shapes[index].transform.d * point_value.y +
                                geometry_shapes[index].transform.y - anchor_y};
                    };
                    if (gradient.type == graphics::GradientType::Linear) {
                        gradient.start = transform_gradient_point(gradient.start);
                        gradient.end = transform_gradient_point(gradient.end);
                    } else if (gradient.type == graphics::GradientType::Radial) {
                        const auto& matrix = geometry_shapes[index].transform;
                        const float x_scale = std::hypot(matrix.a, matrix.b);
                        const float y_scale = std::hypot(matrix.c, matrix.d);
                        const float axis_dot = matrix.a * matrix.c + matrix.b * matrix.d;
                        if (std::abs(x_scale - y_scale) > 1e-4f || std::abs(axis_dot) > 1e-4f)
                            return std::string("Lottie radial gradients under non-uniform group transforms are unsupported");
                        gradient.center = transform_gradient_point(gradient.center);
                        gradient.radius *= x_scale;
                    }
                    for (auto& stop : gradient.color_stops)
                        stop.color.a *= geometry_shapes[index].opacity;
                    for (auto& stop : gradient.opacity_stops)
                        stop.opacity *= geometry_shapes[index].opacity;
                }
            }
            if (!stroke_styles.empty()) {
                const auto stroke_index = strokes.size() == 1 ? 0 : index;
                imported.stroke = *stroke_styles[stroke_index];
                imported.stroke_color_animation = stroke_color_tracks[stroke_index];
                imported.stroke_opacity_animation = stroke_opacity_tracks[stroke_index];
                const auto& matrix = geometry_shapes[index].transform;
                const float x_scale = std::hypot(matrix.a, matrix.b);
                const float y_scale = std::hypot(matrix.c, matrix.d);
                const float axis_dot = matrix.a * matrix.c + matrix.b * matrix.d;
                if (std::abs(x_scale - y_scale) > 1e-4f || std::abs(axis_dot) > 1e-4f)
                    return std::string("Lottie strokes under non-uniform group transforms are unsupported");
                imported.stroke->width = imported.stroke->width.value_or(1.0f) * x_scale;
                for (auto& dash : imported.stroke->dash_array) dash *= x_scale;
                imported.stroke->dash_offset *= x_scale;
                if (imported.stroke->solid_color)
                    (*imported.stroke->solid_color)[3] *= geometry_shapes[index].opacity;
                imported.stroke_width_animation = stroke_width_tracks[stroke_index];
                if (imported.stroke_width_animation) {
                    for (auto& key : imported.stroke_width_animation->keyframes)
                        key.value[0] *= x_scale;
                }
                if (imported.stroke_color_animation) {
                    for (auto& key : imported.stroke_color_animation->keyframes)
                        key.value[3] *= geometry_shapes[index].opacity;
                }
            }
            if (imported_morph && index == 0) {
                imported.morph_target = imported_morph->morph_target;
                imported.morph_animation = imported_morph->morph_animation;
                imported.morph_keyframe_targets = imported_morph->morph_keyframe_targets;
            }
            shape.imported_subpaths.push_back(std::move(imported));
        }
        shape.path.clear();
        const bool has_trim_operator = std::any_of(shape.operators.begin(), shape.operators.end(),
            [](const PathOperatorPlan& operation) {
                return operation.kind == PathOperatorKindPlan::Trim;
            });
        if (has_trim_operator && shape.operators.size() != 1)
            return std::string("Lottie Trim Paths currently requires a single isolated path operator");
        if (!shape.operators.empty() && !has_trim_operator) {
            std::vector<chronon3d::PathOperatorSpec> specs;
            specs.reserve(shape.operators.size());
            for (const auto& operation : shape.operators) {
                if (operation.kind == PathOperatorKindPlan::Repeater) {
                    const auto& value = operation.repeater;
                    specs.push_back({chronon3d::PathOperatorKind::Repeater,
                        chronon3d::RepeaterParams{value.copies,
                            {value.position[0], value.position[1]},
                            {value.scale[0], value.scale[1]}, value.rotation_degrees,
                            {value.anchor[0], value.anchor[1]}, value.opacity_decay}, operation.seed});
                } else if (operation.kind == PathOperatorKindPlan::RoundCorners) {
                    specs.push_back({chronon3d::PathOperatorKind::RoundCorners,
                        chronon3d::RoundCornersParams{operation.radius}, operation.seed});
                } else if (operation.kind == PathOperatorKindPlan::ZigZag) {
                    specs.push_back({chronon3d::PathOperatorKind::ZigZag,
                        chronon3d::ZigZagParams{operation.size, operation.points_per_unit,
                                                operation.smooth}, operation.seed});
                } else if (operation.kind == PathOperatorKindPlan::OffsetPath) {
                    specs.push_back({chronon3d::PathOperatorKind::OffsetPath,
                        chronon3d::OffsetPathParams{operation.amount, operation.miter_limit}, operation.seed});
                } else if (operation.kind == PathOperatorKindPlan::PuckerBloat) {
                    specs.push_back({chronon3d::PathOperatorKind::PuckerBloat,
                        chronon3d::PuckerBloatParams{operation.amount,
                            {operation.anchor[0], operation.anchor[1]}}, operation.seed});
                }
            }
            std::vector<ImportedSubpathPlan> expanded_subpaths;
            for (const auto& imported : shape.imported_subpaths) {
                chronon3d::PathShape source_path;
                for (const auto& command : imported.path) {
                    const chronon3d::Vec2 point_value{command.point[0], command.point[1]};
                    const chronon3d::Vec2 c1{command.control1[0], command.control1[1]};
                    const chronon3d::Vec2 c2{command.control2[0], command.control2[1]};
                    switch (command.type) {
                        case ShapePathCommandTypePlan::MoveTo:
                            source_path.commands.push_back(chronon3d::PathCommand::move_to(point_value)); break;
                        case ShapePathCommandTypePlan::LineTo:
                            source_path.commands.push_back(chronon3d::PathCommand::line_to(point_value)); break;
                        case ShapePathCommandTypePlan::QuadraticTo:
                            source_path.commands.push_back(chronon3d::PathCommand::quadratic_to(c1, point_value)); break;
                        case ShapePathCommandTypePlan::CubicTo:
                            source_path.commands.push_back(chronon3d::PathCommand::cubic_to(c1, c2, point_value)); break;
                        case ShapePathCommandTypePlan::Close:
                            source_path.commands.push_back(chronon3d::PathCommand::close());
                            source_path.closed = true;
                            break;
                    }
                }
                if (imported.fill_gradient) {
                    source_path.fill.enabled = true;
                    chronon3d::graphics::FillStyle fill_style;
                    fill_style.gradient = *imported.fill_gradient;
                    source_path.fill = fill_style.to_fill();
                } else if (imported.fill_color) {
                    const auto& rgba = *imported.fill_color;
                    source_path.fill = chronon3d::Fill::solid_color(
                        chronon3d::Color{rgba[0], rgba[1], rgba[2], rgba[3]});
                } else source_path.fill.enabled = false;
                if (imported.stroke && imported.stroke->solid_color) {
                    const auto& rgba = *imported.stroke->solid_color;
                    source_path.stroke.enabled = true;
                    source_path.stroke.color = {rgba[0], rgba[1], rgba[2], rgba[3]};
                    source_path.stroke.width = imported.stroke->width.value_or(1.0f);
                    source_path.stroke.dash_array = imported.stroke->dash_array;
                    source_path.stroke.dash_offset = imported.stroke->dash_offset;
                } else source_path.stroke.enabled = false;
                auto expanded = chronon3d::apply_path_operators(source_path, specs);
                for (const auto& output_path : expanded) {
                    if (expanded_subpaths.size() >= 4096)
                        return std::string("Lottie path operator expansion exceeds 4096 sibling paths");
                    ImportedSubpathPlan output;
                    output.path.reserve(output_path.commands.size());
                    for (const auto& command : output_path.commands) {
                        ShapePathCommandPlan converted;
                        converted.point = {command.p0.x, command.p0.y};
                        converted.control1 = {command.p1.x, command.p1.y};
                        converted.control2 = {command.p2.x, command.p2.y};
                        switch (command.type) {
                            case chronon3d::PathCommandType::MoveTo: converted.type = ShapePathCommandTypePlan::MoveTo; break;
                            case chronon3d::PathCommandType::LineTo: converted.type = ShapePathCommandTypePlan::LineTo; break;
                            case chronon3d::PathCommandType::QuadraticTo: converted.type = ShapePathCommandTypePlan::QuadraticTo; break;
                            case chronon3d::PathCommandType::CubicTo: converted.type = ShapePathCommandTypePlan::CubicTo; break;
                            case chronon3d::PathCommandType::Close: converted.type = ShapePathCommandTypePlan::Close; break;
                        }
                        output.path.push_back(converted);
                    }
                    if (output_path.fill.enabled) {
                        if (output_path.fill.gradient.color_stops.empty()) {
                            const auto& c = output_path.fill.solid;
                            output.fill_color = std::array<float, 4>{c.r, c.g, c.b, c.a};
                        } else output.fill_gradient = output_path.fill.gradient;
                    }
                    if (output_path.stroke.enabled) {
                        StrokeStyle stroke;
                        stroke.width = output_path.stroke.width;
                        stroke.solid_color = std::array<float, 4>{
                            output_path.stroke.color.r, output_path.stroke.color.g,
                            output_path.stroke.color.b, output_path.stroke.color.a};
                        stroke.dash_array = output_path.stroke.dash_array;
                        stroke.dash_offset = output_path.stroke.dash_offset;
                        output.stroke = std::move(stroke);
                    }
                    expanded_subpaths.push_back(std::move(output));
                }
            }
            shape.imported_subpaths = std::move(expanded_subpaths);
            if (!has_trim_operator) shape.operators.clear();
        }
        return std::nullopt;
    } catch (const std::exception& error) {
        return std::string("malformed Lottie JSON: ") + error.what();
    }
}

}  // namespace chronon3d::render_plan::detail
