#include "render_plan_compiler_detail.hpp"
#include "render_plan_compiler_lottie_detail.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d::render_plan::detail {
namespace { using json = nlohmann::json; }

// Lottie mask import is a self-contained stage: it owns mode/path/morph/
// feather parsing and reports the first unsupported construct instead of
// leaving partially built masks behind.
std::optional<std::string> decode_lottie_masks(
    const json& layer, LayerPlan& layer_plan, float anchor_x, float anchor_y) {
    if (!layer.contains("masksProperties")) return std::nullopt;
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
                        if (!lottie_point(geometry["v"][vertex_index], vertex) ||
                            !lottie_point(geometry["i"][vertex_index], incoming) ||
                            !lottie_point(geometry["o"][vertex_index], outgoing)) return false;
                        vertex[0] -= anchor_x; vertex[1] -= anchor_y;
                        if (vertex_index == 0) {
                            commands.push_back({ShapePathCommandTypePlan::MoveTo, vertex});
                            continue;
                        }
                        std::array<float, 2> prior{}, prior_out{};
                        if (!lottie_point(geometry["v"][vertex_index - 1], prior) ||
                            !lottie_point(geometry["o"][vertex_index - 1], prior_out)) return false;
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
    return std::nullopt;
}

// Lottie path operators (trim/offset/pucker-bloat/repeater) are decoded
// independently of geometry so an unsupported operator reports its own
// diagnostic instead of failing as a generic shape error.
std::optional<std::string> decode_lottie_path_operators(
    const std::vector<const json*>& path_operators,
    ShapeContentPlan& shape) {
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
    return std::nullopt;
}



} // namespace chronon3d::render_plan::detail
