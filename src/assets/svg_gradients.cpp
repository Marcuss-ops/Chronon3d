#include "svg_gradient_detail.hpp"
#include "svg_importer.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <set>
#include <string>
#include <utility>

namespace chronon3d::assets::svg_gradient_detail {
using svg_loader_detail::parse_svg_color;
using svg_loader_detail::parse_svg_length;
using svg_loader_detail::style_properties;
using svg_loader_detail::trim;
using svg_loader_detail::transform_numbers;

bool parse_svg_scalar(std::string text, float fallback, float& value) {
    text = trim(std::move(text));
    bool percent = !text.empty() && text.back() == '%';
    if (percent) text.pop_back();
    try {
        std::size_t used = 0;
        value = std::stof(text, &used);
        if (used != text.size() || !std::isfinite(value)) return false;
        if (percent) value /= 100.0f;
        (void)fallback;
        return std::abs(value) <= 100000.0f;
    } catch (const std::invalid_argument&) { return false; } catch (const std::out_of_range&) { return false; }
}

std::optional<std::string> collect_svg_gradients(
    const boost::property_tree::ptree& tree, SvgGradientMap& gradients,
    Color inherited_color) {
    for (const auto& [name, node] : tree) {
        Color current_color = inherited_color;
        const auto node_properties = style_properties(node);
        if (const auto color_it = node_properties.find("color");
            color_it != node_properties.end()) {
            if (const auto color = parse_svg_color(color_it->second, current_color))
                current_color = *color;
        }
        if (name == "linearGradient" || name == "radialGradient") {
            const auto id = node.get_optional<std::string>("<xmlattr>.id");
            if (!id || id->empty() || id->size() > 128 || gradients.contains(*id))
                return "SVG gradient requires a unique bounded id";
            const auto units = node.get<std::string>("<xmlattr>.gradientUnits", "objectBoundingBox");
            if ((units != "objectBoundingBox" && units != "userSpaceOnUse") ||
                node.get<std::string>("<xmlattr>.spreadMethod", "pad") != "pad" ||
                node.get_optional<std::string>("<xmlattr>.href") ||
                node.get_optional<std::string>("<xmlattr>.{http://www.w3.org/1999/xlink}href"))
                return "SVG gradient supports objectBoundingBox units, pad spread, and no inheritance";
            SvgGradientSpec gradient;
            gradient.radial = name == "radialGradient";
            gradient.user_space = (units == "userSpaceOnUse");
            if (const auto transform = node.get_optional<std::string>("<xmlattr>.gradientTransform"))
                gradient.transform = parse_transform(*transform);
            auto coordinate = [&](const char* key, const char* fallback, float& out) {
                const auto val_str = node.get<std::string>(std::string("<xmlattr>.") + key, fallback);
                if (gradient.user_space) {
                    if (const auto parsed = parse_svg_length(val_str)) {
                        out = *parsed;
                        return true;
                    }
                }
                return parse_svg_scalar(val_str, 0.0f, out);
            };
            if (gradient.radial) {
                if (!coordinate("cx", "50%", gradient.p0.x) ||
                    !coordinate("cy", "50%", gradient.p0.y) ||
                    !coordinate("r", "50%", gradient.radius) || gradient.radius <= 0.0f)
                    return "SVG radial gradient coordinates are invalid";
            } else if (!coordinate("x1", "0%", gradient.p0.x) ||
                       !coordinate("y1", "0%", gradient.p0.y) ||
                       !coordinate("x2", "100%", gradient.p1.x) ||
                       !coordinate("y2", "0%", gradient.p1.y)) {
                return "SVG linear gradient coordinates are invalid";
            }
            float previous = 0.0f;
            for (const auto& [child_name, stop] : node) {
                if (child_name != "stop") continue;
                if (gradient.stops.size() >= 64) return "SVG gradient exceeds 64 stops";
                float offset = 0.0f;
                if (!parse_svg_scalar(stop.get<std::string>("<xmlattr>.offset", "0"), 0.0f, offset) || offset < previous || offset < 0.0f || offset > 1.0f)
                    return "SVG gradient stop offsets must be ordered in [0,1]";
                const auto props = style_properties(stop);
                Color stop_current_color = current_color;
                if (const auto color_it = props.find("color"); color_it != props.end()) {
                    if (const auto color = parse_svg_color(color_it->second, stop_current_color))
                        stop_current_color = *color;
                }
                const auto color_it = props.find("stop-color");
                const auto color = parse_svg_color(color_it == props.end() ? "black" : color_it->second, stop_current_color);
                float opacity = 1.0f;
                const auto opacity_it = props.find("stop-opacity");
                if (opacity_it != props.end() && !parse_svg_scalar(opacity_it->second, 1.0f, opacity))
                    return "SVG stop-opacity is invalid";
                if (!color || opacity < 0.0f || opacity > 1.0f) return "SVG gradient stop paint is invalid";
                auto stop_color = *color;
                stop_color.a *= opacity;
                const std::size_t stop_index = gradient.stops.size();
                std::set<std::string> animated_properties;
                for (const auto& [stop_child_name, animation] : stop) {
                    if (stop_child_name != "animate") continue;
                    const auto property = animation.get<std::string>("<xmlattr>.attributeName", "");
                    if (!animated_properties.insert(property).second || gradient.animations.size() >= 64)
                        return "SVG gradient stop animation targets must be unique and bounded";
                    if (animation.get<std::string>("<xmlattr>.repeatCount", "1") != "1" ||
                        animation.get<std::string>("<xmlattr>.fill", "freeze") != "freeze" ||
                        animation.get<std::string>("<xmlattr>.additive", "replace") != "replace" ||
                        animation.get<std::string>("<xmlattr>.begin", "0s") != "0s")
                        return "SVG gradient stop animations require one iteration, freeze, and begin=0";
                    float duration = 0.0f;
                    auto duration_text = animation.get<std::string>("<xmlattr>.dur", "");
                    float time_scale = 1.0f;
                    if (duration_text.ends_with("ms")) { time_scale = 0.001f; duration_text.resize(duration_text.size() - 2); }
                    else if (duration_text.ends_with("s")) duration_text.pop_back();
                    try {
                        std::size_t used = 0;
                        duration = std::stof(duration_text, &used) * time_scale;
                        if (used != duration_text.size() || !std::isfinite(duration) || duration <= 0.0f || duration > 3600.0f)
                            return "SVG gradient animation duration is invalid";
                    } catch (const std::invalid_argument&) { return "SVG gradient animation duration is invalid"; } catch (const std::out_of_range&) { return "SVG gradient animation duration is invalid"; }
                    const auto mode = animation.get<std::string>("<xmlattr>.calcMode", "linear");
                    if (mode != "linear" && mode != "discrete") return "SVG gradient animation calcMode is unsupported";
                    std::string from_text = animation.get<std::string>("<xmlattr>.from", "");
                    std::string to_text = animation.get<std::string>("<xmlattr>.to", "");
                    if (const auto values = animation.get_optional<std::string>("<xmlattr>.values")) {
                        const auto separator = values->find(';');
                        if (separator == std::string::npos || values->find(';', separator + 1) != std::string::npos)
                            return "SVG gradient animation supports exactly two values";
                        from_text = values->substr(0, separator);
                        to_text = values->substr(separator + 1);
                    }
                    SvgGradientSpec::StopAnimation track;
                    track.stop_index = stop_index;
                    track.duration_seconds = duration;
                    track.discrete = mode == "discrete";
                    if (property == "stop-color") {
                        const auto from_color = parse_svg_color(from_text, stop_current_color);
                        const auto to_color = parse_svg_color(to_text, stop_current_color);
                        if (!from_color || !to_color) return "SVG animated stop-color values are invalid";
                        track.from = *from_color;
                        track.to = *to_color;
                    } else if (property == "stop-opacity") {
                        if (!parse_svg_scalar(from_text, 0.0f, track.from.a) ||
                            !parse_svg_scalar(to_text, 0.0f, track.to.a) ||
                            track.from.a < 0.0f || track.from.a > 1.0f ||
                            track.to.a < 0.0f || track.to.a > 1.0f)
                            return "SVG animated stop-opacity values must be in [0,1]";
                        track.opacity = true;
                    } else {
                        return "SVG gradient animation property is unsupported";
                    }
                    gradient.animations.push_back(track);
                }
                gradient.stops.push_back({offset, stop_color});
                previous = offset;
            }
            if (gradient.stops.size() < 2) return "SVG gradients require at least two stops";
            if (!gradient.animations.empty()) {
                const auto& reference = gradient.animations.front();
                for (const auto& animation : gradient.animations)
                    if (std::abs(animation.duration_seconds - reference.duration_seconds) > 1e-6f ||
                        animation.discrete != reference.discrete)
                        return "SVG animated gradient stops must share duration and calcMode";
            }
            gradients.emplace(*id, std::move(gradient));
        } else if (name == "pattern") {
            const auto id = node.get_optional<std::string>("<xmlattr>.id");
            if (!id || id->empty() || id->size() > 128 || gradients.contains(*id))
                return "SVG pattern requires a unique bounded id";
            if (node.get<std::string>("<xmlattr>.patternUnits", "objectBoundingBox") !=
                    "userSpaceOnUse" ||
                node.get<std::string>("<xmlattr>.patternContentUnits", "userSpaceOnUse") !=
                    "userSpaceOnUse" || node.get_optional<std::string>("<xmlattr>.href") ||
                node.get_optional<std::string>("<xmlattr>.{http://www.w3.org/1999/xlink}href"))
                return "SVG text patterns require explicit userSpaceOnUse units and no inheritance";
            if (!style_properties(node).empty())
                return "SVG text pattern-level paint/style inheritance is not supported";
            const auto dimension = [&](const char* key, std::uint32_t& output) {
                float value = 0.0f;
                if (!parse_svg_scalar(node.get<std::string>(
                        std::string("<xmlattr>.") + key, "0"), 0.0f, value) ||
                    value < 1.0f || value > 256.0f || std::floor(value) != value)
                    return false;
                output = static_cast<std::uint32_t>(value);
                return true;
            };
            auto tile = std::make_shared<graphics::PatternTile>();
            if (!dimension("width", tile->width) || !dimension("height", tile->height))
                return "SVG text pattern dimensions must be integer user-space pixels in [1,256]";
            if (const auto transform = node.get_optional<std::string>("<xmlattr>.patternTransform")) {
                const auto matrix = parse_transform(*transform);
                tile->transform = {matrix[0][0], matrix[0][1], matrix[1][0],
                    matrix[1][1], matrix[2][0], matrix[2][1]};
            }
            for (const auto [key, slot] : {std::pair{"x", 4U}, std::pair{"y", 5U}}) {
                float offset = 0.0f;
                if (!parse_svg_scalar(node.get<std::string>(
                        std::string("<xmlattr>.") + key, "0"), 0.0f, offset))
                    return "SVG text pattern x/y must be finite user-space values";
                tile->transform[slot] += offset;
            }
            tile->rgba.assign(static_cast<std::size_t>(tile->width) * tile->height * 4, 0);
            bool has_content = false;
            std::size_t pattern_shape_count = 0;
            for (const auto& [child_name, child] : node) {
                if (child_name == "<xmlattr>" || child_name == "<xmltext>") continue;
                if (child_name != "rect" && child_name != "path" &&
                    child_name != "circle" && child_name != "ellipse" &&
                    child_name != "line" && child_name != "polyline" &&
                    child_name != "polygon")
                    return "SVG text pattern supports only path and primitive tile content";
                if (++pattern_shape_count > 32)
                    return "SVG text pattern has more than 32 tile shapes";
                const auto props = style_properties(child);
                for (const auto& [property, value] : props) {
                    (void)value;
                    if (property != "fill" && property != "opacity" &&
                        property != "fill-opacity" && property != "fill-rule" &&
                        property != "stroke")
                        return "SVG text pattern tile property is unsupported: " + property;
                }
                if (const auto stroke = props.find("stroke");
                    stroke != props.end() && trim(stroke->second) != "none")
                    return "SVG text pattern tile content does not support stroke paint";
                std::string path_data;
                if (child_name == "path")
                    path_data = child.get<std::string>("<xmlattr>.d", "");
                else
                    path_data = primitive_path_data(child_name, child);
                if (path_data.empty()) return "SVG text pattern tile geometry is empty or invalid";
                auto imported = SvgImporter{}.import_path_data(path_data, {});
                if (!imported.ok) return "SVG text pattern path is malformed: " + imported.error;
                if (imported.path.commands.size() > 1024)
                    return "SVG text pattern tile geometry exceeds 1024 path commands";
                if (const auto transform = child.get_optional<std::string>("<xmlattr>.transform"))
                    apply_transform(imported.path, parse_transform(*transform));
                const auto fill = props.find("fill");
                const auto color = parse_svg_color(fill == props.end() ? "black" : fill->second,
                                                   current_color);
                if (!color) return "SVG text pattern rect requires a solid fill";
                float opacity = 1.0f;
                float fill_opacity_value = 1.0f;
                if (const auto opacity_it = props.find("opacity"); opacity_it != props.end() &&
                    !parse_svg_scalar(opacity_it->second, 1.0f, opacity))
                    return "SVG text pattern rect opacity is invalid";
                if (const auto fill_opacity = props.find("fill-opacity"); fill_opacity != props.end() &&
                    !parse_svg_scalar(fill_opacity->second, 1.0f, fill_opacity_value))
                    return "SVG text pattern rect fill-opacity is invalid";
                const auto r = static_cast<std::uint8_t>(std::round(std::clamp(color->r, 0.0f, 1.0f) * 255));
                const auto g = static_cast<std::uint8_t>(std::round(std::clamp(color->g, 0.0f, 1.0f) * 255));
                const auto b = static_cast<std::uint8_t>(std::round(std::clamp(color->b, 0.0f, 1.0f) * 255));
                const auto a = static_cast<std::uint8_t>(std::round(
                    std::clamp(color->a * opacity * fill_opacity_value, 0.0f, 1.0f) * 255));

                using Contour = std::vector<Vec2>;
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
                for (const auto& command : imported.path.commands) {
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
                            const float t = step / 12.0f, u = 1.0f - t;
                            append(start * (u * u) + command.p0 * (2.0f * u * t) +
                                   command.p1 * (t * t));
                        }
                    } else if (command.type == PathCommandType::CubicTo) {
                        const Vec2 start = current_point;
                        for (int step = 1; step <= 16; ++step) {
                            const float t = step / 16.0f, u = 1.0f - t;
                            append(start * (u * u * u) + command.p0 * (3.0f * u * u * t) +
                                   command.p1 * (3.0f * u * t * t) + command.p2 * (t * t * t));
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
                float min_x = static_cast<float>(tile->width), min_y = static_cast<float>(tile->height);
                float max_x = 0.0f, max_y = 0.0f;
                for (const auto& contour : contours) for (const auto point : contour) {
                    min_x = std::min(min_x, point.x); min_y = std::min(min_y, point.y);
                    max_x = std::max(max_x, point.x); max_y = std::max(max_y, point.y);
                }
                const int x0 = std::clamp(static_cast<int>(std::floor(min_x)), 0,
                                          static_cast<int>(tile->width));
                const int y0 = std::clamp(static_cast<int>(std::floor(min_y)), 0,
                                          static_cast<int>(tile->height));
                const int x1 = std::clamp(static_cast<int>(std::ceil(max_x)), 0,
                                          static_cast<int>(tile->width));
                const int y1 = std::clamp(static_cast<int>(std::ceil(max_y)), 0,
                                          static_cast<int>(tile->height));
                const bool even_odd = props.contains("fill-rule") &&
                    trim(props.at("fill-rule")) == "evenodd";
                constexpr int samples = 4;
                constexpr unsigned sample_count = samples * samples;
                std::vector<std::uint8_t> coverage(
                    static_cast<std::size_t>(tile->width) * tile->height, 0);
                for (int sub_y = y0 * samples; sub_y < y1 * samples; ++sub_y) {
                    const float sample_y = (sub_y + 0.5f) / samples;
                    std::vector<std::pair<float, int>> crossings;
                    for (const auto& contour : contours) {
                        if (contour.size() < 4) continue;
                        for (std::size_t edge = 1; edge < contour.size(); ++edge) {
                            const auto& p = contour[edge - 1];
                            const auto& q = contour[edge];
                            if ((p.y > sample_y) == (q.y > sample_y)) continue;
                            const float cross_x = p.x + (sample_y - p.y) *
                                (q.x - p.x) / (q.y - p.y);
                            crossings.emplace_back(cross_x, q.y > p.y ? 1 : -1);
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
                        if (even_odd ? parity : winding != 0) {
                            const auto pixel = static_cast<std::size_t>(sub_y / samples) *
                                tile->width + static_cast<std::size_t>(sub_x / samples);
                            ++coverage[pixel];
                        }
                    }
                }
                for (int py = y0; py < y1; ++py) for (int px = x0; px < x1; ++px) {
                    const unsigned covered = coverage[
                        static_cast<std::size_t>(py) * tile->width + px];
                    if (covered == 0) continue;
                    if (covered == 0) continue;
                    const auto at = (static_cast<std::size_t>(py) * tile->width + px) * 4;
                    // Pattern children paint in document order using normal
                    // source-over compositing. Keep the stored tile straight
                    // RGBA; the software text backend premultiplies it when
                    // creating the Blend2D image.
                    const unsigned da = tile->rgba[at + 3];
                    const unsigned sa = (static_cast<unsigned>(a) * covered +
                                         sample_count / 2U) / sample_count;
                    const unsigned out_a = sa + (da * (255U - sa) + 127U) / 255U;
                    if (out_a != 0U) {
                        const auto over = [&](unsigned src, unsigned dst) {
                            const unsigned premul = src * sa +
                                (dst * da * (255U - sa) + 127U) / 255U;
                            return static_cast<std::uint8_t>(
                                std::min(255U, (premul + out_a / 2U) / out_a));
                        };
                        tile->rgba[at + 0] = over(r, tile->rgba[at + 0]);
                        tile->rgba[at + 1] = over(g, tile->rgba[at + 1]);
                        tile->rgba[at + 2] = over(b, tile->rgba[at + 2]);
                        tile->rgba[at + 3] = static_cast<std::uint8_t>(out_a);
                    }
                }
                has_content = true;
            }
            if (!has_content) return "SVG text pattern tile is empty";
            SvgGradientSpec pattern;
            pattern.pattern_tile = std::move(tile);
            gradients.emplace(*id, std::move(pattern));
        } else if (name != "<xmlattr>") {
            if (auto error = collect_svg_gradients(node, gradients, current_color)) return error;
        }
    }
    return std::nullopt;
}

}  // namespace chronon3d::assets::svg_gradient_detail
