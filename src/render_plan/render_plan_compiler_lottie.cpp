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

bool lottie_point(const json& value, std::array<float, 2>& out) {
            if (!value.is_array() || value.size() != 2) return false;
            for (std::size_t i = 0; i < 2; ++i) {
                if (!value[i].is_number()) return false;
                const float v = value[i].get<float>();
                if (!std::isfinite(v) || std::abs(v) > 100000.0f) return false;
                out[i] = std::round(v * 10000.0f) / 10000.0f;
            }
            return true;
}

struct Affine2D { float a{1}, b{0}, c{0}, d{1}, x{0}, y{0}; };
using GroupTransformPlan = ImportedSubpathPlan::GroupTransformPlan;
struct GeometryEntry {
    const json* item{};
    Affine2D transform{};
    float opacity{1.0f};
    std::optional<GroupTransformPlan> group_animation;
};

struct LottieCollectedShapes {
    std::vector<GeometryEntry> geometry_shapes;
    std::vector<const json*> fills;
    std::vector<const json*> strokes;
    std::vector<const json*> path_operators;
};

// Recursively flattens the Lottie group tree into geometry entries and
// paint/operator pointers, rejecting unsupported items at the boundary.
std::optional<std::string> collect_lottie_shapes(
    const json& layer, LottieCollectedShapes& out) {
    auto& geometry_shapes = out.geometry_shapes;
    auto& fills = out.fills;
    auto& strokes = out.strokes;
    auto& path_operators = out.path_operators;
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
    return std::nullopt;
}

// Applies each geometry entry's accumulated group transform to shape.path and
// bakes the layer pivot.  The morph operator it may produce is returned for
// the later subpath pass, which attaches it to the first subpath.
std::optional<std::string> materialize_lottie_geometry(
    ShapeContentPlan& shape, const std::vector<GeometryEntry>& geometry_shapes,
    const std::vector<const json*>& path_operators,
    float anchor_x, float anchor_y,
    std::optional<PathOperatorPlan>& imported_morph,
    std::vector<std::size_t>& geometry_command_starts) {
        std::size_t total_vertices = 0;
        geometry_command_starts.clear();
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
            if (!lottie_point(geometry_data["v"][i], vertex) || !lottie_point(geometry_data["o"][i], outgoing) ||
                !lottie_point(geometry_data["i"][i], incoming))
                return std::string("Lottie path contains non-finite or out-of-bounds coordinates");
            if (i == 0) {
                if (shape.path.size() >= 65536)
                    return std::string("Lottie total path command limit exceeded");
                shape.path.push_back({ShapePathCommandTypePlan::MoveTo, vertex});
                continue;
            }
            const auto& prior = geometry_data["v"][i - 1];
            std::array<float, 2> prior_v{}, prior_o{};
            if (!lottie_point(prior, prior_v) || !lottie_point(geometry_data["o"][i - 1], prior_o))
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
                  if (!lottie_point(target_geometry["v"][i], vertex) || !lottie_point(target_geometry["i"][i], incoming) ||
                      !lottie_point(target_geometry["o"][i], outgoing)) return false;
                  if (i == 0) { output.push_back({ShapePathCommandTypePlan::MoveTo, vertex}); continue; }
                  std::array<float, 2> prior{}, prior_out{};
                  if (!lottie_point(target_geometry["v"][i - 1], prior) || !lottie_point(target_geometry["o"][i - 1], prior_out)) return false;
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
    return std::nullopt;
}


