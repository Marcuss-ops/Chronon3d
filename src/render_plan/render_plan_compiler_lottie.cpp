#include "render_plan_compiler_detail.hpp"
#include <chronon3d/render_plan/color_utils.hpp>

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
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d::render_plan::detail {
namespace {
using json = nlohmann::json;
}  // namespace

std::optional<std::string> expand_static_lottie(
    chronon3d::render_plan::ShapeContentPlan& shape,
    const std::filesystem::path& path) {
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
        const auto identity_transform = [&](const json& transform) {
            return property_is(transform, "a", {0.0f, 0.0f}) &&
                   property_is(transform, "p", {0.0f, 0.0f, 0.0f}) &&
                   property_is(transform, "s", {100.0f, 100.0f, 100.0f}) &&
                   property_is(transform, "r", {0.0f}) &&
                   property_is(transform, "o", {100.0f});
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
        if (!layer.contains("ks") || !layer["ks"].is_object() ||
            !identity_transform(layer["ks"]))
            return std::string("animated or non-identity Lottie layer transforms are unsupported by this lowering");
        if (!layer.contains("shapes") || !layer["shapes"].is_array() ||
            layer["shapes"].size() > 256)
            return std::string("Lottie shape list is missing or exceeds 256 items");
        std::vector<const json*> geometry_shapes;
        std::vector<const json*> fills;
        std::vector<const json*> strokes;
        for (const auto& item : layer["shapes"]) {
            if (!item.is_object() || item.contains("exp"))
                return std::string("malformed Lottie shape item or expression");
            const auto type = item.value("ty", std::string{});
            if (type == "sh" || type == "el" || type == "rc") {
                geometry_shapes.push_back(&item);
            } else if (type == "fl") {
                fills.push_back(&item);
            } else if (type == "st") {
                strokes.push_back(&item);
            } else if (type == "tr") {
                if (!identity_transform(item))
                    return std::string("animated or non-identity Lottie shape transforms are unsupported by this lowering");
                continue;
            } else {
                return std::string("Lottie supports only static path, ellipse, rectangle, fill, and transform shapes in this lowering");
            }
        }
        if (geometry_shapes.empty())
            return std::string("Lottie shape geometry is missing");
        using namespace chronon3d::render_plan;
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
        for (const auto* path_shape : geometry_shapes) {
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
              continue;
          }
          if (!path_shape->contains("ks") || !(*path_shape)["ks"].contains("k"))
              return std::string("Lottie path geometry is missing");
          const auto& geometry = (*path_shape)["ks"]["k"];
          if (!geometry.is_object() || !geometry.contains("v") || !geometry.contains("i") ||
              !geometry.contains("o") || !geometry["v"].is_array() ||
              geometry["v"].size() < 2 || geometry["v"].size() > 65536 ||
              !geometry["i"].is_array() || !geometry["o"].is_array() ||
              geometry["i"].size() != geometry["v"].size() ||
              geometry["o"].size() != geometry["v"].size() ||
              geometry["v"].size() > 65536 - total_vertices)
              return std::string("Lottie static path vertex and tangent arrays are invalid or over limit");
          total_vertices += geometry["v"].size();
          for (std::size_t i = 0; i < geometry["v"].size(); ++i) {
            std::array<float, 2> vertex{}, outgoing{}, incoming{};
            if (!point(geometry["v"][i], vertex) || !point(geometry["o"][i], outgoing) ||
                !point(geometry["i"][i], incoming))
                return std::string("Lottie path contains non-finite or out-of-bounds coordinates");
            if (i == 0) {
                if (shape.path.size() >= 65536)
                    return std::string("Lottie total path command limit exceeded");
                shape.path.push_back({ShapePathCommandTypePlan::MoveTo, vertex});
                continue;
            }
            const auto& prior = geometry["v"][i - 1];
            std::array<float, 2> prior_v{}, prior_o{};
            if (!point(prior, prior_v) || !point(geometry["o"][i - 1], prior_o))
                return std::string("Lottie path contains invalid outgoing tangent");
            const std::array<float, 2> c1{std::round((prior_v[0] + prior_o[0]) * 10000.0f) / 10000.0f,
                                            std::round((prior_v[1] + prior_o[1]) * 10000.0f) / 10000.0f};
            const std::array<float, 2> c2{std::round((vertex[0] + incoming[0]) * 10000.0f) / 10000.0f,
                                            std::round((vertex[1] + incoming[1]) * 10000.0f) / 10000.0f};
            if (shape.path.size() >= 65536)
                return std::string("Lottie total path command limit exceeded");
            shape.path.push_back({ShapePathCommandTypePlan::CubicTo, vertex, c1, c2});
          }
          if (geometry.value("c", false)) {
              if (shape.path.size() >= 65536)
                  return std::string("Lottie total path command limit exceeded");
              shape.path.push_back({ShapePathCommandTypePlan::Close});
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
        std::vector<std::optional<std::array<float, 4>>> fill_colors;
        for (const auto* fill : fills) {
            std::array<float, 4> color{};
            if (!decode_lottie_color(*fill, color))
                return std::string("Lottie fill must be static finite RGBA in RGB color space");
            fill_colors.push_back(color);
        }
        std::vector<std::optional<StrokeStyle>> stroke_styles;
        for (const auto* stroke : strokes) {
            std::array<float, 4> color{};
            float width = 0.0f;
            if (!decode_lottie_color(*stroke, color) ||
                !static_scalar_property(*stroke, "w", width) || width < 0.0f ||
                stroke->value("lc", 1) != 1 || stroke->value("lj", 1) != 1 ||
                stroke->value("ml", 4.0f) != 4.0f ||
                (stroke->contains("d") && !(*stroke)["d"].empty()))
                return std::string("Lottie stroke supports bounded static color/opacity/width with default cap/join and no dashes");
            StrokeStyle style;
            style.width = width;
            style.solid_color = color;
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
            if (!fill_colors.empty())
                imported.fill_color = *fill_colors[fills.size() == 1 ? 0 : index];
            if (!stroke_styles.empty())
                imported.stroke = *stroke_styles[strokes.size() == 1 ? 0 : index];
            shape.imported_subpaths.push_back(std::move(imported));
        }
        shape.path.clear();
        return std::nullopt;
    } catch (const std::exception& error) {
        return std::string("malformed Lottie JSON: ") + error.what();
    }
}

}  // namespace chronon3d::render_plan::detail
