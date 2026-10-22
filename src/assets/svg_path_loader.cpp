#include <chronon3d/assets/svg_path_loader.hpp>

#include "svg_importer.hpp"
#include <stb_image.h>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <functional>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <cctype>
#include <map>
#include <set>
#include <filesystem>
#include <limits>

namespace chronon3d::assets {
namespace {

using Affine2 = glm::mat3;

std::vector<float> transform_numbers(std::string_view text) {
    static const std::regex number(R"([-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?)");
    std::vector<float> values;
    for (std::cregex_iterator it(text.data(), text.data() + text.size(), number), end;
         it != end; ++it) {
        values.push_back(std::stof(it->str()));
    }
    return values;
}

Affine2 parse_transform(std::string_view text) {
    Affine2 result(1.0f);
    const auto translation = [](float x, float y) {
        return Affine2(
            glm::vec3(1.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f),
            glm::vec3(x, y, 1.0f));
    };
    const auto scaling = [](float x, float y) {
        return Affine2(
            glm::vec3(x, 0.0f, 0.0f),
            glm::vec3(0.0f, y, 0.0f),
            glm::vec3(0.0f, 0.0f, 1.0f));
    };
    const auto rotation = [](float radians) {
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        return Affine2(
            glm::vec3(c, s, 0.0f),
            glm::vec3(-s, c, 0.0f),
            glm::vec3(0.0f, 0.0f, 1.0f));
    };
    const auto skew_x = [](float radians) {
        return Affine2(
            glm::vec3(1.0f, 0.0f, 0.0f),
            glm::vec3(std::tan(radians), 1.0f, 0.0f),
            glm::vec3(0.0f, 0.0f, 1.0f));
    };
    const auto skew_y = [](float radians) {
        return Affine2(
            glm::vec3(1.0f, std::tan(radians), 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f),
            glm::vec3(0.0f, 0.0f, 1.0f));
    };
    static const std::regex operation(R"(([A-Za-z]+)\s*\(([^)]*)\))");
    const std::string input(text);
    for (std::sregex_iterator it(input.begin(), input.end(), operation), end;
         it != end; ++it) {
        const auto name = (*it)[1].str();
        const auto values = transform_numbers((*it)[2].str());
        Affine2 local(1.0f);
        if (name == "matrix" && values.size() == 6) {
            local = Affine2(
                values[0], values[1], 0.0f,
                values[2], values[3], 0.0f,
                values[4], values[5], 1.0f);
        } else if (name == "translate" && !values.empty()) {
            local = translation(values[0], values.size() > 1 ? values[1] : 0.0f);
        } else if (name == "scale" && !values.empty()) {
            local = scaling(values[0], values.size() > 1 ? values[1] : values[0]);
        } else if (name == "rotate" && !values.empty()) {
            const float radians = glm::radians(values[0]);
            if (values.size() >= 3) {
                const glm::vec2 pivot(values[1], values[2]);
                local = translation(pivot.x, pivot.y) * rotation(radians) *
                    translation(-pivot.x, -pivot.y);
            } else {
                local = rotation(radians);
            }
        } else if (name == "skewX" && !values.empty()) {
            local = skew_x(glm::radians(values[0]));
        } else if (name == "skewY" && !values.empty()) {
            local = skew_y(glm::radians(values[0]));
        }
        result = result * local;
    }
    return result;
}

std::optional<Affine2> parse_viewbox_transform(
    const boost::property_tree::ptree& node) {
    const auto viewbox = node.get_optional<std::string>("<xmlattr>.viewBox");
    if (!viewbox) return std::nullopt;
    const auto values = transform_numbers(*viewbox);
    if (values.size() != 4 || values[2] <= 0.0f || values[3] <= 0.0f) {
        return std::nullopt;
    }

    const float viewport_width = node.get<float>("<xmlattr>.width", values[2]);
    const float viewport_height = node.get<float>("<xmlattr>.height", values[3]);
    if (viewport_width <= 0.0f || viewport_height <= 0.0f) return std::nullopt;

    // SVG's default preserveAspectRatio is xMidYMid meet.  Keep the
    // importer deterministic and geometry-only: explicit viewport dimensions
    // map the viewBox into that viewport while preserving its aspect ratio.
    const float scale = std::min(viewport_width / values[2], viewport_height / values[3]);
    const float offset_x = (viewport_width - values[2] * scale) * 0.5f;
    const float offset_y = (viewport_height - values[3] * scale) * 0.5f;
    return Affine2(
        glm::vec3(scale, 0.0f, 0.0f),
        glm::vec3(0.0f, scale, 0.0f),
        glm::vec3(offset_x - values[0] * scale, offset_y - values[1] * scale, 1.0f));
}

inline std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

inline std::optional<float> parse_svg_length(std::string_view text, float percentage_relative_to = -1.0f) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n'))
        text.remove_suffix(1);
    if (text.empty()) return std::nullopt;

