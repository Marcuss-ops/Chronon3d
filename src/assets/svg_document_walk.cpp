#include "svg_document_walk_detail.hpp"

#include "svg_animation_detail.hpp"
#include "svg_document_validation.hpp"
#include "svg_gradient_detail.hpp"
#include "svg_importer.hpp"
#include "svg_path_loader_detail.hpp"
#include "svg_text_detail.hpp"
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace chronon3d::assets::svg_document_walk_detail {
namespace {
using svg_loader_detail::apply_svg_css_rules;
using svg_gradient_detail::Affine2;
using svg_gradient_detail::SvgGradientMap;
using svg_gradient_detail::apply_transform;
using svg_gradient_detail::collect_svg_gradients;
using svg_gradient_detail::parse_svg_scalar;
using svg_gradient_detail::parse_transform;
using svg_gradient_detail::parse_viewbox_transform;
using svg_gradient_detail::parse_svg_gradient_reference;
using svg_gradient_detail::primitive_path_data;
using svg_gradient_detail::transform_point;
using svg_animation_detail::parse_svg_animation;
using svg_animation_detail::parse_svg_morph_animation;
using svg_loader_detail::parse_svg_color;
using svg_loader_detail::parse_svg_length;
using svg_loader_detail::style_properties;
using svg_loader_detail::style_number;
using svg_loader_detail::transform_numbers;
using svg_loader_detail::trim;
using svg_text_detail::collect_svg_text_runs;
using svg_text_detail::svg_baseline_shift;
using svg_text_detail::svg_text_path_reference;
using svg_text_detail::svg_text_path_offset;

bool is_translation_matrix(const Affine2& transform) {
    constexpr float epsilon = 1e-6f;
    return std::abs(transform[0][0] - 1.0f) <= epsilon &&
        std::abs(transform[1][1] - 1.0f) <= epsilon &&
        std::abs(transform[0][1]) <= epsilon &&
        std::abs(transform[1][0]) <= epsilon &&
        std::abs(transform[0][2]) <= epsilon &&
        std::abs(transform[1][2]) <= epsilon &&
        std::abs(transform[2][2] - 1.0f) <= epsilon;
}

bool is_identity_matrix(const Affine2& transform) {
    constexpr float epsilon = 1e-6f;
    return is_translation_matrix(transform) &&
        std::abs(transform[2][0]) <= epsilon && std::abs(transform[2][1]) <= epsilon;
}

bool is_svg_transform_animation(SvgAnimationKind kind) {
    return kind == SvgAnimationKind::Translate || kind == SvgAnimationKind::Scale ||
        kind == SvgAnimationKind::Rotate;
}

void validate_static_animation_scope(const SvgStyleState& style,
                                     std::vector<SvgAnimationSpec>& animations) {
    const bool has_transform = std::any_of(animations.begin(), animations.end(),
        [](const auto& animation) { return is_svg_transform_animation(animation.kind); });
    const bool only_translations = std::all_of(animations.begin(), animations.end(),
        [](const auto& animation) {
            return !is_svg_transform_animation(animation.kind) ||
                animation.kind == SvgAnimationKind::Translate;
        });
    if (has_transform && style.has_static_transform_scope &&
        (!style.static_transform_scopes_translation || !only_translations)) {
        animations.clear();
        animations.push_back(SvgAnimationSpec{});
        animations.back().duration_seconds = -1.0f;
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

}  // namespace

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
    const Affine2& parent_transform,
    SvgStyleState parent_style) {
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
            // Malformed numeric attributes fall back to the default; only
            // std::stof's own parse exceptions are swallowed here so genuine
            // faults (e.g. allocation failures) still propagate.
            try {
                style.stroke_miter_limit = std::stof(it->second);
                style.stroke.miter_limit = style.stroke_miter_limit;
            } catch (const std::invalid_argument&) {
            } catch (const std::out_of_range&) {}
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
            try { style.stroke.dash_offset = std::stof(it->second); }
            catch (const std::invalid_argument&) {}
            catch (const std::out_of_range&) {}
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
        style.has_static_transform_scope = style.has_static_transform_scope ||
            !is_identity_matrix(node_transform);
        style.static_transform_scopes_translation = style.static_transform_scopes_translation &&
            is_translation_matrix(node_transform);
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
                validate_static_animation_scope(style, animations.back());
                styles.back().morph_animation = std::move(style.morph_animation);
            }
        } else if (name == "rect" || name == "circle" || name == "ellipse" ||
                   name == "line" || name == "polyline" || name == "polygon") {
            if (auto d = primitive_path_data(name, node);
                !d.empty() && !style.display_none && !style.visibility_hidden) {
                paths.emplace_back(std::move(d), transform); styles.push_back(style);
                animations.emplace_back(style.inherited_animations);
                validate_static_animation_scope(style, animations.back());
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
    std::optional<PathFillRule> clip_fill_rule;
    const auto parse_clip_rule = [](const std::string& value) {
        return value == "evenodd" ? PathFillRule::EvenOdd : PathFillRule::NonZero;
    };
    const auto definition_properties = style_properties(definition);
    const auto root_clip_rule = definition_properties.find("clip-rule");
    const auto inherited_rule = root_clip_rule == definition_properties.end()
        ? PathFillRule::NonZero : parse_clip_rule(root_clip_rule->second);
    std::function<std::optional<std::string>(const boost::property_tree::ptree&, const Affine2&, PathFillRule)> visit;
    visit = [&](const boost::property_tree::ptree& tree, const Affine2& parent,
                PathFillRule parent_rule) -> std::optional<std::string> {
        for (const auto& [name, node] : tree) {
            if (name == "<xmlattr>" || name == "<xmltext>" || name == "<xmlcomment>") continue;
            const auto transform_text = node.get_optional<std::string>("<xmlattr>.transform");
            const Affine2 transform = parent * (transform_text ? parse_transform(*transform_text) : Affine2(1.0f));
            auto rule = parent_rule;
            if (definition_kind == "clipPath") {
                const auto properties = style_properties(node);
                if (const auto it = properties.find("clip-rule"); it != properties.end())
                    rule = parse_clip_rule(it->second);
            }
            std::string d;
            if (name == "path") d = node.get<std::string>("<xmlattr>.d", "");
            else if (name == "rect" || name == "circle" || name == "ellipse" || name == "line" ||
                     name == "polyline" || name == "polygon") d = primitive_path_data(name, node);
            else if (name == "g") {
                if (auto error = visit(node, transform, rule)) return error;
                continue;
            } else {
                return "SVG vector masks support only path and primitive geometry";
            }
            if (definition_kind == "clipPath") {
                if (clip_fill_rule && *clip_fill_rule != rule)
                    return "SVG clipPath geometry must use one consistent clip-rule";
                clip_fill_rule = rule;
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
    if (auto error = visit(definition, base, inherited_rule)) return error;
    if (geometry_count == 0) return "SVG vector clip/mask must contain at least one geometry element";
    if (clip_fill_rule) output.fill_rule = *clip_fill_rule;
    output.fill = Fill::solid_color(Color::white());
    output.stroke.enabled = false;
    return std::nullopt;
}

}  // namespace chronon3d::assets::svg_document_walk_detail
