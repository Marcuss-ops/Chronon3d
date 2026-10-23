#include "render_plan_compiler_detail.hpp"

#include <chronon3d/backends/text/text_unicode_grapheme.hpp>
#include <chronon3d/render_plan/color_utils.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d::render_plan::detail {

std::optional<PlanDecodeError> append_merged_svg_text_layers(
    RenderPlan& plan,
    std::vector<std::pair<chronon3d::assets::SvgTextRun, LayerPlan>>& pending_svg_text_layers,
    const std::vector<SvgTextMetrics>& pending_svg_text_metrics,
    const std::vector<bool>& pending_svg_text_boundary_before) {
    std::vector<std::pair<chronon3d::assets::SvgTextRun, LayerPlan>> shaped_text_layers;
    shaped_text_layers.reserve(pending_svg_text_layers.size());
    const auto compatible_paint_run = [](const auto& left, const auto& right) {
        const auto& a = left.first;
        const auto& b = right.first;
        const auto& al = left.second;
        const auto& bl = right.second;
        const auto source_layer = [](const std::string& id) {
            const auto marker = id.find("__svgtext_");
            return id.substr(0, marker);
        };
        return a.text_element_id == b.text_element_id &&
            source_layer(al.id) == source_layer(bl.id) &&
            a.group_ids == b.group_ids &&
            a.coordinate_scope_ids == b.coordinate_scope_ids &&
            a.text_anchor_scope_id == b.text_anchor_scope_id &&
            a.font_family == b.font_family && a.font_size == b.font_size &&
            a.font_weight == b.font_weight && a.font_style == b.font_style &&
            a.direction == b.direction &&
            a.text_anchor == b.text_anchor &&
            a.dominant_baseline == b.dominant_baseline &&
            a.baseline_shift == b.baseline_shift && a.opacity == b.opacity &&
            !a.fill_gradient && !b.fill_gradient && !a.stroke && !b.stroke &&
            !a.stroke_gradient && !b.stroke_gradient &&
            !a.fill_pattern && !b.fill_pattern &&
            !a.stroke_pattern && !b.stroke_pattern &&
            !a.text_path_id && !b.text_path_id &&
            !a.positioned_text_cluster && !b.positioned_text_cluster &&
            !b.x_explicit && !b.y_explicit &&
            a.x_coordinates.empty() && a.y_coordinates.empty() &&
            a.dx_coordinates.empty() && a.dy_coordinates.empty() &&
            a.transformed_position_coordinates.empty() &&
            a.transformed_delta_coordinates.empty() &&
            b.x_coordinates.empty() && b.y_coordinates.empty() &&
            b.dx_coordinates.empty() && b.dy_coordinates.empty() &&
            b.transformed_position_coordinates.empty() &&
            b.transformed_delta_coordinates.empty() &&
            !al.animation && !bl.animation && al.style && bl.style &&
            !al.style->fill_gradient && !bl.style->fill_gradient &&
            !al.style->stroke && !bl.style->stroke &&
            al.style->font_family == bl.style->font_family &&
            al.style->font_weight == bl.style->font_weight &&
            al.style->font_style == bl.style->font_style &&
            al.style->font_size == bl.style->font_size &&
            al.style->text_align == bl.style->text_align &&
            al.rotation == bl.rotation && al.scale == bl.scale &&
            al.enable_3d == bl.enable_3d &&
            al.position_dimensions == bl.position_dimensions &&
            al.opacity == bl.opacity &&
            std::abs(al.position[1] - bl.position[1]) <= 1.0f;
    };
    for (std::size_t begin = 0; begin < pending_svg_text_layers.size();) {
        std::size_t end = begin + 1;
        while (end < pending_svg_text_layers.size() &&
               pending_svg_text_boundary_before[end] &&
               compatible_paint_run(pending_svg_text_layers[end - 1],
                                    pending_svg_text_layers[end])) ++end;
        if (end - begin == 1) {
            shaped_text_layers.push_back(std::move(pending_svg_text_layers[begin]));
            begin = end;
            continue;
        }
        auto combined = pending_svg_text_layers[begin];
        combined.second.text_spans.clear();
        std::string text;
        float pen = 0.0f;
        float ink_right = 0.0f;
        struct PaintRange { std::size_t start; std::size_t end; Color color; };
        std::vector<PaintRange> paint_ranges;
        for (std::size_t index = begin; index < end; ++index) {
            const auto& run = pending_svg_text_layers[index].first;
            const std::size_t start = text.size();
            text += run.text;
            const auto& metrics = pending_svg_text_metrics[index];
            ink_right = std::max(ink_right, pen + metrics.ink_width);
            pen += metrics.advance;
            const auto color = parse_hex_color(run.fill);
            if (!color)
                return PlanDecodeError{"svg.text.fill",
                    "SVG text paint span has an invalid solid fill",
                    "svg_text_invalid", "render-plan-svg"};
            auto paint = *color;
            paint.a *= run.fill_opacity;
            paint_ranges.push_back({start, text.size(), paint});
        }
        combined.second.svg_glyph_paint_ranges.clear();
        combined.second.svg_glyph_paint_ranges.reserve(paint_ranges.size());
        for (const auto& range : paint_ranges) {
            combined.second.svg_glyph_paint_ranges.push_back(
                {range.start, range.end, range.color});
        }
        const auto grapheme_count = chronon3d::detail::grapheme_cluster_count(text);
        for (std::size_t grapheme = 0; grapheme < grapheme_count; ++grapheme) {
            const auto start = chronon3d::detail::grapheme_byte_offset_at(text, grapheme);
            const auto finish = chronon3d::detail::grapheme_byte_offset_at(text, grapheme + 1);
            const auto owner = std::find_if(paint_ranges.begin(), paint_ranges.end(),
                [start](const PaintRange& range) {
                    return start >= range.start && start < range.end;
                });
            if (owner == paint_ranges.end()) continue;
            chronon3d::TextSpanOverride span;
            span.byte_start = start;
            span.byte_end = finish;
            span.color = owner->color;
            if (!combined.second.text_spans.empty() &&
                combined.second.text_spans.back().color == span.color &&
                combined.second.text_spans.back().byte_end == span.byte_start) {
                combined.second.text_spans.back().byte_end = span.byte_end;
            } else {
                combined.second.text_spans.push_back(std::move(span));
            }
        }
        combined.first.text = text;
        combined.second.text = std::move(text);
        combined.second.size[0] = std::max(1.0f, std::max(pen, ink_right) + 1.0f);
        shaped_text_layers.push_back(std::move(combined));
        begin = end;
    }
    for (auto& pending : shaped_text_layers)
        plan.layers.push_back(std::move(pending.second));
    return std::nullopt;
}

} // namespace chronon3d::render_plan::detail
