#include "svg_pattern_detail.hpp"
#include "svg_importer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d::assets::svg_gradient_detail {

using svg_loader_detail::parse_svg_color;
using svg_loader_detail::parse_svg_length;
using svg_loader_detail::style_properties;
using svg_loader_detail::trim;

namespace {

constexpr float kObjectBBoxRaster = 256.0f;
constexpr std::size_t kMaxPatternShapes = 32;
constexpr std::size_t kMaxPatternCommands = 4096;
constexpr std::size_t kMaxPatternDepth = 8;
constexpr std::size_t kMaxPatternHrefDepth = 8;

using Contour = std::vector<Vec2>;

struct TileBuffer {
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::vector<std::uint8_t> rgba;
};

struct PatternPaint {
    bool none{false};
    Color color{Color::black()};
    const SvgGradientSpec* gradient{nullptr};
};

struct TileStyle {
    PatternPaint fill;
    PatternPaint stroke{true, Color::black(), nullptr};
    float fill_opacity{1.0f};
    float stroke_opacity{1.0f};
    float stroke_width{1.0f};
    bool even_odd{false};
    bool visible{true};
    Color current_color{Color::black()};
    Affine2 transform{1.0f};
};

struct TileRenderContext {
    const SvgGradientMap& gradients;
    std::size_t shape_count{0};
    std::size_t command_count{0};
    std::string error;
};

struct ResolvedPattern {
    const boost::property_tree::ptree* content{nullptr};
    std::map<std::string, std::string> attributes;
    Color inherited_color{Color::black()};
};

[[nodiscard]] bool has_element_children(const boost::property_tree::ptree& node) {
    for (const auto& [name, child] : node) {
        (void)child;
        if (name == "<xmlattr>" || name == "<xmltext>" || name == "<xmlcomment>")
            continue;
        return true;
    }
    return false;
}

[[nodiscard]] bool read_local_href(const boost::property_tree::ptree& node,
                                   std::optional<std::string>& id,
                                   std::string& error) {
    auto href = node.get_optional<std::string>("<xmlattr>.href");
    if (!href) href = node.get_optional<std::string>("<xmlattr>.xlink:href");
    if (!href) href = node.get_optional<std::string>(
        "<xmlattr>.{http://www.w3.org/1999/xlink}href");
    if (!href) return true;
    const auto value = trim(*href);
    if (value.size() < 2 || value.front() != '#' ||
        value.find_first_of(" \t\r\n#", 1) != std::string::npos) {
        error = "SVG pattern href must be a local #id reference";
        return false;
    }
    id = value.substr(1);
    return true;
}

[[nodiscard]] bool resolve_pattern_href(
    const std::string& id, const SvgPatternMap& patterns,
    ResolvedPattern& resolved, std::string& error) {
    std::set<std::string> visited;
    std::string current = id;
    for (std::size_t depth = 0; depth < kMaxPatternHrefDepth; ++depth) {
        if (!visited.insert(current).second) {
            error = "SVG pattern href contains a cycle at #" + current;
            return false;
        }
        const auto found = patterns.find(current);
        if (found == patterns.end()) {
            error = "SVG pattern href references an unknown pattern: #" + current;
            return false;
        }
        const auto& node = *found->second.node;
        if (depth == 0) resolved.inherited_color = found->second.inherited_color;
        if (node.get_optional<std::string>("<xmlattr>.viewBox") ||
            node.get_optional<std::string>("<xmlattr>.preserveAspectRatio")) {
            error = "SVG text patterns do not support viewBox or preserveAspectRatio";
            return false;
        }
        if (!style_properties(node).empty()) {
            error = "SVG text pattern-level paint/style inheritance is not supported";
            return false;
        }
        for (const auto* key : {"width", "height", "x", "y", "patternUnits",
                                "patternContentUnits", "patternTransform"}) {
            if (resolved.attributes.contains(key)) continue;
            if (const auto value = node.get_optional<std::string>(
                    std::string("<xmlattr>.") + key))
                resolved.attributes.emplace(key, *value);
        }
        if (!resolved.content && has_element_children(node))
            resolved.content = &node;
        std::optional<std::string> href;
        if (!read_local_href(node, href, error)) return false;
        if (!href) break;
        current = *href;
    }
    if (resolved.content == nullptr) {
        error = "SVG pattern href chain has no tile content";
        return false;
    }
    return true;
}

[[nodiscard]] std::string pattern_attribute(
    const ResolvedPattern& pattern, const char* key, const char* fallback) {
    if (const auto found = pattern.attributes.find(key);
        found != pattern.attributes.end())
        return found->second;
    return fallback;
}

[[nodiscard]] bool reject_percent_user_unit(
    const std::string& value, const char* key, std::string& error) {
    if (trim(value).find('%') != std::string::npos) {
        error = std::string("SVG text pattern ") + key +
            " does not support percentages with userSpaceOnUse units";
        return false;
    }
    return true;
}

[[nodiscard]] bool parse_user_dimension(
    const std::string& text, std::uint32_t& output, const char* key,
    std::string& error) {
    if (!reject_percent_user_unit(text, key, error)) return false;
    float value = 0.0f;
    if (!parse_svg_scalar(text, 0.0f, value) || value < 1.0f ||
        value > 256.0f || std::floor(value) != value) {
        error = std::string("SVG text pattern ") + key +
            " must be an integer user-space pixel value in [1,256]";
        return false;
    }
    output = static_cast<std::uint32_t>(value);
    return true;
}

[[nodiscard]] bool parse_object_dimension(
    const std::string& text, float& fraction, std::uint32_t& pixels,
    const char* key, std::string& error) {
    float value = 0.0f;
    if (!parse_svg_scalar(text, 0.0f, value) || !(value > 0.0f) ||
        value > 1.0f) {
        error = std::string("SVG text pattern objectBoundingBox ") + key +
            " must be a positive fraction in (0,1]";
        return false;
    }
    fraction = value;
    const float scaled = value * kObjectBBoxRaster;
    if (scaled < 1.0f) {
        error = std::string("SVG text pattern objectBoundingBox ") + key +
            " is below the bounded raster resolution";
        return false;
    }
    pixels = static_cast<std::uint32_t>(
        std::clamp(std::lround(scaled), 1L, 256L));
    return true;
}

[[nodiscard]] bool resolve_paint(
    const std::string& source, const Color& current_color,
    const SvgGradientMap& gradients, PatternPaint& output,
    const char* property, std::string& error) {
    const auto value = trim(source);
    if (value == "none") {
        output.none = true;
        output.gradient = nullptr;
        return true;
    }
    std::string gradient_id;
    if (parse_svg_gradient_reference(value, gradient_id)) {
        const auto found = gradients.find(gradient_id);
        if (found == gradients.end()) {
            error = std::string("SVG text pattern tile ") + property +
                " references an unknown paint";
            return false;
        }
        if (found->second.pattern_tile) {
            error = "SVG text pattern tile content does not support nested pattern paint";
            return false;
        }
        if (!found->second.animations.empty()) {
            error = "SVG text pattern tile paint does not support animated gradients";
            return false;
        }
        output.none = false;
        output.gradient = &found->second;
        output.color = Color::black();
        return true;
    }
    if (value.starts_with("url(")) {
        error = std::string("SVG text pattern tile ") + property +
            " requires a local gradient reference";
        return false;
    }
    const auto color = parse_svg_color(value, current_color);
    if (!color) {
        error = std::string("SVG text pattern tile ") + property +
            " color is invalid or unsupported";
        return false;
    }
    output.none = false;
    output.gradient = nullptr;
    output.color = *color;
    return true;
}

[[nodiscard]] bool apply_tile_style(
    const boost::property_tree::ptree& node, TileStyle& style,
    const SvgGradientMap& gradients, std::string& error) {
    const auto properties = style_properties(node);
    for (const auto& [property, value] : properties) {
        (void)value;
        if (property != "fill" && property != "stroke" &&
            property != "fill-opacity" && property != "stroke-opacity" &&
            property != "opacity" && property != "fill-rule" &&
            property != "color" && property != "stroke-width" &&
            property != "stroke-linecap" && property != "stroke-linejoin" &&
            property != "stroke-miterlimit" && property != "display" &&
            property != "visibility") {
            error = "SVG text pattern tile property is unsupported: " + property;
            return false;
        }
    }
    if (const auto found = properties.find("display");
        found != properties.end()) {
        auto value = trim(found->second);
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (value != "none" && value != "inline" && value != "block") {
            error = "SVG text pattern tile display supports none, inline, or block";
            return false;
        }
        if (value == "none") style.visible = false;
    }
    if (const auto found = properties.find("visibility");
        found != properties.end()) {
        const auto value = trim(found->second);
        if (value != "visible" && value != "hidden" && value != "collapse") {
            error = "SVG text pattern tile visibility supports visible, hidden, or collapse";
            return false;
        }
        if (value == "hidden" || value == "collapse") style.visible = false;
    }
    if (const auto found = properties.find("color");
        found != properties.end()) {
        const auto color = parse_svg_color(found->second, style.current_color);
        if (!color) {
            error = "SVG text pattern tile color is invalid or unsupported";
            return false;
        }
        style.current_color = *color;
    }
    if (const auto found = properties.find("fill");
        found != properties.end() &&
        !resolve_paint(found->second, style.current_color, gradients,
                       style.fill, "fill", error))
        return false;
    if (const auto found = properties.find("stroke");
        found != properties.end() &&
        !resolve_paint(found->second, style.current_color, gradients,
                       style.stroke, "stroke", error))
        return false;
    for (const auto& [key, slot] : {
             std::pair{"fill-opacity", &style.fill_opacity},
             std::pair{"stroke-opacity", &style.stroke_opacity}}) {
        if (const auto found = properties.find(key); found != properties.end()) {
            if (!parse_svg_scalar(found->second, 1.0f, *slot) ||
                *slot < 0.0f || *slot > 1.0f) {
                error = std::string("SVG text pattern tile ") + key +
                    " must be a finite number in [0,1]";
                return false;
            }
        }
    }
    if (const auto found = properties.find("fill-rule");
        found != properties.end()) {
        const auto value = trim(found->second);
        if (value != "evenodd" && value != "nonzero") {
            error = "SVG text pattern tile fill-rule must be evenodd or nonzero";
            return false;
        }
        style.even_odd = value == "evenodd";
    }
    if (const auto found = properties.find("stroke-width");
        found != properties.end()) {
        const auto width = parse_svg_length(trim(found->second));
        if (!width || *width < 0.0f || *width > 10000.0f) {
            error = "SVG text pattern tile stroke-width must be finite and within [0,10000]";
            return false;
        }
        style.stroke_width = *width;
    }
    if (const auto found = properties.find("stroke-linecap");
        found != properties.end()) {
        const auto value = trim(found->second);
        if (value != "butt" && value != "round" && value != "square") {
            error = "SVG text pattern tile stroke-linecap must be butt, round, or square";
            return false;
        }
    }
    if (const auto found = properties.find("stroke-linejoin");
        found != properties.end()) {
        const auto value = trim(found->second);
        if (value != "miter" && value != "round" && value != "bevel") {
            error = "SVG text pattern tile stroke-linejoin must be miter, round, or bevel";
            return false;
        }
    }
    if (const auto found = properties.find("stroke-miterlimit");
        found != properties.end()) {
        float limit = 0.0f;
        if (!parse_svg_scalar(found->second, 4.0f, limit) ||
            limit < 1.0f || limit > 1000.0f) {
            error = "SVG text pattern tile stroke-miterlimit must be within [1,1000]";
            return false;
        }
    }
    return true;
}

void apply_node_transform(
    const boost::property_tree::ptree& node, TileStyle& style) {
    if (const auto transform = node.get_optional<std::string>(
            "<xmlattr>.transform"))
        style.transform = style.transform * parse_transform(*transform);
}

[[nodiscard]] float node_opacity(const boost::property_tree::ptree& node) {
    const auto properties = style_properties(node);
    const auto found = properties.find("opacity");
    if (found == properties.end()) return 1.0f;
    float opacity = 1.0f;
    if (!parse_svg_scalar(found->second, 1.0f, opacity) ||
        opacity < 0.0f || opacity > 1.0f)
        return -1.0f;
    return opacity;
}

void blend_pixel(TileBuffer& target, std::size_t pixel,
                 const Color& color, float alpha) {
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    if (alpha <= 0.0f) return;
    const std::size_t at = pixel * 4;
    const float destination_alpha =
        static_cast<float>(target.rgba[at + 3]) / 255.0f;
    const float out_alpha = alpha + destination_alpha * (1.0f - alpha);
    if (out_alpha <= 0.0f) return;
    const auto channel = [&](float source, std::uint8_t destination) {
        const float value =
            (source * alpha + static_cast<float>(destination) / 255.0f *
             destination_alpha * (1.0f - alpha)) / out_alpha;
        return static_cast<std::uint8_t>(
            std::clamp(std::lround(value * 255.0f), 0L, 255L));
    };
    target.rgba[at + 0] = channel(color.r, target.rgba[at + 0]);
    target.rgba[at + 1] = channel(color.g, target.rgba[at + 1]);
    target.rgba[at + 2] = channel(color.b, target.rgba[at + 2]);
    target.rgba[at + 3] = static_cast<std::uint8_t>(
        std::clamp(std::lround(out_alpha * 255.0f), 0L, 255L));
}

void composite_buffer(TileBuffer& target, const TileBuffer& source,
                      float opacity) {
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    if (opacity <= 0.0f) return;
    for (std::size_t pixel = 0;
         pixel < static_cast<std::size_t>(target.width) * target.height;
         ++pixel) {
        const auto at = pixel * 4;
        if (source.rgba[at + 3] == 0) continue;
        blend_pixel(target, pixel,
            Color{static_cast<float>(source.rgba[at + 0]) / 255.0f,
                  static_cast<float>(source.rgba[at + 1]) / 255.0f,
                  static_cast<float>(source.rgba[at + 2]) / 255.0f,
                  1.0f},
            static_cast<float>(source.rgba[at + 3]) / 255.0f * opacity);
    }
}

[[nodiscard]] std::vector<Contour> flatten_contours(const PathShape& path) {
    std::vector<Contour> contours;
    Vec2 current_point{0.0f, 0.0f};
    Vec2 contour_start{0.0f, 0.0f};
    bool have_contour = false;
    const auto append = [&](Vec2 point) {
        if (!have_contour) {
            contours.emplace_back();
            contour_start = point;
            contours.back().push_back(point);
            have_contour = true;
        } else {
            contours.back().push_back(point);
        }
        current_point = point;
    };
    for (const auto& command : path.commands) {
        if (command.type == PathCommandType::MoveTo) {
            contours.emplace_back();
            contours.back().push_back(command.p0);
            contour_start = current_point = command.p0;
            have_contour = true;
        } else if (command.type == PathCommandType::LineTo) {
            append(command.p0);
        } else if (command.type == PathCommandType::QuadraticTo) {
            const Vec2 start = current_point;
            for (int step = 1; step <= 12; ++step) {
                const float t = static_cast<float>(step) / 12.0f;
                const float u = 1.0f - t;
                append(start * (u * u) + command.p0 * (2.0f * u * t) +
                       command.p1 * (t * t));
            }
        } else if (command.type == PathCommandType::CubicTo) {
            const Vec2 start = current_point;
            for (int step = 1; step <= 16; ++step) {
                const float t = static_cast<float>(step) / 16.0f;
                const float u = 1.0f - t;
                append(start * (u * u * u) + command.p0 * (3.0f * u * u * t) +
                       command.p1 * (3.0f * u * t * t) +
                       command.p2 * (t * t * t));
            }
        } else if (command.type == PathCommandType::Close && have_contour) {
            if (contours.back().back().x != contour_start.x ||
                contours.back().back().y != contour_start.y)
                contours.back().push_back(contour_start);
            current_point = contour_start;
            have_contour = false;
        }
    }
    for (auto& contour : contours) {
        if (contour.size() >= 3 &&
            (contour.front().x != contour.back().x ||
             contour.front().y != contour.back().y))
            contour.push_back(contour.front());
    }
    return contours;
}

[[nodiscard]] Color sample_gradient(
    const SvgGradientSpec& gradient, Vec2 point, Vec2 bounds_min,
    Vec2 bounds_size) {
    if (!gradient.user_space) {
        const float width = bounds_size.x > 0.0f ? bounds_size.x : 1.0f;
        const float height = bounds_size.y > 0.0f ? bounds_size.y : 1.0f;
        point = Vec2{(point.x - bounds_min.x) / width,
                     (point.y - bounds_min.y) / height};
    }
    const Affine2 inverse_transform = glm::inverse(gradient.transform);
    point = transform_point(inverse_transform, point);
    float t = 0.0f;
    if (gradient.radial) {
        const float radius = gradient.radius > 0.0f ? gradient.radius : 1.0f;
        t = glm::length(point - gradient.p0) / radius;
    } else {
        const Vec2 direction = gradient.p1 - gradient.p0;
        const float length_squared = glm::dot(direction, direction);
        if (length_squared <= 1.0e-12f) return gradient.stops.front().color;
        t = glm::dot(point - gradient.p0, direction) / length_squared;
    }
    switch (gradient.spread) {
    case graphics::GradientSpread::Repeat:
        t -= std::floor(t);
        break;
    case graphics::GradientSpread::Reflect:
        t = std::fmod(t, 2.0f);
        if (t < 0.0f) t += 2.0f;
        if (t > 1.0f) t = 2.0f - t;
        break;
    case graphics::GradientSpread::Pad:
    default:
        t = std::clamp(t, 0.0f, 1.0f);
        break;
    }
    const auto& stops = gradient.stops;
    if (t <= stops.front().position) return stops.front().color;
    if (t >= stops.back().position) return stops.back().color;
    for (std::size_t index = 1; index < stops.size(); ++index) {
        if (t > stops[index].position) continue;
        const float span = stops[index].position - stops[index - 1].position;
        const float mix = span > 0.0f
            ? (t - stops[index - 1].position) / span : 0.0f;
        const auto& from = stops[index - 1].color;
        const auto& to = stops[index].color;
        return Color{from.r + (to.r - from.r) * mix,
                     from.g + (to.g - from.g) * mix,
                     from.b + (to.b - from.b) * mix,
                     from.a + (to.a - from.a) * mix};
    }
    return stops.back().color;
}

[[nodiscard]] Color paint_color(
    const PatternPaint& paint, const SvgGradientSpec* source_gradient,
    Vec2 point, Vec2 bounds_min, Vec2 bounds_size) {
    if (!source_gradient) return paint.color;
    return sample_gradient(*source_gradient, point, bounds_min, bounds_size);
}

struct Segment {
    Vec2 from;
    Vec2 to;
    Vec2 minimum;
    Vec2 maximum;
};

[[nodiscard]] float distance_squared_to_segment(
    Vec2 point, const Segment& segment) {
    const Vec2 direction = segment.to - segment.from;
    const float length_squared = glm::dot(direction, direction);
    if (length_squared <= 1.0e-12f)
        return glm::dot(point - segment.from, point - segment.from);
    float t = glm::dot(point - segment.from, direction) / length_squared;
    t = std::clamp(t, 0.0f, 1.0f);
    const Vec2 closest = segment.from + direction * t;
    return glm::dot(point - closest, point - closest);
}

void rasterize_shape_impl(
    TileBuffer& target, const PathShape& path, const TileStyle& style,
    const SvgGradientMap& gradients) {
    (void)gradients;
    auto contours = flatten_contours(path);
    for (auto& contour : contours) {
        for (auto& point : contour)
            point = transform_point(style.transform, point);
    }
    float min_x = static_cast<float>(target.width);
    float min_y = static_cast<float>(target.height);
    float max_x = 0.0f;
    float max_y = 0.0f;
    bool first = true;
    for (const auto& contour : contours) for (const auto point : contour) {
        min_x = std::min(min_x, point.x);
        min_y = std::min(min_y, point.y);
        max_x = std::max(max_x, point.x);
        max_y = std::max(max_y, point.y);
        first = false;
    }
    if (first) return;
    const int x0 = std::clamp(static_cast<int>(std::floor(min_x)), 0,
                              static_cast<int>(target.width));
    const int y0 = std::clamp(static_cast<int>(std::floor(min_y)), 0,
                              static_cast<int>(target.height));
    const int x1 = std::clamp(static_cast<int>(std::ceil(max_x)), 0,
                              static_cast<int>(target.width));
    const int y1 = std::clamp(static_cast<int>(std::ceil(max_y)), 0,
                              static_cast<int>(target.height));
    const std::size_t pixel_count =
        static_cast<std::size_t>(target.width) * target.height;
    std::vector<std::uint8_t> fill_coverage(pixel_count, 0);
    std::vector<std::uint8_t> stroke_coverage(pixel_count, 0);
    constexpr int samples = 4;
    constexpr unsigned sample_count = samples * samples;
    if (!style.fill.none && x1 > x0 && y1 > y0) {
        for (int sub_y = y0 * samples; sub_y < y1 * samples; ++sub_y) {
            const float sample_y = (sub_y + 0.5f) / samples;
            std::vector<std::pair<float, int>> crossings;
            for (const auto& contour : contours) {
                if (contour.size() < 4) continue;
                for (std::size_t edge = 1; edge < contour.size(); ++edge) {
                    const auto& from = contour[edge - 1];
                    const auto& to = contour[edge];
                    if ((from.y > sample_y) == (to.y > sample_y)) continue;
                    const float cross_x = from.x + (sample_y - from.y) *
                        (to.x - from.x) / (to.y - from.y);
                    crossings.emplace_back(cross_x, to.y > from.y ? 1 : -1);
                }
            }
            std::sort(crossings.begin(), crossings.end(),
                [](const auto& left, const auto& right) {
                    return left.first < right.first;
                });
            std::size_t crossing = 0;
            int winding = 0;
            bool parity = false;
            for (int sub_x = x0 * samples; sub_x < x1 * samples; ++sub_x) {
                const float sample_x = (sub_x + 0.5f) / samples;
                while (crossing < crossings.size() &&
                       crossings[crossing].first <= sample_x) {
                    winding += crossings[crossing].second;
                    parity = !parity;
                    ++crossing;
                }
                if (style.even_odd ? parity : winding != 0) {
                    const auto pixel =
                        static_cast<std::size_t>(sub_y / samples) *
                        target.width + static_cast<std::size_t>(sub_x / samples);
                    ++fill_coverage[pixel];
                }
            }
        }
    }
    if (!style.stroke.none && style.stroke_width > 0.0f) {
        std::vector<Segment> segments;
        for (const auto& contour : contours) {
            for (std::size_t index = 1; index < contour.size(); ++index) {
                const auto from = contour[index - 1];
                const auto to = contour[index];
                segments.push_back({from, to,
                    Vec2{std::min(from.x, to.x), std::min(from.y, to.y)},
                    Vec2{std::max(from.x, to.x), std::max(from.y, to.y)}});
            }
        }
        const float half_width = style.stroke_width * 0.5f;
        const std::size_t sample_width =
            static_cast<std::size_t>(target.width) * samples;
        const std::size_t sample_height =
            static_cast<std::size_t>(target.height) * samples;
        std::vector<std::uint8_t> stroke_samples(
            sample_width * sample_height, 0);
        for (const auto& segment : segments) {
            const int sx0 = std::clamp(static_cast<int>(
                std::floor(segment.minimum.x - half_width - 1.0f)), 0,
                static_cast<int>(target.width));
            const int sy0 = std::clamp(static_cast<int>(
                std::floor(segment.minimum.y - half_width - 1.0f)), 0,
                static_cast<int>(target.height));
            const int sx1 = std::clamp(static_cast<int>(
                std::ceil(segment.maximum.x + half_width + 1.0f)), 0,
                static_cast<int>(target.width));
            const int sy1 = std::clamp(static_cast<int>(
                std::ceil(segment.maximum.y + half_width + 1.0f)), 0,
                static_cast<int>(target.height));
            for (int sub_y = sy0 * samples; sub_y < sy1 * samples; ++sub_y) {
                const float sample_y = (sub_y + 0.5f) / samples;
                for (int sub_x = sx0 * samples; sub_x < sx1 * samples; ++sub_x) {
                    const float sample_x = (sub_x + 0.5f) / samples;
                    if (distance_squared_to_segment({sample_x, sample_y}, segment) <=
                        half_width * half_width) {
                        stroke_samples[static_cast<std::size_t>(sub_y) *
                            sample_width + static_cast<std::size_t>(sub_x)] = 1;
                    }
                }
            }
        }
        for (int py = y0; py < y1; ++py) for (int px = x0; px < x1; ++px) {
            unsigned covered = 0;
            for (int sy = 0; sy < samples; ++sy) for (int sx = 0; sx < samples; ++sx)
                covered += stroke_samples[
                    static_cast<std::size_t>(py * samples + sy) * sample_width +
                    static_cast<std::size_t>(px * samples + sx)];
            stroke_coverage[static_cast<std::size_t>(py) * target.width + px] =
                static_cast<std::uint8_t>(covered);
        }
    }
    const Vec2 bounds_min{min_x, min_y};
    const Vec2 bounds_size{std::max(1.0e-6f, max_x - min_x),
                           std::max(1.0e-6f, max_y - min_y)};
    for (int py = y0; py < y1; ++py) for (int px = x0; px < x1; ++px) {
        const std::size_t pixel =
            static_cast<std::size_t>(py) * target.width + px;
        const Vec2 center{px + 0.5f, py + 0.5f};
        if (fill_coverage[pixel] > 0) {
            const Color color = paint_color(style.fill, style.fill.gradient,
                center, bounds_min, bounds_size);
            blend_pixel(target, pixel, color,
                color.a * style.fill_opacity *
                static_cast<float>(fill_coverage[pixel]) / sample_count);
        }
        if (stroke_coverage[pixel] > 0) {
            const Color color = paint_color(style.stroke, style.stroke.gradient,
                center, bounds_min, bounds_size);
            blend_pixel(target, pixel, color,
                color.a * style.stroke_opacity *
                static_cast<float>(stroke_coverage[pixel]) / sample_count);
        }
    }
}

void rasterize_shape(
    TileBuffer& target, const PathShape& path, const TileStyle& style,
    const SvgGradientMap& gradients, float opacity) {
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    if (opacity <= 0.0f) return;
    if (opacity >= 1.0f) {
        rasterize_shape_impl(target, path, style, gradients);
        return;
    }
    TileBuffer scratch{target.width, target.height,
        std::vector<std::uint8_t>(
            static_cast<std::size_t>(target.width) * target.height * 4, 0)};
    rasterize_shape_impl(scratch, path, style, gradients);
    composite_buffer(target, scratch, opacity);
}

[[nodiscard]] bool render_children(
    const boost::property_tree::ptree& parent, TileBuffer& target,
    TileStyle style, std::size_t depth, TileRenderContext& context) {
    if (depth > kMaxPatternDepth) {
        context.error = "SVG text pattern group depth exceeds 8";
        return false;
    }
    for (const auto& [name, child] : parent) {
        if (name == "<xmlattr>" || name == "<xmlcomment>") continue;
        if (name == "<xmltext>") {
            if (trim(child.data()).empty()) continue;
            context.error = "SVG text pattern supports only path, primitive, and group tile content";
            return false;
        }
        if (name == "g") {
            auto group_style = style;
            if (!apply_tile_style(child, group_style, context.gradients,
                                  context.error))
                return false;
            apply_node_transform(child, group_style);
            if (!group_style.visible) continue;
            const float opacity = node_opacity(child);
            if (opacity < 0.0f) {
                context.error = "SVG text pattern tile opacity must be a finite number in [0,1]";
                return false;
            }
            if (opacity < 1.0f) {
                TileBuffer scratch{target.width, target.height,
                    std::vector<std::uint8_t>(
                        static_cast<std::size_t>(target.width) *
                        target.height * 4, 0)};
                if (!render_children(child, scratch, group_style, depth + 1,
                                     context))
                    return false;
                composite_buffer(target, scratch, opacity);
            } else if (!render_children(child, target, group_style, depth + 1,
                                        context)) {
                return false;
            }
            continue;
        }
        if (name != "rect" && name != "path" && name != "circle" &&
            name != "ellipse" && name != "line" && name != "polyline" &&
            name != "polygon") {
            context.error = "SVG text pattern supports only path, primitive, and group tile content";
            return false;
        }
        if (++context.shape_count > kMaxPatternShapes) {
            context.error = "SVG text pattern has more than 32 tile shapes";
            return false;
        }
        auto shape_style = style;
        if (!apply_tile_style(child, shape_style, context.gradients,
                              context.error))
            return false;
        apply_node_transform(child, shape_style);
        if (!shape_style.visible) continue;
        const float opacity = node_opacity(child);
        if (opacity < 0.0f) {
            context.error = "SVG text pattern tile opacity must be a finite number in [0,1]";
            return false;
        }
        std::string path_data;
        if (name == "path")
            path_data = child.get<std::string>("<xmlattr>.d", "");
        else
            path_data = primitive_path_data(name, child);
        if (path_data.empty()) {
            context.error = "SVG text pattern tile geometry is empty or invalid";
            return false;
        }
        auto imported = SvgImporter{}.import_path_data(path_data, {});
        if (!imported.ok) {
            context.error = "SVG text pattern path is malformed: " + imported.error;
            return false;
        }
        if (imported.path.commands.size() > 1024) {
            context.error = "SVG text pattern tile geometry exceeds 1024 path commands";
            return false;
        }
        context.command_count += imported.path.commands.size();
        if (context.command_count > kMaxPatternCommands) {
            context.error = "SVG text pattern tile geometry exceeds 4096 total path commands";
            return false;
        }
        rasterize_shape(target, imported.path, shape_style,
                        context.gradients, opacity);
    }
    return true;
}

}  // namespace

