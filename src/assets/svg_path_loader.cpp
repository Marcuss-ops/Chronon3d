#include <chronon3d/assets/svg_path_loader.hpp>

#include "svg_importer.hpp"
#include "svg_path_loader_detail.hpp"
#include "svg_gradient_detail.hpp"
#include "svg_text_detail.hpp"
#include "svg_document_validation.hpp"
#include "svg_animation_detail.hpp"
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
using svg_document_detail::inspect_svg_xml_tree;
using svg_document_detail::parse_root_gaussian_filter;




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

using svg_text_detail::SvgIdMap;


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

    if (const auto unsupported = inspect_svg_xml_tree(document, kMaxXmlNodes)) {
        if (unsupported->starts_with("resource limit:"))
            return {.paths = {}, .elements = {}, .ok = false,
                    .error = "SVG " + *unsupported};
        const auto tag = *unsupported == "CSS esterni" ? "use" : unsupported->c_str();
        return {.paths = {}, .elements = {}, .ok = false,
                .error = std::string("SVG: ") + *unsupported + " non supportata: <" + tag + ">"};
    }
    if (const auto style_error = svg_document_detail::validate_svg_subset(document))
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
