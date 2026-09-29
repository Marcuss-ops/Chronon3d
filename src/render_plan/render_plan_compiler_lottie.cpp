#include "render_plan_compiler_detail.hpp"
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
            layer.contains("masksProperties") || layer.contains("ef") ||
            layer.contains("tt") || layer.contains("tm") || layer.contains("exp"))
            return std::string("Lottie layer type, masks, effects, expressions, or time remap are unsupported");
        const auto property_is = [](const json& owner, const char* key,
                                    std::initializer_list<float> expected) {
            if (!owner.contains(key) || !owner[key].is_object() ||
                !owner[key].contains("a") || owner[key]["a"] != 0 ||
                !owner[key].contains("k")) return false;
            const auto& v = owner[key]["k"];
            if (expected.size() == 1 && v.is_number())
                return std::abs(v.get<float>() - *expected.begin()) < 1e-6f;
            if (!v.is_array() ||
                (v.size() != expected.size() &&
                 !(expected.size() == 2 && v.size() == 3) &&
                 !(expected.size() == 3 && v.size() == 2))) return false;
            std::size_t i = 0;
            for (const float want : expected) {
                if (i >= v.size()) break;
                if (!v[i].is_number() || !std::isfinite(v[i].get<float>()) ||
                    std::abs(v[i].get<float>() - want) >= 1e-6f) return false;
                ++i;
            }
            if (v.size() == 3 && expected.size() == 2)
                return v[2].is_number() && std::abs(v[2].get<float>()) < 1e-6f;
            return true;
        };
        const auto identity_transform = [&](const json& value) {
            return property_is(value, "a", {0.0f, 0.0f}) &&
                   property_is(value, "p", {0.0f, 0.0f, 0.0f}) &&
                   property_is(value, "s", {100.0f, 100.0f, 100.0f}) &&
                   property_is(value, "r", {0.0f}) &&
                   property_is(value, "o", {100.0f});
        };
        const auto static_vec2_property = [&](const json& owner, const char* key,
                                              std::array<float, 2>& out) {
            if (!owner.contains(key) || !owner[key].is_object() ||
                owner[key].value("a", 1) != 0 || !owner[key].contains("k")) return false;
            const auto& value = owner[key]["k"];
            if (!value.is_array() || value.size() != 2) return false;
            for (std::size_t i = 0; i < 2; ++i) {
                if (!value[i].is_number()) return false;
                out[i] = value[i].get<float>();
                if (!std::isfinite(out[i]) || std::abs(out[i]) > 100000.0f) return false;
            }
            return true;
        };
        const auto static_scalar_property = [&](const json& owner, const char* key,
                                                float& out) {
            if (!owner.contains(key) || !owner[key].is_object() ||
                owner[key].value("a", 1) != 0 || !owner[key].contains("k") ||
                !owner[key]["k"].is_number()) return false;
            out = owner[key]["k"].get<float>();
            return std::isfinite(out) && std::abs(out) <= 100000.0f;
        };
        const auto read_transform_property = [](const json& transform, const char* name,
                                                std::vector<float>& result, bool& animated) {
            if (!transform.contains(name) || !transform[name].is_object() ||
                !transform[name].contains("k")) return false;
            const auto& property = transform[name];
            animated = property.value("a", 0) == 1;
            if (animated) return true;
            const auto& value = property["k"];
            result.clear();
            if (value.is_number()) result.push_back(value.get<float>());
            else if (value.is_array()) {
                for (const auto& component : value) {
                    if (!component.is_number()) return false;
                    result.push_back(component.get<float>());
                }
            } else return false;
            return !result.empty() && std::all_of(result.begin(), result.end(), [](float v) {
                return std::isfinite(v) && std::abs(v) <= 100000.0f;
            });
        };
        const auto decode_transform_track = [&](const json& transform, const char* name,
                                                const char* property, float divisor,
                                                AnimationTrackPlan& track) -> bool {
            if (!transform.contains(name) || !transform[name].is_object() ||
                transform[name].value("a", 0) != 1 || !transform[name].contains("k") ||
                !transform[name]["k"].is_array() || transform[name]["k"].empty()) return false;
            const auto& raw_keys = transform[name]["k"];
            track.property = property;
            const auto components = [](const json& raw, std::vector<float>& result) {
                result.clear();
                const json* value = &raw;
                if (raw.is_array() && raw.size() == 1 && raw[0].is_array()) value = &raw[0];
                if (value->is_number()) result.push_back(value->get<float>());
                else if (value->is_array()) {
                    for (const auto& component : *value) {
                        if (!component.is_number()) return false;
                        result.push_back(component.get<float>());
                    }
                } else return false;
                return !result.empty() && std::all_of(result.begin(), result.end(), [](float v) {
                    return std::isfinite(v) && std::abs(v) <= 100000.0f;
                });
            };
            for (std::size_t index = 0; index < raw_keys.size(); ++index) {
                const auto& raw = raw_keys[index];
                if (!raw.is_object() || !raw.contains("t") || !raw["t"].is_number() ||
                    !raw.contains("s")) return false;
                const double time = raw["t"].get<double>();
                std::vector<float> value;
                if (!std::isfinite(time) || time < 0.0 || time > 1000000.0 ||
                    std::floor(time) != time || !components(raw["s"], value)) return false;
                for (auto& component : value) component /= divisor;
                AnimationKeyframePlan key;
                key.frame = Frame{static_cast<std::int64_t>(time)};
                key.value = std::move(value);
                if (index + 1 < raw_keys.size()) {
                    if (raw.value("h", 0) == 1) key.interpolation = "hold";
                    else {
                        key.interpolation = "linear";
                        const auto& next = raw_keys[index + 1];
                        if (raw.contains("o") && next.is_object() && next.contains("i")) {
                            const auto read_handle = [](const json& raw_handle,
                                                        std::array<float, 2>& handle) {
                                if (!raw_handle.is_object() || !raw_handle.contains("x") ||
                                    !raw_handle.contains("y")) return false;
                                const auto first = [](const json& v, float& out) {
                                    const json* item = &v;
                                    if (v.is_array() && !v.empty()) item = &v[0];
                                    if (!item->is_number()) return false;
                                    out = item->get<float>();
                                    return std::isfinite(out);
                                };
                                return first(raw_handle["x"], handle[0]) &&
                                       first(raw_handle["y"], handle[1]);
                            };
                            std::array<float, 2> outgoing{}, incoming{};
                            if (!read_handle(raw["o"], outgoing) || !read_handle(next["i"], incoming))
                                return false;
                            key.interpolation = "bezier";
                            key.bezier_curve = std::array<float, 4>{
                                std::clamp(outgoing[0], 0.0f, 1.0f),
                                std::clamp(outgoing[1], -10.0f, 10.0f),
                                std::clamp(incoming[0], 0.0f, 1.0f),
                                std::clamp(incoming[1], -10.0f, 10.0f)};
                        }
                    }
                }
                track.keyframes.push_back(std::move(key));
            }
            if (track.keyframes.size() < 2)
                return false;
            const auto dimensions = track.keyframes.front().value.size();
            const auto property_name = std::string_view(property);
            if ((property_name == "position" && (dimensions < 2 || dimensions > 3)) ||
                (property_name == "scale" && (dimensions < 2 || dimensions > 3)) ||
                ((property_name == "rotation_z" || property_name == "opacity") && dimensions != 1))
                return false;
            for (std::size_t index = 1; index < track.keyframes.size(); ++index) {
                if (track.keyframes[index].frame <= track.keyframes[index - 1].frame ||
                    track.keyframes[index].value.size() != dimensions) return false;
            }
            return true;
        };
        if (!layer.contains("ks") || !layer["ks"].is_object())
            return std::string("Lottie layer transform is missing");
        auto& transform = layer["ks"];
        std::vector<float> position, anchor, scale, rotation, opacity;
        bool p_animated{}, a_animated{}, s_animated{}, r_animated{}, o_animated{};
        if (!read_transform_property(transform, "p", position, p_animated) ||
            !read_transform_property(transform, "a", anchor, a_animated) ||
            !read_transform_property(transform, "s", scale, s_animated) ||
            !read_transform_property(transform, "r", rotation, r_animated) ||
            !read_transform_property(transform, "o", opacity, o_animated))
            return std::string("Lottie transform properties are malformed or unsupported");
        if ((!p_animated && (position.size() < 2 || position.size() > 3)) ||
            (!a_animated && (anchor.size() < 2 || anchor.size() > 3)) ||
            (!s_animated && (scale.size() < 2 || scale.size() > 3)) ||
            (!r_animated && rotation.size() != 1) ||
            (!o_animated && opacity.size() != 1))
            return std::string("Lottie transform property dimensions are unsupported");
        if (p_animated || a_animated || s_animated || r_animated || o_animated) {
            if (!layer_plan.animation) layer_plan.animation.emplace();
            auto add_track = [&](const char* name, const char* property, float divisor) {
                AnimationTrackPlan track;
                if (!decode_transform_track(transform, name, property, divisor, track)) return false;
                layer_plan.animation->tracks.push_back(std::move(track));
                return true;
            };
            if (p_animated && !add_track("p", "position", 1.0f))
                return std::string("unsupported Lottie position keyframes");
            if (s_animated && !add_track("s", "scale", 100.0f))
                return std::string("unsupported Lottie scale keyframes");
            if (r_animated && !add_track("r", "rotation_z", 1.0f))
                return std::string("unsupported Lottie rotation keyframes");
            if (o_animated && !add_track("o", "opacity", 100.0f))
                return std::string("unsupported Lottie opacity keyframes");
            if (a_animated)
                return std::string("animated Lottie anchor is not representable by the layer transform");
        }
        const auto component_or = [](const std::vector<float>& values, std::size_t index, float fallback) {
            return index < values.size() ? values[index] : fallback;
        };
        if (!p_animated) {
            layer_plan.position = {component_or(position, 0, 0),
                                   component_or(position, 1, 0),
                                   component_or(position, 2, 0)};
            layer_plan.position_dimensions = position.size();
        }
        if (!s_animated) {
            layer_plan.scale = {component_or(scale, 0, 100) / 100.0f,
                                component_or(scale, 1, 100) / 100.0f,
                                component_or(scale, 2, 100) / 100.0f};
            layer_plan.scale_dimensions = scale.size();
        }
        if (!r_animated) {
            layer_plan.rotation[2] = component_or(rotation, 0, 0);
            layer_plan.rotation_dimensions = 1;
        }
        if (!o_animated) {
            if (component_or(opacity, 0, 100) < 0.0f || component_or(opacity, 0, 100) > 100.0f)
                return std::string("Lottie opacity must be within [0, 100]");
            layer_plan.opacity = component_or(opacity, 0, 100) / 100.0f;
        }
        if (a_animated)
            return std::string("animated Lottie anchor is not representable by the layer transform");
        const float anchor_x = component_or(anchor, 0, 0);
        const float anchor_y = component_or(anchor, 1, 0);
        if (!layer.contains("shapes") || !layer["shapes"].is_array() ||
            layer["shapes"].size() > 256)
            return std::string("Lottie shape list is missing or exceeds 256 items");
        struct Affine2D { float a{1}, b{0}, c{0}, d{1}, x{0}, y{0}; };
        struct GeometryEntry { const json* item{}; Affine2D transform{}; float opacity{1.0f}; };
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
                                         float& group_opacity) {
            std::vector<float> gp, ga, gs, gr, go;
            bool ap{}, aa{}, as{}, ar{}, ao{};
            if (!read_transform_property(item, "p", gp, ap) || ap ||
                !read_transform_property(item, "a", ga, aa) || aa ||
                !read_transform_property(item, "s", gs, as) || as ||
                !read_transform_property(item, "r", gr, ar) || ar ||
                !read_transform_property(item, "o", go, ao) || ao ||
                gp.size() < 2 || gp.size() > 3 || ga.size() < 2 || ga.size() > 3 ||
                gs.size() < 2 || gs.size() > 3 || gr.size() != 1 || go.size() != 1 ||
                go[0] < 0.0f || go[0] > 100.0f) return false;
            const float sx = gs[0] / 100.0f;
            const float sy = gs[1] / 100.0f;
            const float angle = gr[0] * 0.01745329251994329577f;
            const float cosine = std::cos(angle), sine = std::sin(angle);
            result.a = cosine * sx;
            result.b = sine * sx;
            result.c = -sine * sy;
            result.d = cosine * sy;
            result.x = gp[0] - result.a * ga[0] - result.c * ga[1];
            result.y = gp[1] - result.b * ga[0] - result.d * ga[1];
            group_opacity = go[0] / 100.0f;
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
        std::function<bool(const json&, const Affine2D&, float, std::size_t)> collect_shapes;
        collect_shapes = [&](const json& item, const Affine2D& parent_transform,
                             float parent_opacity,
                             std::size_t depth) {
            if (!item.is_object() || item.contains("exp") || depth > 16 ||
                ++shape_item_count > 256) return false;
            const auto type = item.value("ty", std::string{});
            if (type == "gr") {
                if (!item.contains("it") || !item["it"].is_array()) return false;
                Affine2D local;
                float local_opacity = 1.0f;
                bool found_transform = false;
                for (const auto& child : item["it"]) {
                    if (child.is_object() && child.value("ty", std::string{}) == "tr") {
                        if (found_transform || !group_transform(child, local, local_opacity)) return false;
                        found_transform = true;
                    }
                }
                const auto combined = found_transform
                    ? compose(parent_transform, local) : parent_transform;
                const float combined_opacity = parent_opacity * local_opacity;
                for (const auto& child : item["it"]) {
                    if (child.is_object() && child.value("ty", std::string{}) == "tr") continue;
                    if (!collect_shapes(child, combined, combined_opacity, depth + 1)) return false;
                }
                return true;
            }
            if (type == "sh" || type == "el" || type == "rc") {
                geometry_shapes.push_back({&item, parent_transform, parent_opacity});
                return true;
            }
            if (type == "fl" || type == "gf") { fills.push_back(&item); return true; }
            if (type == "st") { strokes.push_back(&item); return true; }
            if (type == "rp" || type == "rd" || type == "zz" ||
                type == "op" || type == "pb") {
                path_operators.push_back(&item);
                return true;
            }
            if (type == "tr") return identity_transform(item);
            return false;
        };
        const Affine2D identity;
        for (const auto& item : layer["shapes"])
            if (!collect_shapes(item, identity, 1.0f, 0))
                return std::string("Lottie group structure, transform, or shape item is unsupported");
        if (geometry_shapes.empty())
            return std::string("Lottie shape geometry is missing");
        using namespace chronon3d::render_plan;
        for (const auto* raw_operator : path_operators) {
            const auto kind = raw_operator->value("ty", std::string{});
            PathOperatorPlan operation;
            if (kind == "rd") {
                if (!static_scalar_property(*raw_operator, "r", operation.radius) ||
                    operation.radius < 0.0f)
                    return std::string("Lottie round-corners radius must be static and non-negative");
                operation.kind = PathOperatorKindPlan::RoundCorners;
            } else if (kind == "zz") {
                float smooth = 1.0f;
                if (!static_scalar_property(*raw_operator, "s", operation.size) ||
                    !static_scalar_property(*raw_operator, "r", operation.points_per_unit) ||
                    !static_scalar_property(*raw_operator, "pt", smooth) ||
                    operation.size < 0.0f || operation.points_per_unit <= 0.0f ||
                    (smooth != 1.0f && smooth != 2.0f))
                    return std::string("Lottie zig-zag parameters must be static and bounded");
                operation.smooth = smooth == 2.0f;
                operation.kind = PathOperatorKindPlan::ZigZag;
            } else if (kind == "op") {
                float join = 1.0f;
                if (!static_scalar_property(*raw_operator, "a", operation.amount) ||
                    !static_scalar_property(*raw_operator, "ml", operation.miter_limit) ||
                    !static_scalar_property(*raw_operator, "lj", join) ||
                    operation.miter_limit <= 0.0f || join != 1.0f)
                    return std::string("Lottie offset-path parameters require static miter joins");
                operation.kind = PathOperatorKindPlan::OffsetPath;
            } else if (kind == "pb") {
                if (!static_scalar_property(*raw_operator, "a", operation.amount) ||
                    std::abs(operation.amount) > 100.0f)
                    return std::string("Lottie pucker/bloat amount must be static and within [-100, 100]");
                operation.amount /= 100.0f;
                operation.kind = PathOperatorKindPlan::PuckerBloat;
            } else if (kind == "rp") {
                float copies = 0.0f;
                if (!static_scalar_property(*raw_operator, "c", copies) || copies < 1.0f ||
                    copies > 256.0f || std::floor(copies) != copies ||
                    !raw_operator->contains("tr") || !(*raw_operator)["tr"].is_object())
                    return std::string("Lottie repeater copy count/transform is unsupported or exceeds 256");
                operation.kind = PathOperatorKindPlan::Repeater;
                operation.repeater.copies = static_cast<std::uint32_t>(copies);
                auto& transform = (*raw_operator)["tr"];
                std::vector<float> rp, ra, rs, rr;
                bool ap{}, aa{}, as{}, ar{};
                if (!read_transform_property(transform, "p", rp, ap) || ap || rp.size() < 2 || rp.size() > 3 ||
                    !read_transform_property(transform, "a", ra, aa) || aa || ra.size() < 2 || ra.size() > 3 ||
                    !read_transform_property(transform, "s", rs, as) || as || rs.size() < 2 || rs.size() > 3 ||
                    !read_transform_property(transform, "r", rr, ar) || ar || rr.size() != 1)
                    return std::string("Lottie repeater transform must be static position, anchor, scale and rotation");
                operation.repeater.position = {rp[0], rp[1]};
                operation.repeater.anchor = {ra[0], ra[1]};
                operation.repeater.scale = {rs[0] / 100.0f, rs[1] / 100.0f};
                operation.repeater.rotation_degrees = rr[0];
                float start_opacity = 100.0f, end_opacity = 100.0f;
                if ((transform.contains("so") && !static_scalar_property(transform, "so", start_opacity)) ||
                    (transform.contains("eo") && !static_scalar_property(transform, "eo", end_opacity)) ||
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
        std::size_t total_vertices = 0;
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
              if (!static_vec2_property(*path_shape, "p", position) ||
                  !static_vec2_property(*path_shape, "s", size) ||
                  size[0] <= 0.0f || size[1] <= 0.0f)
                  return std::string("Lottie ellipse/rectangle requires static bounded position and size");
              const float half_w = size[0] * 0.5f;
              const float half_h = size[1] * 0.5f;
              float radius = 0.0f;
              if (shape_type == "rc" && path_shape->contains("r") &&
                  !static_scalar_property(*path_shape, "r", radius))
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
          const auto& geometry_data = (*path_shape)["ks"]["k"];
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
        }
        if (fills.size() != 1 && fills.size() != geometry_shapes.size())
            return std::string("Lottie supports one solid fill or one fill per static geometry");
        if (!strokes.empty() && strokes.size() != 1 && strokes.size() != geometry_shapes.size())
            return std::string("Lottie supports one solid stroke or one stroke per static geometry");
        if (fills.empty() && strokes.empty())
            return std::string("Lottie static geometry requires a solid fill or stroke");

        const auto decode_lottie_color = [&](const json& style,
                                             std::array<float, 4>& color) -> bool {
            if (style.value("r", 1) != 1 || !style.contains("c") ||
                !style["c"].is_object() || !style["c"].contains("k")) return false;
            const auto& value = style["c"]["k"];
            if (!value.is_array() || value.size() != 4) return false;
            float opacity = 100.0f;
            if (style.contains("o") && !static_scalar_property(style, "o", opacity)) return false;
            if (opacity < 0.0f || opacity > 100.0f) return false;
            for (std::size_t i = 0; i < 4; ++i) {
                if (!value[i].is_number()) return false;
                color[i] = value[i].get<float>();
                if (!std::isfinite(color[i]) || color[i] < 0.0f || color[i] > 1.0f) return false;
            }
            color[3] *= opacity / 100.0f;
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
            if (!static_vec2_property(style, "s", start) ||
                !static_vec2_property(style, "e", end)) return false;
            const auto kind = style.value("t", 1);
            if (kind != 1 && kind != 2) return false;
            float opacity = 100.0f;
            if (style.contains("o") && !static_scalar_property(style, "o", opacity)) return false;
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
        std::vector<std::optional<std::array<float, 4>>> fill_colors;
        std::vector<std::optional<graphics::GradientDefinition>> fill_gradients;
        for (const auto* fill : fills) {
            if (fill->value("ty", std::string{}) == "gf") {
                graphics::GradientDefinition gradient;
                if (!decode_lottie_gradient(*fill, gradient))
                    return std::string("Lottie gradient fill is malformed or unsupported");
                fill_colors.push_back(std::nullopt);
                fill_gradients.push_back(std::move(gradient));
            } else {
                std::array<float, 4> color{};
                if (!decode_lottie_color(*fill, color))
                    return std::string("Lottie fill must be static finite RGBA in RGB color space");
                fill_colors.push_back(color);
                fill_gradients.push_back(std::nullopt);
            }
        }
        std::vector<std::optional<StrokeStyle>> stroke_styles;
        for (const auto* stroke : strokes) {
            std::array<float, 4> color{};
            float width = 0.0f;
            if (!decode_lottie_color(*stroke, color) ||
                !static_scalar_property(*stroke, "w", width) || width < 0.0f ||
                stroke->value("lc", 1) != 1 || stroke->value("lj", 1) != 1 ||
                stroke->value("ml", 4.0f) != 4.0f)
                return std::string("Lottie stroke supports bounded static color/opacity/width with default cap/join");
            StrokeStyle style;
            style.width = width;
            style.solid_color = color;
            if (stroke->contains("d")) {
                if (!(*stroke)["d"].is_array() || (*stroke)["d"].size() > 128)
                    return std::string("Lottie stroke dash list is malformed or exceeds 128 entries");
                for (const auto& dash : (*stroke)["d"]) {
                    if (!dash.is_object() || !dash.contains("n") || !dash.contains("v"))
                        return std::string("Lottie stroke dash entry is malformed");
                    float value = 0.0f;
                    if (!static_scalar_property(dash, "v", value) || value < 0.0f)
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
            if (!fill_colors.empty()) {
                const auto fill_index = fills.size() == 1 ? 0 : index;
                if (fill_colors[fill_index]) {
                    imported.fill_color = *fill_colors[fill_index];
                    (*imported.fill_color)[3] *= geometry_shapes[index].opacity;
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
                imported.stroke = *stroke_styles[strokes.size() == 1 ? 0 : index];
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
            }
            shape.imported_subpaths.push_back(std::move(imported));
        }
        shape.path.clear();
        if (!shape.operators.empty()) {
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
            shape.operators.clear();
        }
        return std::nullopt;
    } catch (const std::exception& error) {
        return std::string("malformed Lottie JSON: ") + error.what();
    }
}

}  // namespace chronon3d::render_plan::detail
