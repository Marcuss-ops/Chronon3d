#include "svg_text_detail.hpp"
#include "svg_importer.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d::assets::svg_text_detail {
using svg_gradient_detail::Affine2;
using svg_gradient_detail::SvgGradientMap;
using svg_gradient_detail::parse_transform;
using svg_gradient_detail::parse_viewbox_transform;
using svg_gradient_detail::apply_transform;
using svg_gradient_detail::transform_point;
using svg_loader_detail::style_properties;
using svg_loader_detail::trim;

void collect_svg_text_runs(
    const SvgIdMap& ids,
    const boost::property_tree::ptree& tree,
    std::vector<SvgTextRun>& runs,
    const SvgGradientMap& gradients,
    const Affine2& parent_transform,
    SvgTextStyleState parent_style) {
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
} // namespace chronon3d::assets::svg_text_detail