// Splits the flattened path into one imported subpath per static geometry and
// folds each paint/transform into it.
std::optional<std::string> assemble_lottie_subpaths(
    ShapeContentPlan& shape,
    const std::vector<GeometryEntry>& geometry_shapes,
    const std::vector<std::size_t>& geometry_command_starts,
    const std::vector<const json*>& fills,
    const std::vector<const json*>& strokes,
    const LottiePaintSet& paints,
    const std::optional<PathOperatorPlan>& imported_morph,
    float anchor_x, float anchor_y) {
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
            if (!paints.fill_paints.empty()) {
                const auto fill_index = fills.size() == 1 ? 0 : index;
                if (paints.fill_paints[fill_index]) {
                    auto& paint = *paints.fill_paints[fill_index];
                    imported.fill_color = paint.color;
                    (*imported.fill_color)[3] *= geometry_shapes[index].opacity;
                    imported.fill_color_animation = paint.color_animation;
                    if (imported.fill_color_animation) {
                        for (auto& key : imported.fill_color_animation->keyframes)
                            key.value[3] *= geometry_shapes[index].opacity;
                    }
                    imported.fill_opacity_animation = paint.opacity_animation;
                }
                if (paints.fill_gradients[fill_index]) {
                    imported.fill_gradient = *paints.fill_gradients[fill_index];
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
            if (!paints.stroke_styles.empty()) {
                const auto stroke_index = strokes.size() == 1 ? 0 : index;
                imported.stroke = *paints.stroke_styles[stroke_index];
                imported.stroke_color_animation = paints.stroke_color_tracks[stroke_index];
                imported.stroke_opacity_animation = paints.stroke_opacity_tracks[stroke_index];
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
                imported.stroke_width_animation = paints.stroke_width_tracks[stroke_index];
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
    return std::nullopt;
}

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
            !doc.contains("layers") || !doc["layers"].is_array())
            return std::string("Lottie requires a version field and a layer array");
        if (doc["layers"].size() != 1)
            return std::string("Lottie multi-layer assets are unsupported: asset expansion currently emits one RenderPlan layer and cannot preserve per-layer timing, mattes, or blend order");
        const auto& layer = doc["layers"][0];
        if (layer.value("ty", -1) == 0)
            return std::string("Lottie precompositions are unsupported: RenderPlan has no nested asset composition or local frame-remapping contract");
        if (layer.value("ty", -1) != 4 || layer.value("ddd", 0) != 0 || layer.contains("ef") ||
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
        LottieCollectedShapes collected;
        if (auto collect_error = collect_lottie_shapes(layer, collected))
            return *collect_error;
        const auto& geometry_shapes = collected.geometry_shapes;
        const auto& fills = collected.fills;
        const auto& strokes = collected.strokes;
        const auto& path_operators = collected.path_operators;
        std::optional<PathOperatorPlan> imported_morph;
        if (auto operator_error = decode_lottie_path_operators(path_operators, shape))
            return *operator_error;
        if (auto mask_error = decode_lottie_masks(layer, layer_plan, anchor_x, anchor_y))
            return *mask_error;
        std::vector<std::size_t> geometry_command_starts;
        if (auto geometry_error = materialize_lottie_geometry(
                shape, geometry_shapes, path_operators, anchor_x, anchor_y, imported_morph,
                geometry_command_starts))
            return *geometry_error;
        LottiePaintSet paints;
        if (auto paint_error = decode_lottie_paints(fills, strokes, geometry_shapes.size(), paints))
            return *paint_error;
        if (auto subpath_error = assemble_lottie_subpaths(
                shape, geometry_shapes, geometry_command_starts, fills, strokes, paints,
                imported_morph, anchor_x, anchor_y))
            return *subpath_error;
        shape.path.clear();
        const bool has_trim_operator = std::any_of(shape.operators.begin(), shape.operators.end(),
            [](const PathOperatorPlan& operation) {
                return operation.kind == PathOperatorKindPlan::Trim;
            });
        if (has_trim_operator && shape.operators.size() != 1)
            return std::string("Lottie Trim Paths currently requires a single isolated path operator");
        const bool has_animated_operator = std::any_of(shape.operators.begin(), shape.operators.end(),
            [](const PathOperatorPlan& operation) {
                return operation.repeater_animation.has_value() ||
                    operation.morph_animation.has_value();
            });
        // Animated operators must survive import: the canonical per-frame
        // shape lowering samples their tracks on the original subpaths.
        if (!shape.operators.empty() && !has_trim_operator && !has_animated_operator) {
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
