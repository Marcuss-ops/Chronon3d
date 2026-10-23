#include <chronon3d/assets/svg_path_loader.hpp>

#include "svg_importer.hpp"
#include "svg_path_loader_detail.hpp"
#include "svg_gradient_detail.hpp"
#include "svg_text_detail.hpp"
#include "svg_document_validation.hpp"
#include "svg_document_walk_detail.hpp"
#include "svg_animation_detail.hpp"

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
using svg_document_walk_detail::SvgIdMap;
using svg_document_walk_detail::SvgStyleState;
using svg_document_walk_detail::collect_path_data;
using svg_document_walk_detail::collect_vector_mask_geometry;
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