std::optional<std::string> import_svg_patterns(
    const SvgPatternMap& patterns, SvgGradientMap& gradients) {
    for (const auto& [id, definition] : patterns) {
        if (gradients.contains(id))
            return "SVG pattern id collides with another paint definition: #" + id;
        if (definition.node == nullptr)
            return "SVG pattern definition is missing its XML node: #" + id;
        ResolvedPattern resolved;
        std::string error;
        if (!resolve_pattern_href(id, patterns, resolved, error))
            return error;
        const auto units = trim(pattern_attribute(
            resolved, "patternUnits", "objectBoundingBox"));
        const auto content_units = trim(pattern_attribute(
            resolved, "patternContentUnits", "userSpaceOnUse"));
        if ((units != "objectBoundingBox" && units != "userSpaceOnUse") ||
            (content_units != "objectBoundingBox" &&
             content_units != "userSpaceOnUse"))
            return "SVG text patterns support only objectBoundingBox or userSpaceOnUse units";
        const bool object_bounding_box = units == "objectBoundingBox";
        if (object_bounding_box != (content_units == "objectBoundingBox"))
            return "SVG text patterns require matching patternUnits and "
                   "patternContentUnits; mixed units are not supported";
        auto tile = std::make_shared<graphics::PatternTile>();
        tile->object_bounding_box = object_bounding_box;
        if (object_bounding_box) {
            const auto width_text = pattern_attribute(resolved, "width", "0");
            const auto height_text = pattern_attribute(resolved, "height", "0");
            if (!parse_object_dimension(width_text, tile->object_width,
                                        tile->width, "width", error) ||
                !parse_object_dimension(height_text, tile->object_height,
                                        tile->height, "height", error))
                return error;
            for (const auto& [key, slot] : {
                     std::pair{"x", &tile->object_x},
                     std::pair{"y", &tile->object_y}}) {
                float value = 0.0f;
                if (!parse_svg_scalar(pattern_attribute(resolved, key, "0"),
                                      0.0f, value) ||
                    std::abs(value) > 4.0f) {
                    error = std::string("SVG text pattern objectBoundingBox ") +
                        key + " must be a finite fraction within [-4,4]";
                    return error;
                }
                *slot = value;
            }
            tile->raster_scale = kObjectBBoxRaster;
            const auto pattern_transform = parse_transform(
                pattern_attribute(resolved, "patternTransform", ""));
            tile->transform = {
                pattern_transform[0][0], pattern_transform[0][1],
                pattern_transform[1][0], pattern_transform[1][1],
                pattern_transform[2][0], pattern_transform[2][1]};
        } else {
            if (!parse_user_dimension(pattern_attribute(resolved, "width", "0"),
                                      tile->width, "width", error) ||
                !parse_user_dimension(pattern_attribute(resolved, "height", "0"),
                                      tile->height, "height", error))
                return error;
            auto pattern_transform = parse_transform(
                pattern_attribute(resolved, "patternTransform", ""));
            for (const auto& [key, slot] : {
                     std::pair{"x", 4U}, std::pair{"y", 5U}}) {
                float offset = 0.0f;
                if (!reject_percent_user_unit(
                        pattern_attribute(resolved, key, "0"), key, error) ||
                    !parse_svg_scalar(pattern_attribute(resolved, key, "0"),
                                      0.0f, offset)) {
                    error = std::string("SVG text pattern ") + key +
                        " must be a finite user-space value";
                    return error;
                }
                pattern_transform[2][slot] += offset;
            }
            tile->transform = {
                pattern_transform[0][0], pattern_transform[0][1],
                pattern_transform[1][0], pattern_transform[1][1],
                pattern_transform[2][0], pattern_transform[2][1]};
        }
        TileBuffer buffer{tile->width, tile->height,
            std::vector<std::uint8_t>(
                static_cast<std::size_t>(tile->width) * tile->height * 4, 0)};
        TileStyle base_style;
        base_style.current_color = resolved.inherited_color;
        if (object_bounding_box) {
            const float scale = tile->raster_scale;
            Affine2 content_transform(1.0f);
            content_transform[2][0] = -tile->object_x * scale;
            content_transform[2][1] = -tile->object_y * scale;
            base_style.transform = content_transform * parse_transform(
                std::string("scale(") + std::to_string(scale) + ")");
        }
        TileRenderContext context{gradients};
        if (!render_children(*resolved.content, buffer, base_style, 0, context))
            return context.error;
        if (context.shape_count == 0)
            return "SVG text pattern tile is empty";
        tile->rgba = std::move(buffer.rgba);
        SvgGradientSpec pattern;
        pattern.pattern_tile = std::move(tile);
        gradients.emplace(id, std::move(pattern));
    }
    return std::nullopt;
}

}  // namespace chronon3d::assets::svg_gradient_detail