    float scale = 1.0f;
    if (text.ends_with("px")) {
        text.remove_suffix(2);
    } else if (text.ends_with("pt")) {
        text.remove_suffix(2);
        scale = 96.0f / 72.0f; // 1pt = 1.3333333px
    } else if (text.ends_with("pc")) {
        text.remove_suffix(2);
        scale = 16.0f; // 1pc = 12pt = 16px
    } else if (text.ends_with("in")) {
        text.remove_suffix(2);
        scale = 96.0f; // 1in = 96px
    } else if (text.ends_with("mm")) {
        text.remove_suffix(2);
        scale = 96.0f / 25.4f; // 1mm = 3.779527559px
    } else if (text.ends_with("cm")) {
        text.remove_suffix(2);
        scale = 96.0f / 2.54f; // 1cm = 37.79527559px
    } else if (text.ends_with("%")) {
        if (percentage_relative_to < 0.0f) return std::nullopt;
        text.remove_suffix(1);
        scale = percentage_relative_to * 0.01f;
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1);
    try {
        std::size_t consumed = 0;
        std::string s(text);
        float val = std::stof(s, &consumed);
        if (consumed != s.size() || !std::isfinite(val)) return std::nullopt;
        return val * scale;
    } catch (...) {
        return std::nullopt;
    }
}

inline std::vector<uint8_t> decode_base64_data(std::string_view input) {
    static const int8_t table[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };
    std::vector<uint8_t> out;
    out.reserve(input.size() * 3 / 4);
    uint32_t buffer = 0;
    int bits = 0;
    for (char c : input) {
        if (c == '=') break;
        int val = table[static_cast<uint8_t>(c)];
        if (val < 0) continue;
        buffer = (buffer << 6) | val;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

std::string primitive_path_data(
    const std::string& name,
    const boost::property_tree::ptree& node) {
    const auto attr = [&node](const char* key, float fallback = 0.0f) {
        if (const auto opt = node.get_optional<std::string>(std::string{"<xmlattr>."} + key)) {
            if (const auto parsed = parse_svg_length(*opt)) return *parsed;
        }
        return fallback;
    };
    std::ostringstream path;
    path << std::setprecision(9);
    if (name == "line") {
        path << "M " << attr("x1") << ' ' << attr("y1")
             << " L " << attr("x2") << ' ' << attr("y2");
    } else if (name == "rect") {
        const float x = attr("x");
        const float y = attr("y");
        const float width = attr("width");
        const float height = attr("height");
        if (width <= 0.0f || height <= 0.0f) return {};
        float rx = attr("rx");
        float ry = attr("ry");
        if (rx > 0.0f && ry == 0.0f) ry = rx;
        if (ry > 0.0f && rx == 0.0f) rx = ry;
        rx = std::min(rx, width * 0.5f);
        ry = std::min(ry, height * 0.5f);
        if (rx == 0.0f || ry == 0.0f) {
            path << "M " << x << ' ' << y
                 << " H " << x + width << " V " << y + height
                 << " H " << x << " Z";
        } else {
            path << "M " << x + rx << ' ' << y
                 << " H " << x + width - rx
                 << " A " << rx << ' ' << ry << " 0 0 1 " << x + width << ' ' << y + ry
                 << " V " << y + height - ry
                 << " A " << rx << ' ' << ry << " 0 0 1 " << x + width - rx << ' ' << y + height
                 << " H " << x + rx
                 << " A " << rx << ' ' << ry << " 0 0 1 " << x << ' ' << y + height - ry
                 << " V " << y + ry
                 << " A " << rx << ' ' << ry << " 0 0 1 " << x + rx << ' ' << y << " Z";
        }
    } else if (name == "circle" || name == "ellipse") {
        const float cx = attr("cx");
        const float cy = attr("cy");
        const float rx = attr(name == "circle" ? "r" : "rx");
        const float ry = attr(name == "circle" ? "r" : "ry");
        if (rx <= 0.0f || ry <= 0.0f) return {};
        path << "M " << cx - rx << ' ' << cy
             << " A " << rx << ' ' << ry << " 0 1 0 " << cx + rx << ' ' << cy
             << " A " << rx << ' ' << ry << " 0 1 0 " << cx - rx << ' ' << cy << " Z";
    } else if (name == "polyline" || name == "polygon") {
        const auto points = node.get_optional<std::string>("<xmlattr>.points");
        if (!points) return {};
        const auto values = transform_numbers(*points);
        if (values.size() < 4 || values.size() % 2 != 0) return {};
        path << "M " << values[0] << ' ' << values[1];
        for (std::size_t i = 2; i < values.size(); i += 2) {
            path << " L " << values[i] << ' ' << values[i + 1];
        }
        if (name == "polygon") path << " Z";
    }
    return path.str();
}

Vec2 transform_point(const Affine2& transform, Vec2 point) {
    const glm::vec3 transformed = transform * glm::vec3(point, 1.0f);
    return {transformed.x, transformed.y};
}

void apply_transform(PathShape& path, const Affine2& transform) {
    for (auto& command : path.commands) {
        if (command.type == PathCommandType::Close) continue;
        command.p0 = transform_point(transform, command.p0);
        if (command.type == PathCommandType::QuadraticTo || command.type == PathCommandType::CubicTo) {
            command.p1 = transform_point(transform, command.p1);
        }
        if (command.type == PathCommandType::CubicTo) {
            command.p2 = transform_point(transform, command.p2);
        }
    }
}

struct SvgStyleState {
    std::optional<Fill> fill{Fill::solid_color(Color::black())};
    PathStroke stroke{.enabled = false};
    // SVG's initial stroke-miterlimit is 4 (native Chronon paths retain 10).
    float stroke_miter_limit{4.0f};
    Color current_color{Color::black()};
    PathFillRule fill_rule{PathFillRule::NonZero};
    float opacity{1.0f};
    float fill_opacity{1.0f};
    float stroke_opacity{1.0f};
    bool display_none{false};
    bool visibility_hidden{false};
    std::string fill_gradient_id;
    std::string paint_order{"normal"};
    std::string filter_id;
    std::string vector_mask_id;
    bool vector_mask_is_mask{false};
    struct VectorMaskScope {
        std::string id;
        bool is_mask{false};
        Affine2 transform{1.0f};
    };
    std::vector<VectorMaskScope> vector_mask_scopes;
    std::vector<PathShape> vector_masks;
    std::string source_id;
    std::vector<std::string> group_ids;
    std::vector<SvgAnimationSpec> inherited_animations;
    std::optional<SvgMorphAnimationSpec> morph_animation;
    // ADR-035 — isolated SVG groups enclosing this element, outermost first.
    std::vector<SvgStyleGroupRef> groups;
};

struct SvgGradientSpec {
    bool radial{false};
    bool user_space{false};
    Vec2 p0{0.0f, 0.0f};
    Vec2 p1{1.0f, 0.0f};
    float radius{0.5f};
    Affine2 transform{1.0f};
    std::vector<graphics::GradientStop> stops;
    std::shared_ptr<const graphics::PatternTile> pattern_tile;
    struct StopAnimation {
        std::size_t stop_index{0};
        bool opacity{false};
        Color from{1, 1, 1, 1};
        Color to{1, 1, 1, 1};
        float duration_seconds{0.0f};
        bool discrete{false};
    };
    std::vector<StopAnimation> animations;
};

using SvgGradientMap = std::map<std::string, SvgGradientSpec>;
using SvgIdMap = std::map<std::string,
    std::pair<std::string, const boost::property_tree::ptree*>>;


std::optional<float> svg_baseline_shift(std::string value, float font_size) {
    value = trim(std::move(value));
    if (value == "baseline" || value == "0" || value == "0px") return 0.0f;
    if (value == "sub") return font_size * 0.2f;
    if (value == "super") return -font_size * 0.33f;
    const bool percent = value.ends_with('%');
    const bool em = value.ends_with("em");
    const bool px = value.ends_with("px");
    if (percent) value.pop_back();
    else if (em) value.resize(value.size() - 2);
    else if (px) value.resize(value.size() - 2);
    try {
        std::size_t used = 0;
        float amount = std::stof(value, &used);
        if (used != value.size() || !std::isfinite(amount) || std::abs(amount) > 100000.0f)
            return std::nullopt;
        if (percent) amount = amount * font_size / 100.0f;
        else if (em) amount *= font_size;
        return -amount;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> svg_text_path_reference(
    const boost::property_tree::ptree& node);
std::optional<std::pair<float, bool>> svg_text_path_offset(
    const boost::property_tree::ptree& node);

std::optional<Color> parse_svg_color(std::string value, Color current) {
    value = trim(std::move(value));
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value == "none") return std::nullopt;
    if (value == "currentcolor") return current;
    static const std::map<std::string_view, std::string_view> named_colors{
        {"black", "#000000"}, {"silver", "#C0C0C0"}, {"gray", "#808080"},
        {"grey", "#808080"}, {"white", "#FFFFFF"}, {"maroon", "#800000"},
        {"red", "#FF0000"}, {"purple", "#800080"}, {"fuchsia", "#FF00FF"},
        {"green", "#008000"}, {"lime", "#00FF00"}, {"olive", "#808000"},
        {"yellow", "#FFFF00"}, {"navy", "#000080"}, {"blue", "#0000FF"},
        {"teal", "#008080"}, {"aqua", "#00FFFF"}, {"orange", "#FFA500"}
    };
    if (const auto named = named_colors.find(value); named != named_colors.end()) {
        try { return Color::from_hex(std::string{named->second}.c_str()); }
        catch (...) { return std::nullopt; }
    }
    if (value.size() == 4 && value[0] == '#') {
        const auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
        const int r = digit(value[1]), g = digit(value[2]), b = digit(value[3]);
        if (r < 0 || g < 0 || b < 0) return std::nullopt;
        return Color{r / 15.0f, g / 15.0f, b / 15.0f, 1.0f};
    }
    if (value.size() == 7 && value[0] == '#') {
        try { return Color::from_hex(value.c_str()); } catch (...) { return std::nullopt; }
    }
    if (value.rfind("rgb(", 0) == 0 && value.back() == ')') {
        const auto parts = transform_numbers(std::string_view(value).substr(4, value.size() - 5));
        if (parts.size() == 3) return Color{std::clamp(parts[0], 0.0f, 255.0f) / 255.0f, std::clamp(parts[1], 0.0f, 255.0f) / 255.0f, std::clamp(parts[2], 0.0f, 255.0f) / 255.0f, 1.0f};
    }
    return std::nullopt;
}

std::map<std::string, std::string> style_properties(const boost::property_tree::ptree& node) {
    std::map<std::string, std::string> values;
    for (const auto* key : {"fill", "stroke", "stroke-width", "stroke-opacity", "fill-opacity", "opacity", "stop-color", "stop-opacity", "clip-path", "mask", "filter", "color", "display", "visibility", "fill-rule", "stroke-linecap", "stroke-linejoin", "stroke-miterlimit", "stroke-dasharray", "stroke-dashoffset", "font-family", "font-size", "font-weight", "font-style", "direction", "text-anchor", "dominant-baseline", "baseline-shift", "letter-spacing", "word-spacing", "writing-mode"}) {
        if (auto value = node.get_optional<std::string>(std::string("<xmlattr>.") + key)) {
            auto normalized = trim(*value);
            std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (normalized != "inherit") values[key] = *value;
        }
    }
    if (auto style = node.template get_optional<std::string>("<xmlattr>.style")) {
        std::istringstream input(*style);
        std::string item;
        while (std::getline(input, item, ';')) {
            const auto colon = item.find(':');
            if (colon == std::string::npos) continue;
            auto key = trim(item.substr(0, colon));
            auto value = trim(item.substr(colon + 1));
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            auto normalized = value;
            std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (normalized != "inherit") values[key] = value;
        }
    }
    return values;
}

struct SvgCssRule {
    std::string selector;
    std::map<std::string, std::string> declarations;
    int specificity{0};
    std::size_t order{0};
};

bool simple_svg_css_selector_matches(std::string_view selector,
                                     const std::string& element_name,
                                     const boost::property_tree::ptree& node) {
    if (selector.empty() || selector.find_first_of(" \t\r\n>+~:[") != std::string_view::npos)
        return false;
    std::size_t cursor = 0;
    if (selector[cursor] != '.' && selector[cursor] != '#' && selector[cursor] != '*') {
        const auto end = selector.find_first_of(".#", cursor);
        const auto tag = selector.substr(cursor, end == std::string_view::npos
            ? selector.size() - cursor : end - cursor);
        if (tag != element_name) return false;
        cursor = end == std::string_view::npos ? selector.size() : end;
    } else if (selector[cursor] == '*') {
        ++cursor;
    }
    const auto id = node.get_optional<std::string>("<xmlattr>.id");
    const auto classes = node.get_optional<std::string>("<xmlattr>.class");
    std::set<std::string> class_names;
    if (classes) {
        std::istringstream input(*classes);
        std::string value;
        while (input >> value) class_names.insert(value);
    }
    while (cursor < selector.size()) {
        const char prefix = selector[cursor++];
        const auto end = selector.find_first_of(".#", cursor);
        const auto value = selector.substr(cursor, end == std::string_view::npos
            ? selector.size() - cursor : end - cursor);
        if (value.empty()) return false;
        if (prefix == '#' && (!id || *id != value)) return false;
        if (prefix == '.' && !class_names.contains(std::string(value))) return false;
        cursor = end == std::string_view::npos ? selector.size() : end;
    }
    return true;
}

std::optional<std::string> apply_svg_css_rules(boost::property_tree::ptree& document) {
    std::vector<SvgCssRule> rules;
    std::function<std::optional<std::string>(const boost::property_tree::ptree&)> collect =
        [&](const auto& tree) -> std::optional<std::string> {
            for (const auto& [name, node] : tree) {
                if (name == "style") {
                    std::string stylesheet = node.data();
                    for (const auto& [child_name, child] : node)
                        if (child_name == "<xmltext>") stylesheet += child.data();
                    static const std::regex rule_expression(R"(([^{}]+)\{([^{}]*)\})");
                    std::string remainder = stylesheet;
                    std::size_t offset = 0;
                    for (std::sregex_iterator it(stylesheet.begin(), stylesheet.end(), rule_expression),
                             end; it != end; ++it) {
                        remainder.replace(static_cast<std::size_t>(it->position()) - offset,
                            static_cast<std::size_t>(it->length()), " ");
                        offset += static_cast<std::size_t>(it->length()) - 1;
                        std::string selectors = trim((*it)[1].str());
                        std::map<std::string, std::string> declarations;
                        std::istringstream declaration_stream((*it)[2].str());
                        std::string item;
                        while (std::getline(declaration_stream, item, ';')) {
                            if (trim(item).empty()) continue;
                            const auto colon = item.find(':');
                            if (colon == std::string::npos)
                                return "SVG stylesheet declaration is malformed";
                            if (declarations.size() >= 64)
                                return "SVG stylesheet rule exceeds 64 declarations";
                            auto key = trim(item.substr(0, colon));
                            std::transform(key.begin(), key.end(), key.begin(),
                                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                            auto value = trim(item.substr(colon + 1));
                            if (value.find("!important") != std::string::npos)
                                return "SVG stylesheet !important declarations are not supported";
                            declarations[key] = value;
                        }
                        while (!selectors.empty()) {
                            const auto comma = selectors.find(',');
                            const auto selector = trim(selectors.substr(0, comma));
                            if (selector.empty() || selector.size() > 256 || rules.size() >= 4096)
                                return "SVG stylesheet selector or rule count exceeds resource bounds";
                            if (selector.find_first_of(" \t\r\n>+~:[") != std::string::npos)
                                return "SVG CSS supports only simple element, class, and id selectors";
                            const int specificity = static_cast<int>(std::count(selector.begin(), selector.end(), '#')) * 100 +
                                static_cast<int>(std::count(selector.begin(), selector.end(), '.')) * 10 +
                                (selector.empty() || selector[0] == '.' || selector[0] == '#' || selector[0] == '*'
                                    ? 0 : 1);
                            rules.push_back({selector, declarations, specificity, rules.size()});
                            if (comma == std::string::npos) break;
                            selectors.erase(0, comma + 1);
                        }
                    }
                    if (!trim(std::move(remainder)).empty())
                        return "SVG CSS contains unsupported at-rules or malformed rules";
                    continue;
                }
                if (name == "<xmlattr>" || name == "<xmltext>" || name == "<xmlcomment>") continue;
                if (const auto error = collect(node)) return error;
            }
            return std::nullopt;
        };
    if (const auto error = collect(document)) return error;
    if (rules.empty()) return std::nullopt;

    std::function<void(boost::property_tree::ptree&)> apply = [&](auto& tree) {
        for (auto& [name, node] : tree) {
            if (name == "<xmlattr>" || name == "<xmltext>" || name == "<xmlcomment>" || name == "style") continue;
            std::map<std::string, std::pair<int, std::pair<std::size_t, std::string>>> winners;
            for (const auto& rule : rules) {
                if (!simple_svg_css_selector_matches(rule.selector, name, node)) continue;
                for (const auto& [key, value] : rule.declarations) {
                    const auto found = winners.find(key);
                    if (found == winners.end() || rule.specificity > found->second.first ||
                        (rule.specificity == found->second.first && rule.order >= found->second.second.first))
                        winners[key] = {rule.specificity, {rule.order, value}};
                }
            }
            if (!winners.empty()) {
                std::string css;
                for (const auto& [key, winner] : winners) {
                    if (!css.empty()) css += ';';
                    css += key + ':' + winner.second.second;
                }
                if (auto inline_style = node.template get_optional<std::string>("<xmlattr>.style"))
                    css += ';' + *inline_style;
                node.put("<xmlattr>.style", css);
            }
            apply(node);
        }
    };
    apply(document);
    return std::nullopt;
}

std::optional<std::string> validate_svg_subset(
    const boost::property_tree::ptree& tree) {
    static const std::set<std::string> supported_elements{
        "svg", "g", "path", "rect", "circle", "ellipse", "line",
        "polyline", "polygon", "animate", "animateTransform", "defs", "style",
        "linearGradient", "radialGradient", "stop", "pattern", "use", "clipPath", "mask",
        "filter", "feGaussianBlur", "text", "tspan", "textPath", "image"};
    static const std::set<std::string> supported_properties{
        "fill", "stroke", "stroke-width", "stroke-opacity", "fill-opacity",
        "opacity", "stop-color", "stop-opacity", "clip-path", "mask", "filter",
        "stroke-linecap", "stroke-linejoin", "stroke-dasharray", "stroke-dashoffset",
        "stroke-miterlimit", "fill-rule", "color", "display", "visibility", "paint-order",
        "font-family", "font-size",
        "font-weight", "font-style", "direction", "text-anchor", "dominant-baseline", "baseline-shift"};
    static const std::set<std::string> unsupported_paint_attributes{
        "clip-rule", "mix-blend-mode",
        "vector-effect"};
    const auto valid_unit_interval = [](const std::string& text) {
        try {
            std::size_t consumed = 0;
            const float value = std::stof(text, &consumed);
            return consumed == text.size() && std::isfinite(value) &&
                   value >= 0.0f && value <= 1.0f;
        } catch (...) {
            return false;
        }
    };
    const auto valid_stroke_width = [](std::string text) {
        const auto val = parse_svg_length(text);
        return val.has_value() && *val >= 0.0f && *val <= 100000.0f;
    };
    std::function<std::optional<std::string>(const boost::property_tree::ptree&, const std::string&)> visit;
    visit = [&](const boost::property_tree::ptree& current,
                const std::string& parent_name) -> std::optional<std::string> {
        for (const auto& [name, node] : current) {
            if (name == "<xmlattr>") continue;
            if (name == "<xmlcomment>") continue;
            if (name == "<xmltext>") {
                if (parent_name == "style" || parent_name == "text" ||
                    parent_name == "tspan" || parent_name == "textPath" ||
                    trim(node.data()).empty()) continue;
                return "SVG text content is supported only inside <text> or <tspan>";
            }
            if (!supported_elements.contains(name))
                return "SVG element is not supported: <" + name + ">";
            if (name == "style") {
                if (parent_name != "svg") return "SVG stylesheet elements must be direct children of <svg>";
                continue;
            }
            if (name == "animate" || name == "animateTransform") {
                if (parent_name != "path" && parent_name != "g" && parent_name != "stop")
                    return "SVG animation is supported only as a direct child of <path>, <g>, or gradient <stop>";
                if (parent_name == "g" &&
                    (name == "animateTransform"
                        ? node.get<std::string>("<xmlattr>.attributeName", "") != "transform"
                        : (node.get<std::string>("<xmlattr>.attributeName", "") != "opacity")))
                    return "SVG groups support only transform and opacity animation";
                if (parent_name == "stop" &&
                    (name != "animate" ||
                     (node.get<std::string>("<xmlattr>.attributeName", "") != "stop-color" &&
                      node.get<std::string>("<xmlattr>.attributeName", "") != "stop-opacity")))
                    return "SVG gradient stops support only stop-color and stop-opacity animation";
                continue;
            }
            if (name == "tspan" && parent_name != "text" && parent_name != "tspan")
                return "SVG <tspan> must be nested inside <text> or <tspan>";
            if (name == "textPath" && parent_name != "text" && parent_name != "tspan")
                return "SVG <textPath> must be nested inside <text> or <tspan>";
            if (name == "textPath") {
                if (!svg_text_path_reference(node))
                    return "SVG <textPath> requires a local href='#id' path reference";
                if (!svg_text_path_offset(node))
                    return "SVG <textPath> startOffset must be a finite bounded number or percentage";
                const auto method = node.get<std::string>("<xmlattr>.method", "align");
                const auto spacing = node.get<std::string>("<xmlattr>.spacing", "exact");
                const auto side = node.get<std::string>("<xmlattr>.side", "left");
                if (method != "align" || spacing != "exact" || side != "left")
                    return "SVG <textPath> supports only method=align, spacing=exact, and side=left";
            }
            if (name == "text" && parent_name != "svg" && parent_name != "g")
                return "SVG <text> must be a child of <svg> or <g>";
            if (name == "text" || name == "tspan" || name == "textPath") {
                const auto properties = style_properties(node);
                if (const auto it = properties.find("font-size"); it != properties.end()) {
                    std::string value = trim(it->second);
                    if (value.ends_with("px")) value.resize(value.size() - 2);
                    try {
                        std::size_t used = 0;
                        const float size = std::stof(value, &used);
                        if (used != value.size() || !std::isfinite(size) || size <= 0.0f || size > 4096.0f)
                            return "SVG text font-size must be a positive bounded pixel value";
                    } catch (...) { return "SVG text font-size must be a positive bounded pixel value"; }
                }
                if (const auto it = properties.find("text-anchor"); it != properties.end() &&
                    it->second != "start" && it->second != "middle" && it->second != "end")
                    return "SVG text-anchor supports start, middle, or end";
                if (const auto it = properties.find("direction"); it != properties.end() &&
                    it->second != "ltr" && it->second != "rtl")
                    return "SVG text direction supports ltr or rtl";
                if (const auto it = properties.find("dominant-baseline"); it != properties.end() &&
                    it->second != "auto" && it->second != "use-script" &&
                    it->second != "no-change" && it->second != "reset-size" &&
                    it->second != "ideographic" && it->second != "alphabetic" &&
                    it->second != "central" && it->second != "middle" &&
                    it->second != "hanging" && it->second != "mathematical" &&
                    it->second != "text-before-edge" && it->second != "text-after-edge")
                    return "SVG dominant-baseline has an unsupported baseline identifier";
                if (const auto it = properties.find("font-style"); it != properties.end() &&
                    it->second != "normal" && it->second != "italic" && it->second != "oblique")
                    return "SVG font-style supports normal, italic, or oblique";
                if (const auto it = properties.find("font-weight"); it != properties.end()) {
                    const auto weight = trim(it->second);
                    if (weight != "normal" && weight != "bold") {
                        try {
                            std::size_t used = 0;
                            const int parsed = std::stoi(weight, &used);
                            if (used != weight.size() || parsed < 100 || parsed > 900 || parsed % 100 != 0)
                                return "SVG font-weight must be normal, bold, or 100..900 by hundreds";
                        } catch (...) { return "SVG font-weight must be normal, bold, or 100..900 by hundreds"; }
                    }
                }
                if (const auto it = properties.find("baseline-shift"); it != properties.end() &&
                    !svg_baseline_shift(it->second, 16.0f))
                    return "SVG baseline-shift supports baseline, sub, super, px, em, or percent values";
            }
            if (name == "use") {
                const auto href = node.get_optional<std::string>("<xmlattr>.href");
                if (!href || href->size() < 2 || href->front() != '#' ||
                    href->find_first_of(" \t\r\n", 1) != std::string::npos)
                    return "SVG <use> supports only local #id references";
                for (const auto* coordinate : {"x", "y"}) {
                    if (const auto value = node.get_optional<std::string>(std::string("<xmlattr>.") + coordinate)) {
                        try {
                            std::size_t used = 0;
                            const float parsed = std::stof(*value, &used);
                            if (used != value->size() || !std::isfinite(parsed) || std::abs(parsed) > 100000.0f)
                                return "SVG <use> coordinates must be finite and bounded";
                        } catch (...) { return "SVG <use> coordinates must be finite and bounded"; }
                    }
                }
            }

            if (const auto style = node.template get_optional<std::string>("<xmlattr>.style")) {
                std::istringstream declarations(*style);
                std::string declaration;
                while (std::getline(declarations, declaration, ';')) {
                    if (trim(declaration).empty()) continue;
                    const auto colon = declaration.find(':');
                    if (colon == std::string::npos)
                        return "SVG style declaration is malformed";
                    auto key = trim(declaration.substr(0, colon));
                    std::transform(key.begin(), key.end(), key.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (!supported_properties.contains(key))
                        return "SVG style property is not supported: " + key;
                }
            }
            if (const auto attributes = node.get_child_optional("<xmlattr>")) {
                for (const auto& [key, value] : *attributes) {
                    (void)value;
                    if (unsupported_paint_attributes.contains(key))
                        return "SVG presentation attribute is not supported: " + key;
                }
            }
            const auto properties = style_properties(node);
            if (const auto it = properties.find("display"); it != properties.end()) {
                auto value = trim(it->second);
                std::transform(value.begin(), value.end(), value.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (value != "none" && value != "inline" && value != "block")
                    return "SVG display supports none, inline, or block";
            }
            if (const auto it = properties.find("visibility"); it != properties.end()) {
                auto value = trim(it->second);
                std::transform(value.begin(), value.end(), value.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (value != "visible" && value != "hidden" && value != "collapse")
                    return "SVG visibility supports visible, hidden, or collapse";
            }
            for (const auto* key : {"clip-path", "mask"}) {
                if (const auto it = properties.find(key); it != properties.end()) {
                    const auto value = trim(it->second);
                    const bool valid_target = (name == "svg" || name == "g" || name == "path" ||
                                               name == "rect" || name == "circle" || name == "ellipse" ||
                                               name == "polygon" || name == "polyline" || name == "line");
                    if (!valid_target ||
                        !value.starts_with("url(#") || !value.ends_with(')') || value.size() <= 6)
                        return std::string("SVG ") + key + " supports only root, group, or path references to url(#id)";
                }
            }
            if (const auto it = properties.find("filter"); it != properties.end()) {
                const auto val = trim(it->second);
                const bool valid_target = (name == "svg" || name == "g" || name == "path" ||
                                           name == "rect" || name == "circle" || name == "ellipse" ||
                                           name == "polygon" || name == "polyline" || name == "line" ||
                                           name == "image");
                if (!valid_target || (val != "none" &&
                    (!val.starts_with("url(#") || !val.ends_with(')') || val.size() <= 6)))
                    return "SVG filters currently support only a root or element url(#id) reference";
            }
            if (name == "clipPath" || name == "mask") {
                if (parent_name != "defs") return "SVG clipPath and mask definitions must be children of <defs>";
            }
            if (name == "pattern" && parent_name != "defs")
                return "SVG pattern definitions must be children of <defs>";
            if (parent_name == "pattern" && name != "rect" && name != "path" &&
                name != "circle" && name != "ellipse" && name != "line" &&
                name != "polyline" && name != "polygon")
                return "SVG text patterns support only path and primitive tile content";
            if (name == "filter" && parent_name != "defs")
                return "SVG filter definitions must be children of <defs>";
            if (name == "feGaussianBlur" && parent_name != "filter")
                return "SVG feGaussianBlur must be a direct child of <filter>";
            if (name == "stop") {
                if (const auto it = properties.find("stop-color"); it != properties.end() &&
                    !parse_svg_color(it->second, Color::black()))
                    return "SVG gradient stop color is invalid or unsupported";
                if (const auto it = properties.find("stop-opacity"); it != properties.end() &&
                    !valid_unit_interval(trim(it->second)))
                    return "SVG stop-opacity must be a finite number in [0,1]";
            } else if (properties.contains("stop-color") || properties.contains("stop-opacity")) {
                return "SVG stop-color and stop-opacity are supported only on <stop>";
            }
            for (const auto* key : {"fill", "stroke"}) {
                const auto it = properties.find(key);
                if (it == properties.end()) continue;
                auto value = trim(it->second);
                std::transform(value.begin(), value.end(), value.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                const bool local_paint_reference = value.starts_with("url(#") && value.ends_with(")") &&
                    (key == "fill" || (key == "stroke" &&
                        (name == "text" || name == "tspan" || name == "textPath")));
                if (value == "none" || local_paint_reference) continue;
                if (name == "stop" && key == "fill") continue;
                if (!parse_svg_color(it->second, Color::black()))
                    return std::string("SVG ") + key + " color is invalid or unsupported";
            }
            if (const auto it = properties.find("color"); it != properties.end() &&
                !parse_svg_color(it->second, Color::black()))
                return "SVG color is invalid or unsupported";
            for (const auto* key : {"opacity", "fill-opacity", "stroke-opacity"}) {
                const auto it = properties.find(key);
                if (it != properties.end() && !valid_unit_interval(trim(it->second)))
                    return std::string("SVG ") + key + " must be a finite number in [0,1]";
            }
            if (const auto it = properties.find("stroke-width");
                it != properties.end() && !valid_stroke_width(trim(it->second)))
                return "SVG stroke-width must be finite and within [0,100000]";
            if (const auto it = properties.find("stroke-linecap"); it != properties.end() &&
                it->second != "butt" && it->second != "round" && it->second != "square")
                return "SVG stroke-linecap must be butt, round, or square";
            if (const auto it = properties.find("stroke-linejoin"); it != properties.end() &&
                it->second != "miter" && it->second != "round" && it->second != "bevel")
                return "SVG stroke-linejoin must be miter, round, or bevel";
            if (const auto it = properties.find("stroke-miterlimit"); it != properties.end()) {
                try {
                    std::size_t consumed = 0;
                    const float limit = std::stof(trim(it->second), &consumed);
                    if (consumed != trim(it->second).size() || !std::isfinite(limit) ||
                        limit < 1.0f || limit > 1000.0f)
                        return "SVG stroke-miterlimit must be within [1,1000]";
                } catch (...) {
                    return "SVG stroke-miterlimit must be within [1,1000]";
                }
            }
            if (const auto it = properties.find("fill-rule"); it != properties.end() &&
                it->second != "evenodd" && it->second != "nonzero")
                return "SVG fill-rule must be evenodd or nonzero";
            if (const auto it = properties.find("stroke-dashoffset"); it != properties.end()) {
                try {
                    std::size_t consumed = 0;
                    const float offset = std::stof(trim(it->second), &consumed);
                    if (consumed != trim(it->second).size() || !std::isfinite(offset) ||
                        std::abs(offset) > 100000.0f)
                        return "SVG stroke-dashoffset must be a finite bounded number";
                } catch (...) {
                    return "SVG stroke-dashoffset must be a finite bounded number";
                }
            }
            if (const auto it = properties.find("stroke-dasharray"); it != properties.end() &&
                trim(it->second) != "none") {
                const auto dashes = transform_numbers(it->second);
                if (dashes.empty() || dashes.size() > 32 ||
                    std::any_of(dashes.begin(), dashes.end(), [](float dash) {
                        return !std::isfinite(dash) || dash < 0.0f || dash > 100000.0f;
                    }) || std::all_of(dashes.begin(), dashes.end(), [](float dash) { return dash == 0.0f; }))
                    return "SVG stroke-dasharray must contain 1..32 finite non-negative values, not all zero";
            }

            if (name == "linearGradient" || name == "radialGradient" || name == "stop") continue;
            if (auto error = visit(node, name)) return error;
        }
        return std::nullopt;
    };
    return visit(tree, {});
}

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
    } catch (...) { return false; }
}

std::optional<std::string> collect_svg_gradients(
    const boost::property_tree::ptree& tree, SvgGradientMap& gradients,
    Color inherited_color = Color::black()) {
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
                    } catch (...) { return "SVG gradient animation duration is invalid"; }
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

bool parse_svg_gradient_reference(const std::string& text, std::string& id) {
    const auto value = trim(text);
    if (!value.starts_with("url(#") || !value.ends_with(')') || value.size() <= 6) return false;
    id = value.substr(5, value.size() - 6);
    return !id.empty() && id.find_first_of(" \t\r\n") == std::string::npos;
}

std::optional<std::string> parse_root_gaussian_filter(
    const boost::property_tree::ptree& filter, float& std_deviation) {
    if (filter.get<std::string>("<xmlattr>.filterUnits", "objectBoundingBox") !=
            "objectBoundingBox" ||
        filter.get<std::string>("<xmlattr>.primitiveUnits", "userSpaceOnUse") !=
            "userSpaceOnUse")
        return "SVG root Gaussian filter supports objectBoundingBox filterUnits and userSpaceOnUse primitiveUnits";
    if (const auto attributes = filter.get_child_optional("<xmlattr>")) {
        for (const auto& [key, value] : *attributes) {
            (void)value;
            if (key != "id" && key != "filterUnits" && key != "primitiveUnits" &&
                key != "x" && key != "y" && key != "width" && key != "height")
                return "SVG root Gaussian filter contains an unsupported attribute: " + key;
        }
    }
    for (const auto* key : {"filterRes"})
        if (filter.get_optional<std::string>(std::string("<xmlattr>.") + key))
            return "SVG root Gaussian filter does not support custom filter regions or filterRes";
    std::size_t primitive_count = 0;
    const boost::property_tree::ptree* blur = nullptr;
    for (const auto& [name, child] : filter) {
        if (name == "<xmlattr>" || name == "<xmlcomment>" || name == "<xmltext>") {
            if (name == "<xmltext>" && !trim(child.data()).empty())
                return "SVG filter definitions cannot contain text";
            continue;
        }
        if (name != "feGaussianBlur")
            return "SVG root filter supports exactly one feGaussianBlur primitive";
        ++primitive_count;
        blur = &child;
    }
    if (primitive_count != 1 || !blur)
        return "SVG root filter supports exactly one feGaussianBlur primitive";
    if (blur->get<std::string>("<xmlattr>.in", "SourceGraphic") != "SourceGraphic" ||
        blur->get<std::string>("<xmlattr>.edgeMode", "none") != "none")
        return "SVG feGaussianBlur supports only SourceGraphic input, edgeMode=none, and no result chaining";
    if (const auto attributes = blur->get_child_optional("<xmlattr>")) {
        for (const auto& [key, value] : *attributes) {
            (void)value;
            if (key != "stdDeviation" && key != "in" && key != "edgeMode" && key != "result")
                return "SVG feGaussianBlur contains an unsupported attribute: " + key;
        }
    }
    auto deviation_text = blur->get<std::string>("<xmlattr>.stdDeviation", "0");
    std::replace(deviation_text.begin(), deviation_text.end(), ',', ' ');
    std::istringstream values(deviation_text);
    std::vector<float> deviations;
    std::string token;
    while (values >> token) {
        try {
            std::size_t used = 0;
            const float value = std::stof(token, &used);
            if (used != token.size() || !std::isfinite(value) || value < 0.0f || value > 256.0f)
                return "SVG feGaussianBlur stdDeviation must be finite and in [0,256]";
            deviations.push_back(value);
        } catch (...) {
            return "SVG feGaussianBlur stdDeviation must be finite and in [0,256]";
        }
        if (deviations.size() > 2)
            return "SVG feGaussianBlur stdDeviation accepts one isotropic value or two equal axis values";
    }
    if (deviations.empty() || (deviations.size() == 2 &&
        std::abs(deviations[0] - deviations[1]) > 1e-6f))
        return "SVG feGaussianBlur stdDeviation accepts one isotropic value or two equal axis values";
    for (const auto& [name, child] : *blur) {
        if (name != "<xmlattr>" && name != "<xmlcomment>" && name != "<xmltext>")
            return "SVG feGaussianBlur cannot contain child elements";
        if (name == "<xmltext>" && !trim(child.data()).empty())
            return "SVG feGaussianBlur cannot contain text";
    }
    std_deviation = deviations.front();
    return std::nullopt;
}

float style_number(const std::map<std::string, std::string>& style, const char* key, float fallback) {
    const auto it = style.find(key);
    if (it == style.end()) return fallback;
    try { return std::clamp(std::stof(it->second), 0.0f, 1.0f); } catch (...) { return fallback; }
}

bool parse_svg_animation(const std::string& tag, const boost::property_tree::ptree& node,
                         SvgAnimationSpec& output) {
    const auto parse_time = [](std::string text, float fallback, float& value) {
        if (text.empty()) { value = fallback; return true; }
        float scale = 1.0f;
        if (text.ends_with("ms")) { scale = 0.001f; text.resize(text.size() - 2); }
        else if (text.ends_with("s")) text.pop_back();
        try {
            std::size_t used = 0;
            value = std::stof(text, &used) * scale;
            return used == text.size() && std::isfinite(value) && value >= 0.0f && value <= 3600.0f;
        } catch (...) { return false; }
    };
    const auto parse_values = [](const std::string& text, std::vector<float>& values) {
        if (text.empty()) return false;
        for (const char c : text) {
            if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.' ||
                  c == 'e' || c == 'E' || c == ',' || std::isspace(static_cast<unsigned char>(c))))
                return false;
        }
        values = transform_numbers(text);
        return !values.empty() && values.size() <= 2 &&
            std::all_of(values.begin(), values.end(), [](float value) {
                return std::isfinite(value) && std::abs(value) <= 100000.0f;
            });
    };
    const bool transform = tag == "animateTransform";
    const auto attribute = node.get_optional<std::string>("<xmlattr>.attributeName");
    if (!attribute || (transform && *attribute != "transform")) return false;
    SvgAnimationKind paint_kind = SvgAnimationKind::Opacity;
    bool color_property = false;
    bool opacity_property = false;
    if (!transform) {
        if (*attribute == "opacity") paint_kind = SvgAnimationKind::Opacity;
        else if (*attribute == "fill") { paint_kind = SvgAnimationKind::FillColor; color_property = true; }
        else if (*attribute == "stroke") { paint_kind = SvgAnimationKind::StrokeColor; color_property = true; }
        else if (*attribute == "stroke-width") paint_kind = SvgAnimationKind::StrokeWidth;
        else if (*attribute == "fill-opacity") { paint_kind = SvgAnimationKind::FillOpacity; opacity_property = true; }
        else if (*attribute == "stroke-opacity") { paint_kind = SvgAnimationKind::StrokeOpacity; opacity_property = true; }
        else return false;
    }
    if (node.get<std::string>("<xmlattr>.repeatCount", "1") != "1" ||
        node.get<std::string>("<xmlattr>.fill", "freeze") != "freeze" ||
        node.get<std::string>("<xmlattr>.additive", "replace") != "replace") return false;
    float duration = 0.0f, begin = 0.0f;
    if (!parse_time(node.get<std::string>("<xmlattr>.dur", ""), 0.0f, duration) ||
        !parse_time(node.get<std::string>("<xmlattr>.begin", "0s"), 0.0f, begin) || duration <= 0.0f)
        return false;
    const auto mode = node.get<std::string>("<xmlattr>.calcMode", "linear");
    if (mode != "linear" && mode != "discrete") return false;
    std::vector<float> from, to;
    if (color_property) {
        std::string from_color, to_color;
        if (const auto values = node.get_optional<std::string>("<xmlattr>.values")) {
            const auto separator = values->find(';');
            if (separator == std::string::npos || values->find(';', separator + 1) != std::string::npos) return false;
            from_color = values->substr(0, separator);
            to_color = values->substr(separator + 1);
        } else {
            from_color = node.get<std::string>("<xmlattr>.from", "");
            to_color = node.get<std::string>("<xmlattr>.to", "");
        }
        const auto color_from = parse_svg_color(from_color, Color::black());
        const auto color_to = parse_svg_color(to_color, Color::black());
        if (!color_from || !color_to) return false;
        from = {color_from->r, color_from->g, color_from->b, color_from->a};
        to = {color_to->r, color_to->g, color_to->b, color_to->a};
    } else if (const auto values = node.get_optional<std::string>("<xmlattr>.values")) {
        const auto separator = values->find(';');
        if (separator == std::string::npos || values->find(';', separator + 1) != std::string::npos ||
            !parse_values(values->substr(0, separator), from) ||
            !parse_values(values->substr(separator + 1), to)) return false;
    } else if (!parse_values(node.get<std::string>("<xmlattr>.from", ""), from) ||
               !parse_values(node.get<std::string>("<xmlattr>.to", ""), to)) return false;
    if (transform) {
        const auto type = node.get<std::string>("<xmlattr>.type", "");
        const auto expected = type == "translate" || type == "scale" ? 2U : type == "rotate" ? 1U : 0U;
        if (expected == 0 || from.size() != expected || to.size() != expected) return false;
        if (type == "scale" && (std::abs(from[0] - from[1]) > 1e-6f ||
                                 std::abs(to[0] - to[1]) > 1e-6f)) return false;
        output.kind = type == "translate" ? SvgAnimationKind::Translate :
            type == "scale" ? SvgAnimationKind::Scale : SvgAnimationKind::Rotate;
        output.components = static_cast<std::uint8_t>(expected);
    } else if (!color_property) {
        if (from.size() != 1 || to.size() != 1 || !std::isfinite(from[0]) || !std::isfinite(to[0])) return false;
        if ((opacity_property || *attribute == "opacity") &&
            (from[0] < 0.0f || from[0] > 1.0f || to[0] < 0.0f || to[0] > 1.0f)) return false;
        if (*attribute == "stroke-width" && (from[0] < 0.0f || to[0] < 0.0f || from[0] > 100000.0f || to[0] > 100000.0f)) return false;
        output.kind = paint_kind;
        output.components = 1;
    }
    if (color_property) output.components = 4;
    else if (transform) output.components = static_cast<std::uint8_t>(from.size());
    output.kind = transform ? output.kind : paint_kind;
    for (std::size_t i = 0; i < from.size() && i < 4; ++i) output.from[i] = from[i];
    for (std::size_t i = 0; i < to.size() && i < 4; ++i) output.to[i] = to[i];
    output.begin_seconds = begin;
    output.duration_seconds = duration;
    output.discrete = mode == "discrete";
    return true;
}

bool parse_svg_morph_animation(const boost::property_tree::ptree& node,
                               SvgMorphAnimationSpec& output) {
    if (node.get<std::string>("<xmlattr>.attributeName", "") != "d" ||
        node.get<std::string>("<xmlattr>.repeatCount", "1") != "1" ||
        node.get<std::string>("<xmlattr>.fill", "freeze") != "freeze" ||
        node.get<std::string>("<xmlattr>.additive", "replace") != "replace") return false;
    const auto parse_time = [](std::string text, float& seconds) {
        float scale = 1.0f;
        if (text.ends_with("ms")) { scale = 0.001f; text.resize(text.size() - 2); }
        else if (text.ends_with("s")) text.pop_back();
        try {
            std::size_t used = 0;
            seconds = std::stof(text, &used) * scale;
            return used == text.size() && std::isfinite(seconds) && seconds >= 0.0f && seconds <= 3600.0f;
        } catch (...) { return false; }
    };
    float begin = 0.0f;
    if (!parse_time(node.get<std::string>("<xmlattr>.begin", "0s"), begin) || begin != 0.0f ||
        !parse_time(node.get<std::string>("<xmlattr>.dur", ""), output.duration_seconds) ||
        output.duration_seconds <= 0.0f) return false;
    const auto mode = node.get<std::string>("<xmlattr>.calcMode", "linear");
    if (mode != "linear" && mode != "discrete") return false;
    const auto values = node.get_optional<std::string>("<xmlattr>.values");
    if (!values) return false;
    std::vector<std::string> path_values;
    std::size_t start = 0;
    while (start <= values->size()) {
        const auto separator = values->find(';', start);
        const auto end = separator == std::string::npos ? values->size() : separator;
        auto value = trim(values->substr(start, end - start));
        if (value.empty() || path_values.size() >= 256) return false;
        path_values.push_back(std::move(value));
        if (separator == std::string::npos) break;
        start = separator + 1;
    }
    if (path_values.size() < 2) return false;
    output.key_times.resize(path_values.size());
    if (const auto key_times = node.get_optional<std::string>("<xmlattr>.keyTimes")) {
        std::vector<float> parsed;
        std::istringstream input(*key_times);
        std::string item;
        while (std::getline(input, item, ';')) {
            float time = 0.0f;
            if (!parse_svg_scalar(item, 0.0f, time) || time < 0.0f || time > 1.0f) return false;
            parsed.push_back(time);
        }
        if (parsed.size() != path_values.size() || parsed.front() != 0.0f || parsed.back() != 1.0f)
            return false;
        for (std::size_t index = 1; index < parsed.size(); ++index)
            if (!(parsed[index] > parsed[index - 1])) return false;
        output.key_times = std::move(parsed);
    } else {
        for (std::size_t index = 0; index < path_values.size(); ++index)
            output.key_times[index] = static_cast<float>(index) / static_cast<float>(path_values.size() - 1);
    }
    output.paths.clear();
    output.paths.reserve(path_values.size());
    for (const auto& value : path_values) {
        auto imported = SvgImporter{}.import_path_data(value, {});
        if (!imported.ok) return false;
        output.paths.push_back(std::move(imported.path));
    }
    output.discrete = mode == "discrete";
    return true;
}

void collect_path_data(
    const boost::property_tree::ptree& tree,
    std::vector<std::pair<std::string, Affine2>>& paths,
    std::vector<SvgStyleState>& styles,
    std::vector<std::vector<SvgAnimationSpec>>& animations,
    const SvgGradientMap& gradients,
    const std::map<std::string, std::pair<std::string, const boost::property_tree::ptree*>>& ids,
    std::size_t use_depth,
    std::size_t& expanded_uses,
    std::uint32_t& isolated_group_counter,
    const Affine2& parent_transform = Affine2(1.0f),
    SvgStyleState parent_style = {}) {
    for (const auto& [name, node] : tree) {
        if (name == "animate" || name == "animateTransform" || name == "defs" || name == "pattern" ||
            name == "linearGradient" || name == "radialGradient" || name == "stop" ||
            name == "style") continue;
        auto style = parent_style;
        const std::size_t inherited_group_count = style.groups.size();
        const auto source_id = node.get_optional<std::string>("<xmlattr>.id");
        if (name == "svg" || name == "g" || name == "use") {
            if (source_id) style.group_ids.push_back(*source_id);
        } else if (name == "path" || name == "rect" || name == "circle" ||
                   name == "ellipse" || name == "line" || name == "polyline" ||
                   name == "polygon") {
            style.source_id = source_id.value_or(std::string{});
        }
        const auto properties = style_properties(node);
        if (const auto it = properties.find("display"); it != properties.end()) {
            auto value = trim(it->second);
            std::transform(value.begin(), value.end(), value.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            style.display_none = style.display_none || value == "none";
        }
        if (const auto it = properties.find("visibility"); it != properties.end()) {
            auto value = trim(it->second);
            std::transform(value.begin(), value.end(), value.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            style.visibility_hidden = value == "hidden" || value == "collapse";
        }
        if (const auto it = properties.find("color"); it != properties.end()) {
            if (const auto color = parse_svg_color(it->second, style.current_color))
                style.current_color = *color;
        }
        const Color current = style.current_color;
        if (auto it = properties.find("fill"); it != properties.end()) {
            std::string gradient_id;
            if (parse_svg_gradient_reference(it->second, gradient_id)) {
                style.fill_gradient_id = gradient_id;
                if (!gradients.contains(gradient_id)) style.fill.reset();
            } else {
                style.fill_gradient_id.clear();
                const auto color = parse_svg_color(it->second, current);
                style.fill = color ? std::optional<Fill>{Fill::solid_color(*color)} : std::nullopt;
            }
        }
        std::optional<SvgStyleState::VectorMaskScope> local_vector_mask_scope;
        for (const auto* key : {"clip-path", "mask"}) {
            if (const auto it = properties.find(key); it != properties.end()) {
                std::string reference_id;
                if (parse_svg_gradient_reference(it->second, reference_id)) {
                    style.vector_mask_id = std::move(reference_id);
                    style.vector_mask_is_mask = std::string_view(key) == "mask";
                    local_vector_mask_scope = SvgStyleState::VectorMaskScope{
                        style.vector_mask_id, style.vector_mask_is_mask, Affine2(1.0f)};
                } else {
                    style.vector_mask_id.clear();
                }
            }
        }
        if (auto it = properties.find("filter"); it != properties.end()) {
            std::string filter_id;
            if (parse_svg_gradient_reference(it->second, filter_id)) {
                style.filter_id = std::move(filter_id);
            }
        }
        if (auto it = properties.find("stroke"); it != properties.end()) {
            const auto color = parse_svg_color(it->second, current);
            style.stroke.enabled = color.has_value();
            if (color) style.stroke.color = *color;
        }
        if (auto it = properties.find("stroke-width"); it != properties.end()) {
            if (auto val = parse_svg_length(it->second)) {
                style.stroke.width = std::max(0.0f, *val);
            }
        }
        if (auto it = properties.find("paint-order"); it != properties.end()) {
            style.paint_order = trim(it->second);
        }
        if (auto it = properties.find("stroke-linecap"); it != properties.end()) {
            style.stroke.cap = it->second == "round" ? LineCap::Round
                : it->second == "square" ? LineCap::Square : LineCap::Butt;
        }
        if (auto it = properties.find("stroke-linejoin"); it != properties.end()) {
            style.stroke.join = it->second == "round" ? LineJoin::Round
                : it->second == "bevel" ? LineJoin::Bevel : LineJoin::Miter;
        }
        if (auto it = properties.find("stroke-miterlimit"); it != properties.end()) {
            try {
                style.stroke_miter_limit = std::stof(it->second);
                style.stroke.miter_limit = style.stroke_miter_limit;
            } catch (...) {}
        }
        if (auto it = properties.find("fill-rule"); it != properties.end()) {
            style.fill_rule = it->second == "nonzero" ? PathFillRule::NonZero
                : PathFillRule::EvenOdd;
        }
        if (auto it = properties.find("stroke-dasharray"); it != properties.end()) {
            if (trim(it->second) == "none") {
                style.stroke.dash_array.clear();
            } else {
                style.stroke.dash_array = transform_numbers(it->second);
                if (style.stroke.dash_array.size() % 2 != 0) {
                    const auto original = style.stroke.dash_array;
                    style.stroke.dash_array.insert(style.stroke.dash_array.end(),
                        original.begin(), original.end());
                }
            }
        }
        if (auto it = properties.find("stroke-dashoffset"); it != properties.end()) {
            try { style.stroke.dash_offset = std::stof(it->second); } catch (...) {}
        }
        const float element_opacity = style_number(properties, "opacity", 1.0f);
        if (name == "g" && element_opacity < 1.0f) {
            // Preserve every isolated ancestor so the renderer can composite
            // nested group surfaces from the inside out.
            style.groups.push_back(SvgStyleGroupRef{++isolated_group_counter,
                                                     element_opacity});
        } else {
            style.opacity *= element_opacity;
        }
        style.fill_opacity *= style_number(properties, "fill-opacity", 1.0f);
        style.stroke_opacity *= style_number(properties, "stroke-opacity", 1.0f);
        const auto local_transform = node.get_optional<std::string>("<xmlattr>.transform");
        Affine2 node_transform = local_transform ? parse_transform(*local_transform) : Affine2(1.0f);
        if (name == "svg") {
            if (auto viewbox_transform = parse_viewbox_transform(node)) {
                node_transform = node_transform * *viewbox_transform;
            }
        }
        const Affine2 transform = parent_transform * node_transform;
        if (local_vector_mask_scope) {
            local_vector_mask_scope->transform = transform;
            style.vector_mask_scopes.push_back(*local_vector_mask_scope);
        }
        if (name == "g") {
            for (const auto& child : node) {
                if (child.first != "animate" && child.first != "animateTransform") continue;
                SvgAnimationSpec animation;
                if (style.inherited_animations.size() >= 9 ||
                    !parse_svg_animation(child.first, child.second, animation)) {
                    style.inherited_animations.clear();
                    style.inherited_animations.push_back(SvgAnimationSpec{});
                    style.inherited_animations.back().duration_seconds = -1.0f;
                    break;
                }
                if (animation.kind == SvgAnimationKind::Opacity) {
                    if (style.groups.size() == inherited_group_count)
                        style.groups.push_back(SvgStyleGroupRef{
                            ++isolated_group_counter, element_opacity, std::nullopt});
                    if (!style.groups.back().opacity_animation)
                        style.groups.back().opacity_animation = animation;
                    else {
                        style.inherited_animations.clear();
                        style.inherited_animations.push_back(SvgAnimationSpec{});
                        style.inherited_animations.back().duration_seconds = -1.0f;
                        break;
                    }
                    continue;
                }
                style.inherited_animations.push_back(animation);
            }
        }
        if (name == "use") {
            const auto href = node.get<std::string>("<xmlattr>.href", "");
            const auto target = ids.find(href.size() > 1 ? href.substr(1) : std::string{});
            if (target == ids.end() || use_depth >= 16 || expanded_uses >= 4096 ||
                target->second.first == "use" || target->second.first == "defs" ||
                target->second.first == "linearGradient" || target->second.first == "radialGradient" ||
                target->second.first == "stop") {
                if (!style.display_none && !style.visibility_hidden) {
                    paths.emplace_back(std::string{}, transform);
                    styles.push_back(style);
                    animations.emplace_back(1, SvgAnimationSpec{});
                    animations.back().front().duration_seconds = -1.0f;
                }
                continue;
            }
            ++expanded_uses;
            const float x = node.get<float>("<xmlattr>.x", 0.0f);
            const float y = node.get<float>("<xmlattr>.y", 0.0f);
            std::ostringstream translation;
            translation << "translate(" << x << ' ' << y << ')';
            const Affine2 use_transform = transform * parse_transform(translation.str());
            boost::property_tree::ptree wrapper;
            wrapper.add_child(target->second.first, *target->second.second);
            collect_path_data(wrapper, paths, styles, animations, gradients, ids,
                use_depth + 1, expanded_uses, isolated_group_counter, use_transform, style);
            continue;
        }
        if (name == "path") {
            if (auto d = node.get_optional<std::string>("<xmlattr>.d");
                d && !style.display_none && !style.visibility_hidden) {
                paths.emplace_back(*d, transform); styles.push_back(style);
                animations.emplace_back(style.inherited_animations);
                for (const auto& child : node) {
                    if (child.first != "animate" && child.first != "animateTransform") continue;
                    if (child.first == "animate" &&
                        child.second.get<std::string>("<xmlattr>.attributeName", "") == "d") {
                        SvgMorphAnimationSpec morph;
                        if (style.morph_animation || !parse_svg_morph_animation(child.second, morph)) {
                            animations.back().push_back(SvgAnimationSpec{});
                            animations.back().back().duration_seconds = -1.0f;
                            break;
                        }
                        style.morph_animation = std::move(morph);
                        continue;
                    }
                    SvgAnimationSpec animation;
                    if (animations.back().size() >= 9 || !parse_svg_animation(child.first, child.second, animation)) {
                        animations.back().clear();
                        animations.back().push_back(SvgAnimationSpec{});
                        animations.back().back().duration_seconds = -1.0f;
                        break;
                    }
                    animations.back().push_back(animation);
                }
                const Affine2 identity(1.0f);
                const bool has_static_transform = [&] {
                    for (int column = 0; column < 3; ++column)
                        for (int row = 0; row < 3; ++row)
                            if (std::abs(transform[column][row] - identity[column][row]) > 1e-6f)
                                return true;
                    return false;
                }();
                if (has_static_transform && !animations.back().empty()) {
                    animations.back().clear();
                    animations.back().push_back(SvgAnimationSpec{});
                    animations.back().back().duration_seconds = -1.0f;
                }
                styles.back().morph_animation = std::move(style.morph_animation);
            }
        } else if (name == "rect" || name == "circle" || name == "ellipse" ||
                   name == "line" || name == "polyline" || name == "polygon") {
            if (auto d = primitive_path_data(name, node);
                !d.empty() && !style.display_none && !style.visibility_hidden) {
                paths.emplace_back(std::move(d), transform); styles.push_back(style);
                animations.emplace_back(style.inherited_animations);
            }
        } else if (name == "image") {
            const auto attr = [&node](const char* key, float fallback = 0.0f) {
                if (const auto opt = node.get_optional<std::string>(std::string{"<xmlattr>."} + key)) {
                    if (const auto parsed = parse_svg_length(*opt)) return *parsed;
                }
                return fallback;
            };
            const float x = attr("x", 0.0f);
            const float y = attr("y", 0.0f);
            const float width = attr("width", 0.0f);
            const float height = attr("height", 0.0f);
            const auto href = node.get_optional<std::string>("<xmlattr>.href");
            const auto xlink_href = node.get_optional<std::string>("<xmlattr>.{http://www.w3.org/1999/xlink}href");
            const std::string img_href = href ? *href : (xlink_href ? *xlink_href : "");
            if (width > 0.0f && height > 0.0f && img_href.starts_with("data:image/") &&
                !style.display_none && !style.visibility_hidden) {
                const auto comma = img_href.find(',');
                if (comma != std::string::npos) {
                    const std::string_view base64_str = std::string_view(img_href).substr(comma + 1);
                    const auto bytes = decode_base64_data(base64_str);
                    int img_w = 0, img_h = 0, channels = 0;
                    auto* pixels = stbi_load_from_memory(
                        reinterpret_cast<const stbi_uc*>(bytes.data()),
                        static_cast<int>(bytes.size()), &img_w, &img_h, &channels, 4);
                    if (pixels) {
                        std::ostringstream rect_d;
                        rect_d << std::setprecision(9)
                               << "M " << x << ' ' << y
                               << " H " << x + width << " V " << y + height
                               << " H " << x << " Z";
                        Color pixel_color{
                            pixels[0] / 255.0f,
                            pixels[1] / 255.0f,
                            pixels[2] / 255.0f,
                            (pixels[3] / 255.0f) * style.opacity
                        };
                        style.fill = Fill::solid_color(pixel_color);
                        style.stroke.enabled = false;
                        paths.emplace_back(rect_d.str(), transform);
                        styles.push_back(style);
                        animations.emplace_back(style.inherited_animations);
                        stbi_image_free(pixels);
                    }
                }
            }
        }
        collect_path_data(node, paths, styles, animations, gradients, ids,
                          use_depth, expanded_uses, isolated_group_counter,
                          transform, style);
    }
}

struct SvgTextStyleState {
    std::string font_family{"serif"};
    float font_size{16.0f};
    int font_weight{400};
    std::string font_style{"normal"};
    std::string direction{"auto"};
    std::string text_anchor{"start"};
    std::string dominant_baseline{"auto"};
    float baseline_shift{0.0f};
    std::optional<std::string> text_path_id;
    float text_path_start_offset{0.0f};
    bool text_path_start_offset_percent{false};
    std::optional<PathShape> text_path_geometry;
    Color fill{Color::black()};
    std::optional<graphics::GradientDefinition> fill_gradient;
    std::shared_ptr<const graphics::PatternTile> fill_pattern;
    std::optional<Color> stroke;
    std::optional<graphics::GradientDefinition> stroke_gradient;
    std::shared_ptr<const graphics::PatternTile> stroke_pattern;
    float stroke_width{1.0f};
    LineCap stroke_cap{LineCap::Butt};
    LineJoin stroke_join{LineJoin::Miter};
    float stroke_miter_limit{4.0f};
    std::vector<float> stroke_dash_array;
    float stroke_dash_offset{0.0f};
    Color current_color{Color::black()};
    float opacity{1.0f};
    float fill_opacity{1.0f};
    float stroke_opacity{1.0f};
    bool display_none{false};
    bool visibility_hidden{false};
    std::optional<float> x;
    std::optional<float> y;
    float dx{0.0f};
    float dy{0.0f};
    std::vector<float> x_coordinates;
    std::vector<float> y_coordinates;
    std::vector<float> dx_coordinates;
    std::vector<float> dy_coordinates;
    std::vector<float> rotate_degrees;
    float letter_spacing{0.0f};
    float word_spacing{0.0f};
    std::optional<float> text_length;
    std::string length_adjust{"spacing"};
    std::string text_anchor_scope_id;
    std::vector<std::string> group_ids;
    std::vector<std::string> coordinate_scope_ids;
};

std::vector<float> svg_text_lengths(const boost::property_tree::ptree& node,
                                    const char* key) {
    auto value = node.get_optional<std::string>(std::string("<xmlattr>.") + key);
    if (!value) return {};
    std::string list = *value;
    std::replace(list.begin(), list.end(), ',', ' ');
    std::istringstream input(list);
    std::vector<float> result;
    std::string token;
    while (input >> token) {
        if (token.ends_with("px")) token.resize(token.size() - 2);
        try {
            std::size_t used = 0;
            const float parsed = std::stof(token, &used);
            if (used != token.size() || !std::isfinite(parsed) || std::abs(parsed) > 100000.0f)
                throw std::invalid_argument("invalid SVG text coordinate");
            result.push_back(parsed);
        } catch (...) {
            throw std::invalid_argument(std::string("SVG text ") + key +
                " must contain finite bounded pixel values");
        }
        if (result.size() > 4096)
            throw std::invalid_argument(std::string("SVG text ") + key +
                " exceeds the 4096-coordinate limit");
    }
    if (result.empty())
        throw std::invalid_argument(std::string("SVG text ") + key + " coordinate list is empty");
    return result;
}

std::vector<float> svg_text_angles(const boost::property_tree::ptree& node) {
    const auto value = node.get_optional<std::string>("<xmlattr>.rotate");
    if (!value) return {};
    std::string list = *value;
    std::replace(list.begin(), list.end(), ',', ' ');
    std::istringstream input(list);
    std::vector<float> result;
    std::string token;
    while (input >> token) {
        if (token.ends_with("deg")) token.resize(token.size() - 3);
        try {
            std::size_t used = 0;
            const float parsed = std::stof(token, &used);
            if (used != token.size() || !std::isfinite(parsed) || std::abs(parsed) > 36000.0f)
                throw std::invalid_argument("invalid SVG text rotate angle");
            result.push_back(parsed);
        } catch (...) {
            throw std::invalid_argument("SVG text rotate values must be bounded degrees");
        }
        if (result.size() > 4096)
            throw std::invalid_argument("SVG text rotate list exceeds the 4096-angle limit");
    }
    if (result.empty())
        throw std::invalid_argument("SVG text rotate list is empty");
    return result;
}

float svg_text_spacing(const std::string& source, float font_size) {
    auto value = trim(source);
    if (value == "normal") return 0.0f;
    float factor = 1.0f;
    if (value.ends_with("px")) value.resize(value.size() - 2);
    else if (value.ends_with("em")) {
        value.resize(value.size() - 2);
        factor = font_size;
    }
    try {
        std::size_t used = 0;
        const float parsed = std::stof(value, &used);
        if (used != value.size() || !std::isfinite(parsed) || std::abs(parsed) > 100000.0f)
            throw std::invalid_argument("invalid SVG text spacing");
        return parsed * factor;
    } catch (...) {
        throw std::invalid_argument("SVG text spacing must be a finite bounded px or em value");
    }
}

std::optional<float> svg_text_length(const boost::property_tree::ptree& node,
                                     const char* key) {
    const auto values = svg_text_lengths(node, key);
    if (values.empty()) return std::nullopt;
    if (values.size() != 1)
        throw std::invalid_argument(std::string("SVG text ") + key +
            " must contain exactly one length");
    return values.front();
}

std::optional<std::string> svg_text_path_reference(
    const boost::property_tree::ptree& node) {
    auto href = node.get_optional<std::string>("<xmlattr>.href");
    if (!href) href = node.get_optional<std::string>("<xmlattr>.xlink:href");
    if (!href) href = node.get_optional<std::string>(
        "<xmlattr>.{http://www.w3.org/1999/xlink}href");
    if (!href || href->size() < 2 || href->front() != '#' || href->size() > 129)
        return std::nullopt;
    const auto id = href->substr(1);
    if (id.find_first_of(" \t\r\n#") != std::string::npos) return std::nullopt;
    return id;
}

std::optional<std::pair<float, bool>> svg_text_path_offset(
    const boost::property_tree::ptree& node) {
    const auto raw = node.get_optional<std::string>("<xmlattr>.startOffset");
    if (!raw) return std::pair{0.0f, false};
    auto value = trim(*raw);
    const bool percent = value.ends_with('%');
    if (percent) value.pop_back();
    try {
        std::size_t used = 0;
        const float parsed = std::stof(value, &used);
        if (used != value.size() || !std::isfinite(parsed) || std::abs(parsed) > 100000.0f)
            return std::nullopt;
        return std::pair{parsed, percent};
    } catch (...) {
        return std::nullopt;
    }
}

std::string svg_font_family(const std::string& authored) {
    auto value = trim(authored);
    const auto comma = value.find(',');
    if (comma != std::string::npos) value = trim(value.substr(0, comma));
    if (value.size() >= 2 &&
        ((value.front() == '\'' && value.back() == '\'') ||
         (value.front() == '"' && value.back() == '"')))
        value = value.substr(1, value.size() - 2);
    return value;
}

std::string svg_color_hex(const Color& color) {
    std::ostringstream out;
    out << '#' << std::uppercase << std::hex << std::setfill('0')
        << std::setw(2) << static_cast<int>(std::round(std::clamp(color.r, 0.0f, 1.0f) * 255.0f))
        << std::setw(2) << static_cast<int>(std::round(std::clamp(color.g, 0.0f, 1.0f) * 255.0f))
        << std::setw(2) << static_cast<int>(std::round(std::clamp(color.b, 0.0f, 1.0f) * 255.0f));
    return out.str();
}

void apply_svg_text_style(const boost::property_tree::ptree& node,
                          SvgTextStyleState& state,
                          const SvgGradientMap& gradients) {
    const auto properties = style_properties(node);
    if (const auto it = properties.find("display"); it != properties.end()) {
        auto value = trim(it->second);
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        state.display_none = state.display_none || value == "none";
    }
    if (const auto it = properties.find("visibility"); it != properties.end()) {
        auto value = trim(it->second);
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        state.visibility_hidden = value == "hidden" || value == "collapse";
    }
    if (const auto it = properties.find("font-family"); it != properties.end()) {
        state.font_family = svg_font_family(it->second);
        if (state.font_family.empty() || state.font_family.size() > 256)
            throw std::invalid_argument("SVG font-family is empty or exceeds 256 bytes");
    }
    if (const auto it = properties.find("font-size"); it != properties.end()) {
        auto value = trim(it->second);
        if (value.ends_with("px")) value.resize(value.size() - 2);
        state.font_size = std::stof(value);
    }
    if (const auto it = properties.find("letter-spacing"); it != properties.end())
        state.letter_spacing = svg_text_spacing(it->second, state.font_size);
    if (const auto it = properties.find("word-spacing"); it != properties.end())
        state.word_spacing = svg_text_spacing(it->second, state.font_size);
    if (node.get_optional<std::string>("<xmlattr>.rotate"))
        state.rotate_degrees = svg_text_angles(node);
    if (node.get_optional<std::string>("<xmlattr>.textLength")) {
        state.text_length = svg_text_length(node, "textLength");
        if (!state.text_length || *state.text_length <= 0.0f)
            throw std::invalid_argument("SVG textLength must be a positive finite pixel value");
        state.length_adjust = node.get<std::string>("<xmlattr>.lengthAdjust", "spacing");
        if (state.length_adjust != "spacing" && state.length_adjust != "spacingAndGlyphs")
            throw std::invalid_argument("SVG lengthAdjust supports spacing or spacingAndGlyphs");
    }
    if (const auto it = properties.find("font-weight"); it != properties.end()) {
        if (it->second == "normal") state.font_weight = 400;
        else if (it->second == "bold") state.font_weight = 700;
        else state.font_weight = std::stoi(it->second);
    }
    if (const auto it = properties.find("font-style"); it != properties.end())
        state.font_style = it->second;
    if (const auto it = properties.find("direction"); it != properties.end())
        state.direction = it->second;
    if (const auto it = properties.find("writing-mode"); it != properties.end() &&
        it->second != "horizontal-tb" && it->second != "lr-tb")
        throw std::invalid_argument("vertical SVG text writing modes are not supported by the bounded importer");
    if (const auto it = properties.find("text-anchor"); it != properties.end())
        state.text_anchor = it->second;
    if (const auto it = properties.find("dominant-baseline"); it != properties.end() &&
        it->second != "no-change" && it->second != "reset-size")
        state.dominant_baseline = it->second;
    if (const auto it = properties.find("color"); it != properties.end())
        if (const auto color = parse_svg_color(it->second, state.current_color))
            state.current_color = *color;
    if (const auto it = properties.find("fill"); it != properties.end()) {
        std::string gradient_id;
        const bool gradient_reference = parse_svg_gradient_reference(it->second, gradient_id);
        if (trim(it->second).starts_with("url(") && !gradient_reference)
            throw std::invalid_argument("SVG text fill requires a local gradient or supported pattern reference");
        if (gradient_reference) {
            const auto gradient = gradients.find(gradient_id);
            if (gradient == gradients.end())
                throw std::invalid_argument("SVG text fill references an unknown gradient");
            const auto& source = gradient->second;
            if (source.pattern_tile) {
                state.fill_pattern = source.pattern_tile;
                state.fill_gradient.reset();
            } else {
            if (!source.animations.empty())
                throw std::invalid_argument("animated gradient stops in SVG text paint are not supported");
            const Vec2 origin = transform_point(source.transform, {0.0f, 0.0f});
            const Vec2 axis_x = transform_point(source.transform, {1.0f, 0.0f}) - origin;
            const Vec2 axis_y = transform_point(source.transform, {0.0f, 1.0f}) - origin;
            if (source.radial) {
                const float sx = glm::length(axis_x), sy = glm::length(axis_y);
                if (sx <= 0.0f || sy <= 0.0f ||
                    std::abs(sx - sy) > 1.0e-5f * std::max(sx, sy) ||
                    std::abs(glm::dot(axis_x, axis_y)) > 1.0e-5f * sx * sy)
                    throw std::invalid_argument("SVG text radial gradientTransform must preserve circular geometry");
                state.fill_gradient = graphics::GradientDefinition::radial(
                    transform_point(source.transform, source.p0), source.radius * sx,
                    source.stops);
            } else {
                state.fill_gradient = graphics::GradientDefinition::linear(
                    transform_point(source.transform, source.p0),
                    transform_point(source.transform, source.p1), source.stops);
            }
            state.fill_pattern.reset();
            }
        } else if (const auto color = parse_svg_color(it->second, state.current_color)) {
            state.fill_gradient.reset();
            state.fill_pattern.reset();
            state.fill = *color;
        } else {
            state.fill_gradient.reset();
            state.fill_pattern.reset();
            state.fill.a = 0.0f;
        }
    }
    if (const auto it = properties.find("stroke"); it != properties.end()) {
        std::string gradient_id;
        const bool gradient_reference = parse_svg_gradient_reference(it->second, gradient_id);
        if (trim(it->second).starts_with("url(") && !gradient_reference)
            throw std::invalid_argument("SVG text stroke requires a local gradient or supported pattern reference");
        if (gradient_reference) {
            const auto gradient = gradients.find(gradient_id);
            if (gradient == gradients.end())
                throw std::invalid_argument("SVG text stroke references an unknown gradient");
            const auto& source = gradient->second;
            if (source.pattern_tile) {
                state.stroke_pattern = source.pattern_tile;
                state.stroke_gradient.reset();
                state.stroke = Color::black();
            } else {
            if (!source.animations.empty())
                throw std::invalid_argument("animated gradient stops in SVG text paint are not supported");
            const Vec2 origin = transform_point(source.transform, {0.0f, 0.0f});
            const Vec2 axis_x = transform_point(source.transform, {1.0f, 0.0f}) - origin;
            const Vec2 axis_y = transform_point(source.transform, {0.0f, 1.0f}) - origin;
            if (source.radial) {
                const float sx = glm::length(axis_x), sy = glm::length(axis_y);
                if (sx <= 0.0f || sy <= 0.0f ||
                    std::abs(sx - sy) > 1.0e-5f * std::max(sx, sy) ||
                    std::abs(glm::dot(axis_x, axis_y)) > 1.0e-5f * sx * sy)
                    throw std::invalid_argument("SVG text radial stroke gradientTransform must preserve circular geometry");
                state.stroke_gradient = graphics::GradientDefinition::radial(
                    transform_point(source.transform, source.p0), source.radius * sx,
                    source.stops);
            } else {
                state.stroke_gradient = graphics::GradientDefinition::linear(
                    transform_point(source.transform, source.p0),
                    transform_point(source.transform, source.p1), source.stops);
            }
            state.stroke = Color::black();
            state.stroke_pattern.reset();
            }
        } else if (trim(it->second) == "none") {
            state.stroke_gradient.reset();
            state.stroke_pattern.reset();
            state.stroke.reset();
        } else if (const auto color = parse_svg_color(it->second, state.current_color)) {
            state.stroke_gradient.reset();
            state.stroke_pattern.reset();
            state.stroke = *color;
        } else {
            throw std::invalid_argument("SVG text stroke must be a solid color or none");
        }
    }
    if (const auto it = properties.find("stroke-width"); it != properties.end()) {
        auto value = trim(it->second);
        if (value.ends_with("px")) value.resize(value.size() - 2);
        try {
            std::size_t used = 0;
            const float width = std::stof(value, &used);
            if (used != value.size() || !std::isfinite(width) || width < 0.0f || width > 100000.0f)
                throw std::invalid_argument("invalid stroke width");
            state.stroke_width = width;
        } catch (...) {
            throw std::invalid_argument("SVG text stroke-width must be a finite bounded pixel value");
        }
    }
    if (const auto it = properties.find("stroke-linecap"); it != properties.end()) {
        state.stroke_cap = it->second == "round" ? LineCap::Round
            : it->second == "square" ? LineCap::Square : LineCap::Butt;
    }
    if (const auto it = properties.find("stroke-linejoin"); it != properties.end()) {
        state.stroke_join = it->second == "round" ? LineJoin::Round
            : it->second == "bevel" ? LineJoin::Bevel : LineJoin::Miter;
    }
    if (const auto it = properties.find("stroke-miterlimit"); it != properties.end()) {
        state.stroke_miter_limit = std::stof(it->second);
    }
    if (const auto it = properties.find("stroke-dasharray"); it != properties.end()) {
        state.stroke_dash_array = trim(it->second) == "none"
            ? std::vector<float>{} : transform_numbers(it->second);
        if (state.stroke_dash_array.size() % 2 != 0) {
            const auto original = state.stroke_dash_array;
            state.stroke_dash_array.insert(state.stroke_dash_array.end(),
                original.begin(), original.end());
        }
    }
    if (const auto it = properties.find("stroke-dashoffset"); it != properties.end())
        state.stroke_dash_offset = std::stof(it->second);
    if (const auto it = properties.find("baseline-shift"); it != properties.end()) {
        const auto shift = svg_baseline_shift(it->second, state.font_size);
        if (!shift) throw std::invalid_argument("SVG baseline-shift supports baseline, sub, super, px, em, or percent values");
        state.baseline_shift += *shift;
    }
    if (node.get_optional<std::string>("<xmlattr>.href") ||
        node.get_optional<std::string>("<xmlattr>.xlink:href") ||
        node.get_optional<std::string>(
            "<xmlattr>.{http://www.w3.org/1999/xlink}href")) {
        const auto reference = svg_text_path_reference(node);
        const auto offset = svg_text_path_offset(node);
        if (!reference || !offset)
            throw std::invalid_argument("SVG textPath requires a local href and valid startOffset");
        state.text_path_id = *reference;
        state.text_path_start_offset = offset->first;
        state.text_path_start_offset_percent = offset->second;
    }
    state.opacity *= style_number(properties, "opacity", 1.0f);
    state.fill_opacity *= style_number(properties, "fill-opacity", 1.0f);
    state.stroke_opacity *= style_number(properties, "stroke-opacity", 1.0f);
    if (node.get_optional<std::string>("<xmlattr>.x")) {
        state.x_coordinates = svg_text_lengths(node, "x");
        state.x = state.x_coordinates.front();
    }
    if (node.get_optional<std::string>("<xmlattr>.y")) {
        state.y_coordinates = svg_text_lengths(node, "y");
        state.y = state.y_coordinates.front();
    }
    if (node.get_optional<std::string>("<xmlattr>.dx")) {
        state.dx_coordinates = svg_text_lengths(node, "dx");
        state.dx += state.dx_coordinates.front();
    }
    if (node.get_optional<std::string>("<xmlattr>.dy")) {
        state.dy_coordinates = svg_text_lengths(node, "dy");
        state.dy += state.dy_coordinates.front();
    }
}

void collect_svg_text_runs(
    const SvgIdMap& ids,
    const boost::property_tree::ptree& tree,
    std::vector<SvgTextRun>& runs,
    const SvgGradientMap& gradients,
    const Affine2& parent_transform = Affine2(1.0f),
    SvgTextStyleState parent_style = {}) {
    for (const auto& [name, node] : tree) {
        if (name == "<xmlattr>" || name == "<xmlcomment>" || name == "<xmltext>" ||
            name == "style" || name == "defs" || name == "linearGradient" ||
            name == "radialGradient" || name == "stop" || name == "pattern" || name == "animate" ||
            name == "animateTransform" || name == "use") continue;
        auto style = parent_style;
        if (name == "svg" || name == "g" || name == "text")
            apply_svg_text_style(node, style, gradients);
        if (name == "svg" || name == "g") {
            if (const auto id = node.get_optional<std::string>("<xmlattr>.id"))
                style.group_ids.push_back(*id);
            if (const auto id = node.get_optional<std::string>("<xmlattr>.id"))
                style.coordinate_scope_ids.push_back(*id);
        }
        const auto local_transform = node.get_optional<std::string>("<xmlattr>.transform");
        Affine2 local = local_transform ? parse_transform(*local_transform) : Affine2(1.0f);
        if (name == "svg")
            if (auto viewbox = parse_viewbox_transform(node)) local = local * *viewbox;
        const auto transform = parent_transform * local;
        if (name == "text") {
            const auto text_id = node.get<std::string>("<xmlattr>.id", "");
            const auto text_group_id = text_id.empty()
                ? "__svg_text_" + std::to_string(runs.size()) : text_id;
            auto text_style = style;
            if (!text_id.empty()) text_style.group_ids.push_back(text_id);
            text_style.text_anchor_scope_id = text_group_id;
            bool root_x_pending = node.get_optional<std::string>("<xmlattr>.x").has_value();
            bool root_y_pending = node.get_optional<std::string>("<xmlattr>.y").has_value();
            std::function<void(const boost::property_tree::ptree&, SvgTextStyleState,
                               std::string, bool, bool, bool)> visit_text;
            visit_text = [&](const auto& text_node, SvgTextStyleState current,
                             std::string own_id, bool x_explicit, bool y_explicit,
                             bool shaping_mergeable) {
                if (current.text_length || !current.rotate_degrees.empty()) {
                    for (const auto& [child_name, child] : text_node) {
                        (void)child;
                        if (child_name == "tspan" || child_name == "textPath")
                            throw std::invalid_argument(
                                "bounded SVG textLength/rotate currently requires a direct text run without nested spans or textPath");
                    }
                }
                bool local_x_pending = x_explicit;
                bool local_y_pending = y_explicit;
                const auto emit_run = [&](const std::string& value,
                                          const SvgTextStyleState& run_style,
                                          const std::string& run_id,
                                          bool shaping_mergeable) {
                    if (trim(value).empty() || run_style.display_none ||
                        run_style.visibility_hidden) return;
                    const bool has_x = local_x_pending || root_x_pending;
                    const bool has_y = local_y_pending || root_y_pending;
                    const auto origin = transform_point(transform, Vec2{0.0f, 0.0f});
                    const auto point = transform_point(transform, Vec2{
                        has_x ? run_style.x.value_or(0.0f) : 0.0f,
                        has_y ? run_style.y.value_or(0.0f) : 0.0f});
                    const auto delta = transform_point(transform,
                        Vec2{run_style.dx, run_style.dy}) - origin;
                    SvgTextRun run;
                    run.text = value;
                    run.shaping_mergeable = shaping_mergeable;
                    run.text_element_id = text_group_id;
                    run.source_id = run_id.empty() ? text_id : run_id;
                    run.group_ids = run_style.group_ids;
                    run.coordinate_scope_ids = run_style.coordinate_scope_ids;
                    run.text_anchor_scope_id = run_style.text_anchor_scope_id;
                    const auto transform_list = [&](const std::vector<float>& values,
                                                    bool horizontal, bool relative) {
                        std::vector<float> mapped;
                        mapped.reserve(values.size());
                        for (const float value : values) {
                            const Vec2 source = horizontal ? Vec2{value, 0.0f} : Vec2{0.0f, value};
                            const Vec2 result = transform_point(transform, source) -
                                (relative ? origin : Vec2{0.0f, 0.0f});
                            mapped.push_back(horizontal ? result.x : result.y);
                        }
                        return mapped;
                    };
                    const bool has_position_lists = run_style.x_coordinates.size() > 1 ||
                        run_style.y_coordinates.size() > 1 ||
                        run_style.dx_coordinates.size() > 1 ||
                        run_style.dy_coordinates.size() > 1;
                    if (has_position_lists) {
                        const bool rotated_or_skewed = std::abs(transform[1][0]) > 1.0e-6f ||
                            std::abs(transform[0][1]) > 1.0e-6f;
                        if (rotated_or_skewed) {
                            if ((run_style.x_coordinates.size() > 1 ||
                                 run_style.y_coordinates.size() > 1) &&
                                (run_style.x_coordinates.size() != run_style.y_coordinates.size() ||
                                 run_style.x_coordinates.size() < 2))
                                throw std::invalid_argument(
                                    "rotated/skewed SVG text needs paired x/y coordinate lists");
                            if ((run_style.dx_coordinates.size() > 1 ||
                                 run_style.dy_coordinates.size() > 1) &&
                                (run_style.dx_coordinates.size() != run_style.dy_coordinates.size() ||
                                 run_style.dx_coordinates.size() < 2))
                                throw std::invalid_argument(
                                    "rotated/skewed SVG text needs paired dx/dy coordinate lists");
                            for (std::size_t index = 0; index < run_style.x_coordinates.size(); ++index) {
                                const auto point = transform_point(transform,
                                    {run_style.x_coordinates[index], run_style.y_coordinates[index]});
                                run.transformed_position_coordinates.push_back({point.x, point.y});
                            }
                            for (std::size_t index = 0; index < run_style.dx_coordinates.size(); ++index) {
                                const auto delta = transform_point(transform,
                                    {run_style.dx_coordinates[index], run_style.dy_coordinates[index]}) - origin;
                                run.transformed_delta_coordinates.push_back({delta.x, delta.y});
                            }
                        } else {
                            run.x_coordinates = transform_list(run_style.x_coordinates, true, false);
                            run.y_coordinates = transform_list(run_style.y_coordinates, false, false);
                            run.dx_coordinates = transform_list(run_style.dx_coordinates, true, true);
                            run.dy_coordinates = transform_list(run_style.dy_coordinates, false, true);
                        }
                    }
                    run.x_explicit = has_x;
                    run.y_explicit = has_y;
                    if (has_x) run.x = point.x;
                    if (has_y) run.y = point.y;
                    run.dx = delta.x;
                    run.dy = delta.y;
                    run.rotate_degrees.reserve(run_style.rotate_degrees.size());
                    for (const float degrees : run_style.rotate_degrees) {
                        const float radians = degrees * 0.017453292519943295f;
                        const auto origin = transform_point(transform, Vec2{0.0f, 0.0f});
                        const auto direction = transform_point(transform,
                            Vec2{std::cos(radians), std::sin(radians)}) - origin;
                        run.rotate_degrees.push_back(std::atan2(direction.y, direction.x) *
                            57.29577951308232f);
                    }
                    run.letter_spacing = run_style.letter_spacing;
                    run.word_spacing = run_style.word_spacing;
                    run.text_length = run_style.text_length;
                    run.length_adjust = run_style.length_adjust;
                    run.font_family = run_style.font_family;
                    run.font_size = run_style.font_size;
                    run.font_weight = run_style.font_weight;
                    run.font_style = run_style.font_style;
                    run.direction = run_style.direction;
                    run.text_anchor = run_style.text_anchor;
                    run.dominant_baseline = run_style.dominant_baseline;
                    run.baseline_shift = (transform_point(transform,
                        Vec2{0.0f, run_style.baseline_shift}) - origin).y;
                    run.text_path_id = run_style.text_path_id;
                    run.text_path_geometry = run_style.text_path_geometry;
                    run.text_path_start_offset = run_style.text_path_start_offset;
                    run.text_path_start_offset_percent = run_style.text_path_start_offset_percent;
                    run.fill = svg_color_hex(run_style.fill);
                    run.fill_gradient = run_style.fill_gradient;
                    run.fill_pattern = run_style.fill_pattern;
                    run.fill_opacity = std::clamp(run_style.fill_opacity * run_style.fill.a,
                                                  0.0f, 1.0f);
                    if (run_style.stroke)
                        run.stroke = svg_color_hex(*run_style.stroke);
                    run.stroke_gradient = run_style.stroke_gradient;
                    run.stroke_pattern = run_style.stroke_pattern;
                    run.stroke_width = run_style.stroke_width;
                    run.stroke_cap = run_style.stroke_cap;
                    run.stroke_join = run_style.stroke_join;
                    run.stroke_miter_limit = run_style.stroke_miter_limit;
                    run.stroke_dash_array = run_style.stroke_dash_array;
                    run.stroke_dash_offset = run_style.stroke_dash_offset;
                    run.stroke_opacity = run_style.stroke
                        ? std::clamp(run_style.stroke_opacity * run_style.stroke->a, 0.0f, 1.0f)
                        : 1.0f;
                    run.opacity = std::clamp(run_style.opacity, 0.0f, 1.0f);
                    const auto no_positioning = [](const SvgTextRun& candidate) {
                        return !candidate.x_explicit && !candidate.y_explicit &&
                            candidate.dx == 0.0f && candidate.dy == 0.0f &&
                            candidate.rotate_degrees.empty() &&
                            candidate.letter_spacing == 0.0f && candidate.word_spacing == 0.0f &&
                            !candidate.text_length &&
                            candidate.x_coordinates.empty() && candidate.y_coordinates.empty() &&
                            candidate.dx_coordinates.empty() && candidate.dy_coordinates.empty() &&
                            candidate.transformed_position_coordinates.empty() &&
                            candidate.transformed_delta_coordinates.empty() &&
                            !candidate.text_path_id;
                    };
                    if (!runs.empty() && run.shaping_mergeable &&
                        runs.back().shaping_mergeable && no_positioning(run) &&
                        no_positioning(runs.back()) &&
                        runs.back().text_element_id == run.text_element_id &&
                        runs.back().group_ids == run.group_ids &&
                        runs.back().coordinate_scope_ids == run.coordinate_scope_ids &&
                        runs.back().text_anchor_scope_id == run.text_anchor_scope_id &&
                        runs.back().font_family == run.font_family &&
                        runs.back().font_size == run.font_size &&
                        runs.back().font_weight == run.font_weight &&
                        runs.back().font_style == run.font_style &&
                        runs.back().direction == run.direction &&
                        runs.back().text_anchor == run.text_anchor &&
                        runs.back().dominant_baseline == run.dominant_baseline &&
                        runs.back().baseline_shift == run.baseline_shift &&
                        runs.back().rotate_degrees == run.rotate_degrees &&
                        runs.back().letter_spacing == run.letter_spacing &&
                        runs.back().word_spacing == run.word_spacing &&
                        runs.back().text_length == run.text_length &&
                        runs.back().length_adjust == run.length_adjust &&
                        runs.back().fill == run.fill &&
                        !runs.back().fill_gradient && !run.fill_gradient &&
                        !runs.back().fill_pattern && !run.fill_pattern &&
                        runs.back().fill_opacity == run.fill_opacity &&
                        runs.back().stroke == run.stroke &&
                        !runs.back().stroke_gradient && !run.stroke_gradient &&
                        !runs.back().stroke_pattern && !run.stroke_pattern &&
                        runs.back().stroke_width == run.stroke_width &&
                        runs.back().stroke_cap == run.stroke_cap &&
                        runs.back().stroke_join == run.stroke_join &&
                        runs.back().stroke_miter_limit == run.stroke_miter_limit &&
                        runs.back().stroke_dash_array == run.stroke_dash_array &&
                        runs.back().stroke_dash_offset == run.stroke_dash_offset &&
                        runs.back().stroke_opacity == run.stroke_opacity &&
                        runs.back().opacity == run.opacity) {
                        runs.back().text += run.text;
                    } else {
                        runs.push_back(std::move(run));
                    }
                    local_x_pending = false;
                    local_y_pending = false;
                    root_x_pending = false;
                    root_y_pending = false;
                };
                if (!text_node.data().empty())
                    emit_run(text_node.data(), current, own_id, shaping_mergeable);
                for (const auto& [child_name, child] : text_node) {
                    if (child_name == "<xmltext>") {
                        emit_run(child.data(), current, own_id, shaping_mergeable);
                    } else if (child_name == "tspan") {
                        auto nested = current;
                        nested.dx = 0.0f;
                        nested.dy = 0.0f;
                        apply_svg_text_style(child, nested, gradients);
                        const auto span_id = child.template get<std::string>("<xmlattr>.id", "");
                        const auto span_properties = style_properties(child);
                        if (span_properties.contains("text-anchor"))
                            nested.text_anchor_scope_id = !span_id.empty() ? span_id
                                : text_group_id + "_anchor_" + std::to_string(runs.size());
                        if (!span_id.empty()) nested.group_ids.push_back(span_id);
                        visit_text(child, std::move(nested), span_id.empty() ? own_id : span_id,
                            child.template get_optional<std::string>("<xmlattr>.x").has_value(),
                            child.template get_optional<std::string>("<xmlattr>.y").has_value(),
                            span_id.empty() && span_properties.empty());
                    } else if (child_name == "textPath") {
                        auto nested = current;
                        nested.dx = 0.0f;
                        nested.dy = 0.0f;
                        apply_svg_text_style(child, nested, gradients);
                        if (nested.text_path_id) {
                            const auto target = ids.find(*nested.text_path_id);
                            if (target == ids.end() || target->second.first != "path")
                                throw std::invalid_argument(
                                    "SVG textPath href must resolve to a <path> element");
                            const auto path_data = target->second.second->get_optional<std::string>(
                                "<xmlattr>.d");
                            if (!path_data)
                                throw std::invalid_argument("SVG textPath target path has no d geometry");
                            auto parsed = SvgImporter{}.import_path_data(*path_data, {});
                            if (!parsed.ok)
                                throw std::invalid_argument("SVG textPath target path is malformed: " + parsed.error);
                            const auto local = target->second.second->get_optional<std::string>(
                                "<xmlattr>.transform");
                            apply_transform(parsed.path, transform *
                                (local ? parse_transform(*local) : Affine2(1.0f)));
                            nested.text_path_geometry = std::move(parsed.path);
                        }
                        const auto path_id = child.template get<std::string>("<xmlattr>.id", "");
                        visit_text(child, std::move(nested), path_id.empty() ? own_id : path_id,
                            child.template get_optional<std::string>("<xmlattr>.x").has_value(),
                            child.template get_optional<std::string>("<xmlattr>.y").has_value(),
                            false);
                    }
                }
            };
            visit_text(node, text_style, text_id,
                false, false, true);
            continue;
        }
        collect_svg_text_runs(ids, node, runs, gradients, transform, std::move(style));
    }
}

std::optional<std::string> collect_vector_mask_geometry(
    const std::string& definition_kind,
    const boost::property_tree::ptree& definition,
    const Affine2& root_transform,
    PathShape& output) {
    if (definition_kind == "clipPath" &&
        definition.get<std::string>("<xmlattr>.clipPathUnits", "userSpaceOnUse") != "userSpaceOnUse")
        return "SVG clipPath supports only userSpaceOnUse coordinates";
    if (definition_kind == "mask") {
        const auto mask_units = definition.get<std::string>("<xmlattr>.maskUnits", "userSpaceOnUse");
        const auto content_units = definition.get<std::string>("<xmlattr>.maskContentUnits", "userSpaceOnUse");
        if ((mask_units != "userSpaceOnUse" && mask_units != "objectBoundingBox") ||
            (content_units != "userSpaceOnUse" && content_units != "objectBoundingBox"))
            return "SVG mask supports only explicit userSpaceOnUse units";
    }
    const auto definition_transform = definition.get_optional<std::string>("<xmlattr>.transform");
    const Affine2 base = root_transform * (definition_transform ? parse_transform(*definition_transform) : Affine2(1.0f));
    std::size_t geometry_count = 0;
    std::function<std::optional<std::string>(const boost::property_tree::ptree&, const Affine2&)> visit;
    visit = [&](const boost::property_tree::ptree& tree, const Affine2& parent) -> std::optional<std::string> {
        for (const auto& [name, node] : tree) {
            if (name == "<xmlattr>" || name == "<xmltext>" || name == "<xmlcomment>") continue;
            const auto transform_text = node.get_optional<std::string>("<xmlattr>.transform");
            const Affine2 transform = parent * (transform_text ? parse_transform(*transform_text) : Affine2(1.0f));
            std::string d;
            if (name == "path") d = node.get<std::string>("<xmlattr>.d", "");
            else if (name == "rect" || name == "circle" || name == "ellipse" || name == "line" ||
                     name == "polyline" || name == "polygon") d = primitive_path_data(name, node);
            else if (name == "g") {
                if (auto error = visit(node, transform)) return error;
                continue;
            } else {
                return "SVG vector masks support only path and primitive geometry";
            }
            ++geometry_count;
            if (definition_kind == "mask") {
                const auto paint = style_properties(node);
                if (const auto fill = paint.find("fill"); fill != paint.end()) {
                    const auto fill_val = trim(fill->second);
                    if (fill_val == "black" || fill_val == "#000" || fill_val == "#000000") {
                        output.fill_rule = PathFillRule::EvenOdd;
                    }
                }
            }
            if (d.empty()) return "SVG vector mask geometry is empty";
            auto imported = SvgImporter{}.import_path_data(d, {});
            if (!imported.ok) return "SVG vector mask path is malformed: " + imported.error;
            apply_transform(imported.path, transform);
            output.commands.insert(output.commands.end(), imported.path.commands.begin(), imported.path.commands.end());
            if (output.commands.size() > 65536) return "SVG vector mask exceeds 65536 path commands";
        }
        return std::nullopt;
    };
    if (auto error = visit(definition, base)) return error;
    if (geometry_count == 0) return "SVG vector clip/mask must contain at least one geometry element";
    output.fill = Fill::solid_color(Color::white());
    output.stroke.enabled = false;
    return std::nullopt;
}

} // namespace

SvgPathLoadResult parse_svg_path_data(std::string_view d, SvgPathLoadOptions options) {
    return SvgImporter{}.import_path_data(d, options);
}

SvgDocumentLoadResult load_svg_document_file(
    const std::string& filename,
    SvgPathLoadOptions options) {
    constexpr std::uintmax_t kMaxSvgBytes = 8U * 1024U * 1024U;
    constexpr std::size_t kMaxXmlNodes = 100000;
    constexpr std::size_t kMaxCommandsPerPath = 65536;
    std::error_code size_error;
    const auto byte_size = std::filesystem::file_size(filename, size_error);
    if (size_error || byte_size > kMaxSvgBytes)
        return {.paths = {}, .elements = {}, .ok = false,
                .error = "SVG document exceeds the 8 MiB resource limit"};
    boost::property_tree::ptree document;
    try {
        // Preserve mixed-content ordering (`A<tspan>B</tspan>C`) as explicit
        // <xmltext> children. The default ptree parser concatenates the text
        // into parent.data(), which loses the text/tspan order needed for SVG
        // text chunk layout.
        boost::property_tree::read_xml(
            filename, document, boost::property_tree::xml_parser::no_concat_text);
    } catch (const boost::property_tree::xml_parser::xml_parser_error& e) {
        return {.paths = {}, .ok = false, .error = e.what()};
    }

    if (const auto css_error = apply_svg_css_rules(document))
        return {.paths = {}, .elements = {}, .ok = false,
                .error = "SVG CSS: " + *css_error};

    std::string unsupported;
    std::size_t xml_nodes = 0;
    std::function<void(const boost::property_tree::ptree&, const std::string&)> inspect =
        [&](const auto& tree, const std::string& parent_name) {
        for (const auto& [name, node] : tree) {
            if (name == "<xmlattr>") continue;
            if (name == "<xmlcomment>") continue;
            if (name == "<xmltext>") {
                if (!trim(node.data()).empty() && parent_name != "text" &&
                    parent_name != "tspan" && parent_name != "textPath")
                    unsupported = "text content outside <text>/<tspan>";
                if (!unsupported.empty()) return;
                continue;
            }
            if (++xml_nodes > kMaxXmlNodes) {
                unsupported = "resource limit: XML node count exceeds 100000";
                return;
            }
            if (name == "image") {
                const auto href = node.template get_optional<std::string>("<xmlattr>.href");
                const auto xlink_href = node.template get_optional<std::string>(
                    "<xmlattr>.{http://www.w3.org/1999/xlink}href");
                const std::string img_href = href ? *href : (xlink_href ? *xlink_href : "");
                if (img_href.empty() || !img_href.starts_with("data:image/")) {
                    unsupported = "non supportata: <image>";
                    return;
                }
                continue;
            }
            if (name == "style") continue;
            const auto href = node.template get_optional<std::string>("<xmlattr>.href");
            const auto xlink_href = node.template get_optional<std::string>(
                "<xmlattr>.{http://www.w3.org/1999/xlink}href");
            if (href || xlink_href) {
                const auto reference = href ? href : xlink_href;
                const bool supported_reference =
                    ((name == "use" || name == "textPath") && reference->size() >= 2 &&
                     reference->front() == '#' &&
                     reference->find_first_of(" \t\r\n", 1) == std::string::npos) ||
                    (name == "image" && reference->starts_with("data:image/"));
                if (!supported_reference) {
                    unsupported = "CSS esterni";
                    return;
                }
            }
            inspect(node, name);
            if (!unsupported.empty()) return;
        }
    };
    inspect(document, {});
    if (!unsupported.empty()) {
        if (unsupported.starts_with("resource limit:"))
            return {.paths = {}, .elements = {}, .ok = false, .error = "SVG " + unsupported};
        const auto tag = unsupported == "CSS esterni" ? "use" : unsupported.c_str();
        return {.paths = {}, .elements = {}, .ok = false,
                .error = std::string("SVG: ") + unsupported + " non supportata: <" + tag + ">"};
    }
    if (const auto style_error = validate_svg_subset(document))
        return {.paths = {}, .elements = {}, .ok = false,
                .error = *style_error};
    SvgGradientMap gradients;
    if (const auto gradient_error = collect_svg_gradients(document, gradients))
        return {.paths = {}, .elements = {}, .ok = false, .error = *gradient_error};
    std::map<std::string, std::pair<std::string, const boost::property_tree::ptree*>> ids;
    std::optional<std::string> duplicate_id_error;
    std::function<void(const boost::property_tree::ptree&)> collect_ids = [&](const auto& tree) {
        for (const auto& [name, node] : tree) {
            if (name == "<xmlattr>" || name == "<xmlcomment>" || name == "<xmltext>") continue;
            if (const auto id = node.template get_optional<std::string>("<xmlattr>.id")) {
                if (id->empty() || id->size() > 128 || !ids.emplace(*id, std::pair{name, &node}).second)
                    duplicate_id_error = "SVG ids must be unique, non-empty, and at most 128 bytes";
            }
            collect_ids(node);
        }
    };
    collect_ids(document);
    if (duplicate_id_error)
        return {.paths = {}, .elements = {}, .ok = false, .error = *duplicate_id_error};
    std::optional<PathShape> vector_mask;
    const boost::property_tree::ptree* svg_root = nullptr;
    for (const auto& [name, node] : document)
        if (name == "svg") { svg_root = &node; break; }
    if (!svg_root)
        return {.paths = {}, .elements = {}, .ok = false, .error = "SVG document root <svg> is missing"};
    const auto root_properties = style_properties(*svg_root);
    std::optional<float> root_gaussian_blur;
    if (const auto filter_it = root_properties.find("filter");
        filter_it != root_properties.end() && trim(filter_it->second) != "none") {
        std::string filter_id;
        if (!parse_svg_gradient_reference(filter_it->second, filter_id))
            return {.paths = {}, .elements = {}, .ok = false,
                    .error = "SVG root filter reference is malformed"};
        const auto filter = ids.find(filter_id);
        if (filter == ids.end() || filter->second.first != "filter")
            return {.paths = {}, .elements = {}, .ok = false,
                    .error = "SVG root filter reference does not resolve to a <filter> definition"};
        float std_deviation = 0.0f;
        if (const auto error = parse_root_gaussian_filter(*filter->second.second,
                                                          std_deviation))
            return {.paths = {}, .elements = {}, .ok = false, .error = *error};
        const auto root_transform_text = svg_root->get_optional<std::string>("<xmlattr>.transform");
        Affine2 root_transform = root_transform_text
            ? parse_transform(*root_transform_text) : Affine2(1.0f);
        if (const auto viewbox_transform = parse_viewbox_transform(*svg_root))
            root_transform = root_transform * *viewbox_transform;
        const auto origin = transform_point(root_transform, {0.0f, 0.0f});
        const auto axis_x = transform_point(root_transform, {1.0f, 0.0f}) - origin;
        const auto axis_y = transform_point(root_transform, {0.0f, 1.0f}) - origin;
        const float scale_x = glm::length(axis_x);
        const float scale_y = glm::length(axis_y);
        if (!std::isfinite(scale_x) || !std::isfinite(scale_y) || scale_x <= 0.0f ||
            std::abs(scale_x - scale_y) > 1e-5f * std::max(scale_x, scale_y) ||
            std::abs(glm::dot(axis_x, axis_y)) > 1e-5f * scale_x * scale_y)
            return {.paths = {}, .elements = {}, .ok = false,
                    .error = "SVG root Gaussian filter requires an isotropic root transform"};
        std_deviation *= scale_x;
        if (!std::isfinite(std_deviation) || std_deviation > 256.0f)
            return {.paths = {}, .elements = {}, .ok = false,
                    .error = "SVG root Gaussian filter radius exceeds 256 after root scaling"};
        root_gaussian_blur = std_deviation;
    }
    const auto clip_ref = root_properties.find("clip-path");
    const auto mask_ref = root_properties.find("mask");
    if (clip_ref != root_properties.end() && mask_ref != root_properties.end())
        return {.paths = {}, .elements = {}, .ok = false,
                .error = "SVG root cannot combine clip-path and mask in the bounded vector-mask subset"};
    const auto vector_ref = clip_ref != root_properties.end() ? clip_ref : mask_ref;
    if (root_gaussian_blur && vector_ref != root_properties.end())
        return {.paths = {}, .elements = {}, .ok = false,
                .error = "SVG root Gaussian filter cannot be combined with a root clip-path or mask"};
    if (vector_ref != root_properties.end()) {
        std::string reference_id;
        if (!parse_svg_gradient_reference(vector_ref->second, reference_id))
            return {.paths = {}, .elements = {}, .ok = false, .error = "SVG vector mask reference is malformed"};
        const auto target = ids.find(reference_id);
        const std::string expected_kind = clip_ref != root_properties.end() ? "clipPath" : "mask";
        if (target == ids.end() || target->second.first != expected_kind)
            return {.paths = {}, .elements = {}, .ok = false,
                    .error = "SVG vector mask reference does not resolve to a matching definition"};
        auto root_transform_text = svg_root->get_optional<std::string>("<xmlattr>.transform");
        Affine2 root_transform = root_transform_text ? parse_transform(*root_transform_text) : Affine2(1.0f);
        if (auto viewbox = parse_viewbox_transform(*svg_root)) root_transform = root_transform * *viewbox;
        PathShape mask_geometry;
        if (const auto mask_error = collect_vector_mask_geometry(expected_kind,
                *target->second.second, root_transform, mask_geometry))
            return {.paths = {}, .elements = {}, .ok = false, .error = *mask_error};
        vector_mask = std::move(mask_geometry);
    }
    // Keep validation before collection: collect_path_data uses forgiving
    // conversions, while this pass rejects malformed style values explicitly.
    std::vector<std::pair<std::string, Affine2>> path_data;
    std::vector<SvgStyleState> styles;
    std::vector<std::vector<SvgAnimationSpec>> animations;
    std::size_t expanded_uses = 0;
    std::uint32_t isolated_group_counter{0};
    collect_path_data(document, path_data, styles, animations, gradients, ids, 0,
        expanded_uses, isolated_group_counter);
    std::vector<SvgTextRun> text_runs;
    try {
        collect_svg_text_runs(ids, document, text_runs, gradients);
    } catch (const std::exception& error) {
        return {.paths = {}, .elements = {}, .text_runs = {}, .vector_mask = std::nullopt,
                .ok = false, .error = error.what()};
    }
    if (!root_gaussian_blur) {
        for (const auto& s : styles) {
            if (!s.filter_id.empty()) {
                const auto filter = ids.find(s.filter_id);
                if (filter != ids.end() && filter->second.first == "filter") {
                    float std_dev = 0.0f;
                    if (!parse_root_gaussian_filter(*filter->second.second, std_dev)) {
                        root_gaussian_blur = std_dev;
                        break;
                    }
                }
            }
        }
    }
    if (root_gaussian_blur && !text_runs.empty())
        return {.paths = {}, .elements = {}, .ok = false,
                .error = "SVG root Gaussian filter currently requires path-only content"};
    const bool root_mask_reference = vector_ref != root_properties.end();
    std::string root_mask_id;
    if (root_mask_reference) (void)parse_svg_gradient_reference(vector_ref->second, root_mask_id);
    for (std::size_t path_index = 0; path_index < styles.size(); ++path_index) {
        for (const auto& scope : styles[path_index].vector_mask_scopes) {
            // Root scope stays on the layer (so text is clipped too); local
            // scopes are attached to each affected path independently.
            if (root_mask_reference && scope.id == root_mask_id) continue;
            const auto target = ids.find(scope.id);
            const std::string expected = scope.is_mask ? "mask" : "clipPath";
            if (target == ids.end() || target->second.first != expected)
                return {.paths = {}, .elements = {}, .ok = false,
                        .error = "SVG element clip/mask reference does not resolve to a matching definition"};
            PathShape geometry;
            if (const auto error = collect_vector_mask_geometry(expected,
                    *target->second.second, scope.transform, geometry))
                return {.paths = {}, .elements = {}, .ok = false, .error = *error};
            styles[path_index].vector_masks.push_back(std::move(geometry));
        }
    }
    if (std::any_of(path_data.begin(), path_data.end(), [](const auto& path) { return path.first.empty(); }))
        return {.paths = {}, .elements = {}, .ok = false,
                .error = "SVG <use> reference is unresolved, recursive, out of bounds, or does not target geometry"};
    if (path_data.empty() && text_runs.empty()) {
        return {.paths = {}, .ok = false, .error = "No <path d=\"...\"> found"};
    }

    SvgDocumentLoadResult result;
    result.paths.reserve(path_data.size());
    result.text_runs = std::move(text_runs);
    result.vector_mask = vector_mask;
    result.root_gaussian_blur = root_gaussian_blur;
    for (std::size_t index = 0; index < path_data.size(); ++index) {
        if (std::any_of(animations[index].begin(), animations[index].end(), [](const auto& animation) {
                return animation.duration_seconds < 0.0f;
            }))
            return {.paths = {}, .elements = {}, .ok = false,
                    .error = "SVG animation is malformed or outside the supported bounded subset"};
        auto path = SvgImporter{}.import_path_data(path_data[index].first, options);
        if (!path.ok) {
            return {
                .paths = {},
                .ok = false,
                .error = "Failed to import <path> at index " +
                         std::to_string(index) + ": " + path.error,
            };
        }
        auto style = styles[index];
        std::vector<SvgGradientAnimationKeyframe> gradient_animation;
        apply_transform(path.path, path_data[index].second);
        if (path.path.commands.size() > kMaxCommandsPerPath)
            return {.paths = {}, .elements = {}, .ok = false,
                    .error = "SVG resource limit: path command count exceeds 65536"};
        const auto quantize = [](Vec2& point) {
            if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
                std::abs(point.x) > 100000.0f || std::abs(point.y) > 100000.0f)
                return false;
            point.x = std::round(point.x * 10000.0f) / 10000.0f;
            point.y = std::round(point.y * 10000.0f) / 10000.0f;
            return true;
        };
        for (auto& command : path.path.commands) {
            if (!quantize(command.p0) || !quantize(command.p1) || !quantize(command.p2))
                return {.paths = {}, .elements = {}, .ok = false,
                        .error = "SVG coordinate exceeds finite resource bounds"};
        }
        if (style.morph_animation) {
            auto& morph = *style.morph_animation;
            if (morph.paths.size() < 2 || morph.paths.size() != morph.key_times.size())
                return {.paths = {}, .elements = {}, .ok = false,
                        .error = "SVG path morph requires matching values and keyTimes"};
            const auto& topology = morph.paths.front();
            if (topology.commands.empty() || topology.commands.size() > kMaxCommandsPerPath)
                return {.paths = {}, .elements = {}, .ok = false,
                        .error = "SVG morph path is empty or exceeds 65536 commands"};
            for (auto& target : morph.paths) {
                apply_transform(target, path_data[index].second);
                if (target.commands.size() != topology.commands.size() || target.closed != topology.closed)
                    return {.paths = {}, .elements = {}, .ok = false,
                            .error = "SVG path morph requires matching command topology"};
                for (std::size_t command_index = 0; command_index < target.commands.size(); ++command_index) {
                    auto& command = target.commands[command_index];
                    if (command.type != topology.commands[command_index].type ||
                        !quantize(command.p0) || !quantize(command.p1) || !quantize(command.p2))
                        return {.paths = {}, .elements = {}, .ok = false,
                                .error = "SVG path morph requires matching command topology and bounded coordinates"};
                }
            }
            path.path = morph.paths.front();
        }
        if (!style.fill_gradient_id.empty()) {
            const auto gradient_it = gradients.find(style.fill_gradient_id);
            if (gradient_it == gradients.end())
                return {.paths = {}, .elements = {}, .ok = false,
                        .error = "SVG fill references an unknown gradient id"};
            float min_x = std::numeric_limits<float>::infinity();
            float min_y = std::numeric_limits<float>::infinity();
            float max_x = -min_x, max_y = -min_y;
            for (const auto& command : path.path.commands) {
                if (command.type == PathCommandType::Close) continue;
                std::array<Vec2, 3> points{command.p0, command.p1, command.p2};
                const std::size_t point_count = command.type == PathCommandType::CubicTo ? 3 :
                    command.type == PathCommandType::QuadraticTo ? 2 : 1;
                for (std::size_t point_index = 0; point_index < point_count; ++point_index) {
                    const auto point = points[point_index];
                    min_x = std::min(min_x, point.x); min_y = std::min(min_y, point.y);
                    max_x = std::max(max_x, point.x); max_y = std::max(max_y, point.y);
                }
            }
            const float width = max_x - min_x, height = max_y - min_y;
            if (!(width > 0.0f) || !(height > 0.0f))
                return {.paths = {}, .elements = {}, .ok = false,
                        .error = "SVG objectBoundingBox gradient requires non-degenerate path bounds"};
            const auto& source = gradient_it->second;
            const Affine2 total_gradient_transform = source.user_space
                ? (path_data[index].second * source.transform)
                : source.transform;
            const auto bbox_point = [&](Vec2 coord) {
                if (source.user_space) {
                    return transform_point(total_gradient_transform, coord);
                }
                const Vec2 transformed = transform_point(source.transform, coord);
                return Vec2{min_x + transformed.x * width, min_y + transformed.y * height};
            };
            auto stops = source.stops;
            for (auto& stop : stops) stop.color.a *= style.fill_opacity;
            if (source.radial) {
                const auto center = bbox_point(source.p0);
                const float sx = glm::length(transform_point(total_gradient_transform, {1.0f, 0.0f}) -
                                            transform_point(total_gradient_transform, {0.0f, 0.0f}));
                const float radius = source.user_space
                    ? (source.radius * sx)
                    : (source.radius * width);
                if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(radius) || radius <= 0.0f)
                    return {.paths = {}, .elements = {}, .ok = false,
                            .error = "SVG radial gradientTransform exceeds finite bounds"};
                style.fill = Fill::radial(center, radius, std::move(stops));
            } else {
                const auto start = bbox_point(source.p0);
                const auto end = bbox_point(source.p1);
                if (!std::isfinite(start.x) || !std::isfinite(start.y) ||
                    !std::isfinite(end.x) || !std::isfinite(end.y) ||
                    std::abs(start.x) > 100000.0f || std::abs(start.y) > 100000.0f ||
                    std::abs(end.x) > 100000.0f || std::abs(end.y) > 100000.0f)
                    return {.paths = {}, .elements = {}, .ok = false,
                            .error = "SVG linear gradientTransform exceeds finite bounds"};
                style.fill = Fill::linear(start, end, std::move(stops));
            }
            if (!source.animations.empty() && style.fill && style.fill->enabled) {
                const std::vector<float> event_times{0.0f, source.animations.front().duration_seconds};
                for (const float seconds : event_times) {
                    auto gradient = style.fill->gradient;
                    for (const auto& animation : source.animations) {
                        if (animation.stop_index >= gradient.color_stops.size())
                            return {.paths = {}, .elements = {}, .ok = false,
                                    .error = "SVG animated gradient stop index is invalid"};
                        const float progress = animation.discrete
                            ? (seconds >= animation.duration_seconds ? 1.0f : 0.0f)
                            : std::clamp(seconds / animation.duration_seconds, 0.0f, 1.0f);
                        const auto mix = [progress](float from, float to) {
                            return from + (to - from) * progress;
                        };
                        auto& stop = gradient.color_stops[animation.stop_index];
                        if (animation.opacity) {
                            stop.color.a = mix(animation.from.a, animation.to.a) * style.fill_opacity;
                        } else {
                            stop.color.r = mix(animation.from.r, animation.to.r);
                            stop.color.g = mix(animation.from.g, animation.to.g);
                            stop.color.b = mix(animation.from.b, animation.to.b);
                        }
                    }
                    gradient_animation.push_back({seconds, std::move(gradient)});
                }
                style.fill->gradient = gradient_animation.front().gradient;
            }
        }
        style.stroke.color.a *= style.stroke_opacity;
        if (style.fill && is_solid_fill(*style.fill)) style.fill->solid.a *= style.fill_opacity;
        path.path.fill = style.fill.value_or(Fill{.enabled = false});
        path.path.stroke = style.stroke;
        path.path.stroke.miter_limit = style.stroke_miter_limit;
        path.path.fill_rule = style.fill_rule;
        path.path.opacity = style.opacity;
        SvgStyledElement styled_element{
            path.path, style.fill, style.stroke, style.opacity,
            style.groups.empty() ? std::nullopt
                : std::optional<SvgStyleGroupRef>{style.groups.back()},
            std::move(style.groups),
            std::move(animations[index]),
            std::move(style.morph_animation), std::move(gradient_animation),
            !style.fill_gradient_id.empty() &&
                !gradients.at(style.fill_gradient_id).animations.empty() &&
                gradients.at(style.fill_gradient_id).animations.front().discrete};
        styled_element.source_id = std::move(style.source_id);
        styled_element.group_ids = std::move(style.group_ids);
        styled_element.vector_masks = std::move(style.vector_masks);
        const bool stroke_first = style.paint_order.starts_with("stroke");
        const bool has_fill = style.fill.has_value() && style.fill->enabled;
        const bool has_stroke = style.stroke.enabled;
        if (stroke_first && has_fill && has_stroke) {
            auto stroke_element = styled_element;
            stroke_element.fill = Fill{.enabled = false};
            stroke_element.path.fill = Fill{.enabled = false};
            result.elements.push_back(stroke_element);
            result.paths.push_back(stroke_element.path);

            auto fill_element = std::move(styled_element);
            fill_element.stroke.enabled = false;
            fill_element.path.stroke.enabled = false;
            result.elements.push_back(std::move(fill_element));
            result.paths.push_back(result.elements.back().path);
        } else {
            result.elements.push_back(std::move(styled_element));
            result.paths.push_back(std::move(path.path));
        }
    }
    result.ok = true;
    return result;
}

} // namespace chronon3d::assets
