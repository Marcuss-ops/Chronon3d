#include "svg_path_loader_detail.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <istream>
#include <map>
#include <regex>
#include <set>
#include <string_view>
#include <vector>

namespace chronon3d::assets::svg_loader_detail {
std::vector<float> transform_numbers(std::string_view text) {
    static const std::regex number(R"([-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?)");
    std::vector<float> values;
    for (std::cregex_iterator it(text.data(), text.data() + text.size(), number), end;
         it != end; ++it) {
        values.push_back(std::stof(it->str()));
    }
    return values;
}

std::optional<float> parse_svg_length(std::string_view text, float percentage_relative_to) {
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
    } catch (const std::invalid_argument&) {
        return std::nullopt;
    } catch (const std::out_of_range&) {
        return std::nullopt;
    }
}

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
        return Color::try_from_hex(std::string{named->second}.c_str());
    }
    if (value.size() == 4 && value[0] == '#') {
        const auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
        const int r = digit(value[1]), g = digit(value[2]), b = digit(value[3]);
        if (r < 0 || g < 0 || b < 0) return std::nullopt;
        return Color{r / 15.0f, g / 15.0f, b / 15.0f, 1.0f};
    }
    if (value.size() == 7 && value[0] == '#') {
        return Color::try_from_hex(value.c_str());
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



namespace {
using boost::property_tree::ptree;

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


}  // namespace

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


}  // namespace chronon3d::assets::svg_loader_detail
