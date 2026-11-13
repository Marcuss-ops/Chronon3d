#include "render_plan_compiler_detail.hpp"
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/backends/text/text_unicode_grapheme.hpp>
#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_path_composer.hpp>
#include "../text/unicode/unicode_script.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace chronon3d::render_plan::detail {

namespace {

struct SvgTextLengthRunLayout {
    float base_advance{0.0f};
    float base_ink_width{0.0f};
    std::size_t cluster_count{0};
    std::size_t word_gap_count{0};
    float scale{1.0f};
    float gap{0.0f};
    float trailing_gap{0.0f};
};

struct SvgTextLengthScopeNode {
    float target{0.0f};
    std::string length_adjust{"spacing"};
    std::string parent;
    std::size_t run_count{0};
    std::size_t first_run{0};
    std::size_t last_run{0};
    int depth{0};
};

bool svg_text_cluster_is_space(std::string_view value) {
    return value == " " || value == "\t" || value == "\n" || value == "\r";
}

bool svg_text_run_has_length_scope(
    const chronon3d::assets::SvgTextRun& run, const std::string& scope_id) {
    return std::any_of(run.text_length_scopes.begin(), run.text_length_scopes.end(),
        [&](const chronon3d::assets::SvgTextLengthScope& scope) {
            return scope.id == scope_id;
        });
}

std::optional<PlanDecodeError> resolve_svg_text_length_scopes(
    std::vector<chronon3d::assets::SvgTextRun>& runs,
    std::vector<SvgTextMetrics>& text_metrics,
    chronon3d::assets::AssetResolver& resolver,
    std::unique_ptr<chronon3d::FontEngine>& font_engine,
    std::size_t layer_index) {
    std::map<std::string, std::vector<std::size_t>> groups;
    for (std::size_t index = 0; index < runs.size(); ++index) {
        if (!runs[index].text_length_scopes.empty())
            groups[runs[index].text_element_id].push_back(index);
    }
    if (!font_engine && !groups.empty())
        return PlanDecodeError{"layers[" + std::to_string(layer_index) +
            "].shape.asset", "SVG textLength requires a text font engine",
            "svg_text_invalid", "render-plan-svg"};
    for (auto& [element_id, run_indices] : groups) {
        (void)element_id;
        std::map<std::string, SvgTextLengthScopeNode> scopes;
        for (const std::size_t run_index : run_indices) {
            const auto& chain = runs[run_index].text_length_scopes;
            std::string parent;
            for (std::size_t depth = 0; depth < chain.size(); ++depth) {
                const auto& scope = chain[depth];
                auto [it, inserted] = scopes.try_emplace(scope.id);
                auto& node = it->second;
                if (inserted) {
                    node.target = scope.target;
                    node.length_adjust = scope.length_adjust;
                    node.parent = parent;
                    node.first_run = run_index;
                    node.last_run = run_index;
                    node.depth = static_cast<int>(depth);
                } else {
                    if (std::abs(node.target - scope.target) > 1.0e-3f ||
                        node.length_adjust != scope.length_adjust ||
                        node.parent != parent)
                        return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                            "].shape.asset", "SVG nested textLength scopes are inconsistent",
                            "svg_text_length_scope_invalid", "render-plan-svg"};
                    node.first_run = std::min(node.first_run, run_index);
                    node.last_run = std::max(node.last_run, run_index);
                }
                ++node.run_count;
                parent = scope.id;
            }
        }

        std::map<std::size_t, SvgTextLengthRunLayout> layouts;
        for (const std::size_t run_index : run_indices) {
            const auto& run = runs[run_index];
            chronon3d::FontSpec font_spec;
            font_spec.font_path = text_metrics[run_index].font_asset;
            if (const auto path = resolver.resolve_logical(font_spec.font_path))
                font_spec.font_path = path->string();
            font_spec.font_family = run.font_family;
            font_spec.font_weight = run.font_weight;
            font_spec.font_style = run.font_style;
            font_spec.font_size = run.font_size;
            const auto shaped = font_engine->shape_text(
                run.text, font_spec, run.font_size);
            if (!shaped)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                    "].shape.asset", "SVG textLength text could not be shaped",
                    "svg_text_invalid", "render-plan-svg"};
            const auto placed = chronon3d::resolve_placed_glyph_run(
                *shaped, 0.0f, run.text);
            auto& layout = layouts[run_index];
            layout.base_advance = placed.total_width;
            layout.base_ink_width = text_metrics[run_index].ink_width;
            std::size_t last_nonempty_cluster = 0;
            for (std::size_t cluster_index = 0;
                 cluster_index < placed.clusters.size(); ++cluster_index) {
                const auto& cluster = placed.clusters[cluster_index];
                if (cluster.byte_len == 0 ||
                    cluster.byte_offset + cluster.byte_len > run.text.size())
                    continue;
                ++layout.cluster_count;
                last_nonempty_cluster = cluster_index;
            }
            for (std::size_t cluster_index = 0;
                 cluster_index < placed.clusters.size(); ++cluster_index) {
                const auto& cluster = placed.clusters[cluster_index];
                if (cluster.byte_len == 0 ||
                    cluster.byte_offset + cluster.byte_len > run.text.size())
                    continue;
                const auto cluster_text = run.text.substr(
                    cluster.byte_offset, cluster.byte_len);
                if (svg_text_cluster_is_space(cluster_text) &&
                    cluster_index != last_nonempty_cluster)
                    ++layout.word_gap_count;
            }
        }

        std::vector<std::string> scope_order;
        scope_order.reserve(scopes.size());
        for (const auto& [scope_id, node] : scopes) {
            (void)node;
            scope_order.push_back(scope_id);
        }
        std::sort(scope_order.begin(), scope_order.end(),
            [&](const std::string& left, const std::string& right) {
                return scopes.at(left).depth > scopes.at(right).depth;
            });
        for (const auto& scope_id : scope_order) {
            const auto& node = scopes.at(scope_id);
            float total = 0.0f;
            std::size_t total_clusters = 0;
            for (std::size_t run_index = node.first_run;
                 run_index <= node.last_run; ++run_index) {
                if (!svg_text_run_has_length_scope(runs[run_index], scope_id))
                    continue;
                const auto& run = runs[run_index];
                const auto& layout = layouts[run_index];
                if (layout.cluster_count == 0) continue;
                const float base_spacing =
                    run.letter_spacing * static_cast<float>(layout.cluster_count - 1) +
                    run.word_spacing * static_cast<float>(layout.word_gap_count);
                total += layout.base_advance * layout.scale +
                    base_spacing * layout.scale +
                    layout.gap * static_cast<float>(layout.cluster_count - 1) +
                    layout.trailing_gap;
                total_clusters += layout.cluster_count;
            }
            if (total_clusters == 0) continue;
            const std::size_t total_gaps = total_clusters - 1;
            if (!std::isfinite(total))
                return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                    "].shape.asset", "SVG textLength produced a non-finite advance",
                    "svg_text_length_scope_invalid", "render-plan-svg"};
            if (node.length_adjust == "spacingAndGlyphs") {
                if (!(total > 0.0f))
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                        "].shape.asset", "SVG textLength spacingAndGlyphs needs a positive advance",
                        "svg_text_length_scope_invalid", "render-plan-svg"};
                const float factor = node.target / total;
                if (!std::isfinite(factor) || factor <= 0.0f)
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                        "].shape.asset", "SVG textLength produced an invalid glyph scale",
                        "svg_text_length_scope_invalid", "render-plan-svg"};
                for (std::size_t run_index = node.first_run;
                     run_index <= node.last_run; ++run_index) {
                    if (!svg_text_run_has_length_scope(runs[run_index], scope_id))
                        continue;
                    auto& layout = layouts[run_index];
                    layout.scale *= factor;
                    layout.gap *= factor;
                    layout.trailing_gap *= factor;
                }
            } else {
                if (total_gaps == 0) continue;
                const float gap = (node.target - total) /
                    static_cast<float>(total_gaps);
                if (!std::isfinite(gap))
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                        "].shape.asset", "SVG textLength produced an invalid spacing gap",
                        "svg_text_length_scope_invalid", "render-plan-svg"};
                for (std::size_t run_index = node.first_run;
                     run_index <= node.last_run; ++run_index) {
                    if (!svg_text_run_has_length_scope(runs[run_index], scope_id))
                        continue;
                    auto& layout = layouts[run_index];
                    layout.gap += gap;
                    if (run_index != node.last_run &&
                        run_index + 1 < runs.size() &&
                        svg_text_run_has_length_scope(runs[run_index + 1], scope_id))
                        layout.trailing_gap += gap;
                }
            }
        }
        for (const std::size_t run_index : run_indices) {
            const auto& layout = layouts[run_index];
            if (layout.cluster_count == 0) continue;
            const auto& run = runs[run_index];
            const float base_spacing =
                run.letter_spacing * static_cast<float>(layout.cluster_count - 1) +
                run.word_spacing * static_cast<float>(layout.word_gap_count);
            auto& resolved = runs[run_index];
            resolved.text_length_resolved = true;
            resolved.resolved_text_length_scale = layout.scale;
            resolved.resolved_text_length_gap = layout.gap;
            resolved.resolved_text_length_trailing_gap = layout.trailing_gap;
            text_metrics[run_index].advance =
                layout.base_advance * layout.scale +
                base_spacing * layout.scale +
                layout.gap * static_cast<float>(layout.cluster_count - 1) +
                layout.trailing_gap;
            text_metrics[run_index].ink_width =
                layout.base_ink_width * layout.scale;
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<PlanDecodeError> append_svg_text_runs(
    LayerPlan& layer, const RenderPlan& plan,
    chronon3d::assets::AssetResolver& resolver, std::size_t layer_index,
    ShapeContentPlan& shape,
    const std::vector<chronon3d::assets::SvgTextRun>& text_runs_input,
    const std::vector<chronon3d::assets::SvgStyledElement>& elements,
    std::unique_ptr<chronon3d::FontEngine>& font_engine,
    std::vector<SvgTextMetrics>& text_metrics,
    const std::vector<bool>& text_chunk_boundary_safe,
    std::vector<std::size_t>& current_text_layers,
    std::vector<std::pair<chronon3d::assets::SvgTextRun, LayerPlan>>& pending_svg_text_layers,
    std::vector<SvgTextMetrics>& pending_svg_text_metrics,
    std::vector<bool>& pending_svg_text_boundary_before) {
    std::vector<chronon3d::assets::SvgTextRun> resolved_runs = text_runs_input;
    if (auto error = resolve_svg_text_length_scopes(
            resolved_runs, text_metrics, resolver, font_engine, layer_index))
        return error;
    const auto& text_runs = resolved_runs;
    const auto group_key_for = [&](const chronon3d::assets::SvgTextRun& run,
                                   std::size_t index) {
        return layer.id + "#" + (run.text_element_id.empty()
            ? (run.source_id.empty() ? "run_" + std::to_string(index) : run.source_id)
            : run.text_element_id);
    };
    std::unordered_map<std::string, float> text_anchor_advances;
        text_anchor_advances.clear();
        for (std::size_t text_index = 0; text_index < text_runs.size(); ++text_index) {
            const auto& run = text_runs[text_index];
            const auto anchor_scope = run.text_anchor_scope_id.empty()
                ? group_key_for(run, text_index) : layer.id + "#anchor#" + run.text_anchor_scope_id;
            text_anchor_advances[anchor_scope] += text_metrics[text_index].advance;
        }
        std::unordered_map<std::string, std::array<float, 2>> text_group_cursors;
        std::unordered_set<std::string> started_text_anchor_scopes;
        for (std::size_t text_index = 0; text_index < text_runs.size(); ++text_index) {
            const auto& run = text_runs[text_index];
            const auto group_key = group_key_for(run, text_index);
            const auto cursor_it = text_group_cursors.find(group_key);
            const bool first_run = cursor_it == text_group_cursors.end();
            float run_x = run.x_explicit ? run.x.value_or(0.0f)
                : first_run ? 0.0f : cursor_it->second[0];
            float baseline_y = run.y_explicit ? run.y.value_or(0.0f)
                : first_run ? 0.0f : cursor_it->second[1];
            run_x += run.dx;
            baseline_y += run.dy + run.baseline_shift;
            if (run.dominant_baseline == "auto" ||
                run.dominant_baseline == "use-script")
                baseline_y += text_metrics[text_index].script_baseline_offset.value_or(0.0f);
            else if (run.dominant_baseline == "central")
                baseline_y += text_metrics[text_index].central_baseline_offset.value_or(
                    (text_metrics[text_index].ascent -
                     text_metrics[text_index].descent) * 0.5f);
            else if (run.dominant_baseline == "middle")
                baseline_y += text_metrics[text_index].x_height * 0.5f;
            else if (run.dominant_baseline == "hanging")
                baseline_y += text_metrics[text_index].hanging_baseline_offset.value_or(
                    text_metrics[text_index].ascent * 0.8f);
            else if (run.dominant_baseline == "ideographic")
                baseline_y += text_metrics[text_index].ideographic_baseline_offset.value_or(
                    text_metrics[text_index].descent);
            else if (run.dominant_baseline == "mathematical")
                baseline_y += text_metrics[text_index].mathematical_baseline_offset.value_or(
                    text_metrics[text_index].cap_height * 0.5f);
            else if (run.dominant_baseline == "text-before-edge")
                baseline_y += text_metrics[text_index].before_edge_baseline_offset.value_or(
                    text_metrics[text_index].ascent);
            else if (run.dominant_baseline == "text-after-edge")
                baseline_y += text_metrics[text_index].after_edge_baseline_offset.value_or(
                    -text_metrics[text_index].descent);
            const auto anchor_scope = run.text_anchor_scope_id.empty()
                ? group_key : layer.id + "#anchor#" + run.text_anchor_scope_id;
            const bool first_anchor_run = started_text_anchor_scopes.insert(anchor_scope).second;
            const bool apply_anchor = run.text_length_resolved
                ? first_anchor_run : (first_anchor_run || run.x_explicit);
            if (apply_anchor) {
                const float anchor_advance = run.text_length_resolved
                    ? text_anchor_advances[anchor_scope]
                    : (run.x_explicit ? text_metrics[text_index].advance
                                      : text_anchor_advances[anchor_scope]);
                if (run.text_anchor == "middle") run_x -= anchor_advance * 0.5f;
                else if (run.text_anchor == "end") run_x -= anchor_advance;
            }
            text_group_cursors[group_key] = {
                run_x + text_metrics[text_index].advance, baseline_y};
            LayerPlan text_layer;
            const std::string authored_id = run.source_id.empty()
                ? "run_" + std::to_string(text_index) : run.source_id;
            text_layer.id = layer.id + "__svgtext_" + authored_id + "_" +
                std::to_string(text_index);
            text_layer.type = LayerType::Text;
            text_layer.text = run.text;
            text_layer.position_dimensions = 3;
            text_layer.position[0] = layer.position[0] + run_x +
                text_metrics[text_index].advance * 0.5f;
            text_layer.position[1] = layer.position[1] + baseline_y -
                text_metrics[text_index].ascent +
                text_metrics[text_index].line_height * 0.5f;
            text_layer.position[2] = layer.position[2];
            text_layer.size_dimensions = 2;
            text_layer.size = {std::max(1.0f,
                std::max(text_metrics[text_index].advance,
                         text_metrics[text_index].ink_width) + 1.0f),
                text_metrics[text_index].line_height};
            text_layer.enable_3d = layer.enable_3d;
            text_layer.scale = layer.scale;
            text_layer.scale_dimensions = layer.scale_dimensions;
            text_layer.rotation = layer.rotation;
            text_layer.rotation_dimensions = layer.rotation_dimensions;
            text_layer.opacity = std::clamp(run.opacity * layer.opacity.value_or(1.0f), 0.0f, 1.0f);
            LayerStylePlan text_style;
            text_style.font_family = run.font_family;
            text_style.font_weight = run.font_weight;
            text_style.font_style = run.font_style;
            text_style.text_direction = run.direction;
            text_style.text_align = "left";
            text_style.font_size = run.font_size;
            text_style.fill = run.fill;
            text_style.fill_gradient = run.fill_gradient;
            text_style.fill_pattern = run.fill_pattern;
            text_style.fill_opacity = run.fill_opacity;
            if (run.stroke && run.stroke_width > 0.0f) {
                text_style.stroke = StrokeStyle{.color = *run.stroke,
                    .width = run.stroke_width,
                    .gradient = run.stroke_gradient,
                    .dash_array = run.stroke_dash_array,
                    .dash_offset = run.stroke_dash_offset,
                    .cap = run.stroke_cap == LineCap::Round ? "round"
                        : run.stroke_cap == LineCap::Square ? "square" : "butt",
                    .join = run.stroke_join == LineJoin::Round ? "round"
                        : run.stroke_join == LineJoin::Bevel ? "bevel" : "miter",
                    .miter_limit = run.stroke_miter_limit,
                    .opacity = run.stroke_opacity};
                text_style.stroke_pattern = run.stroke_pattern;
            }
            text_style.text_fit_mode = "none";
            text_layer.font = text_metrics[text_index].font_asset;
            text_layer.style = std::move(text_style);
            if (run.text_path_id && run.text_length)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                    "].shape.asset", "SVG textLength on textPath is unsupported by this bounded importer",
                    "svg_text_length_text_path_unsupported", "render-plan-svg"};
            if (!run.text_path_id) {
                const bool has_coordinate_lists = !run.x_coordinates.empty() ||
                    !run.y_coordinates.empty() || !run.dx_coordinates.empty() ||
                    !run.dy_coordinates.empty() ||
                    !run.transformed_position_coordinates.empty() ||
                    !run.transformed_delta_coordinates.empty() ||
                    !run.rotate_degrees.empty() || run.letter_spacing != 0.0f ||
                    run.word_spacing != 0.0f || run.text_length.has_value();
                if (run.text_length && (!run.x_coordinates.empty() ||
                    !run.y_coordinates.empty() || !run.dx_coordinates.empty() ||
                    !run.dy_coordinates.empty() ||
                    !run.transformed_position_coordinates.empty() ||
                    !run.transformed_delta_coordinates.empty()))
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                        "].shape.asset", "SVG textLength cannot be combined with explicit coordinate lists in this bounded importer",
                        "svg_text_length_positioning_unsupported", "render-plan-svg"};
                if (has_coordinate_lists) {
                    chronon3d::FontSpec coordinate_font;
                    coordinate_font.font_path = text_metrics[text_index].font_asset;
                    if (const auto path = resolver.resolve_logical(coordinate_font.font_path))
                        coordinate_font.font_path = path->string();
                    coordinate_font.font_family = run.font_family;
                    coordinate_font.font_weight = run.font_weight;
                    coordinate_font.font_style = run.font_style;
                    coordinate_font.font_size = run.font_size;
                    const auto shaped = font_engine->shape_text(
                        run.text, coordinate_font, run.font_size);
                    if (!shaped)
                        return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                            "].shape.asset", "SVG positioned text could not be shaped",
                            "svg_text_invalid", "render-plan-svg"};
                    const auto placed = chronon3d::resolve_placed_glyph_run(
                        *shaped, 0.0f, run.text);
                    float cursor_x = run_x;
                    float cursor_y = baseline_y;
                    std::size_t nonempty_cluster_count = 0;
                    std::size_t word_gap_count = 0;
                    std::size_t last_nonempty_cluster = 0;
                    for (std::size_t cluster_index = 0;
                         cluster_index < placed.clusters.size(); ++cluster_index) {
                        const auto& cluster = placed.clusters[cluster_index];
                        if (cluster.byte_len == 0 ||
                            cluster.byte_offset + cluster.byte_len > run.text.size())
                            continue;
                        ++nonempty_cluster_count;
                        last_nonempty_cluster = cluster_index;
                    }
                    for (std::size_t cluster_index = 0;
                         cluster_index < placed.clusters.size(); ++cluster_index) {
                        const auto& cluster = placed.clusters[cluster_index];
                        if (cluster.byte_len == 0 ||
                            cluster.byte_offset + cluster.byte_len > run.text.size())
                            continue;
                        const auto cluster_text = run.text.substr(
                            cluster.byte_offset, cluster.byte_len);
                        if ((cluster_text == " " || cluster_text == "\t" ||
                             cluster_text == "\n" || cluster_text == "\r") &&
                            cluster_index != last_nonempty_cluster)
                            ++word_gap_count;
                    }
                    const float base_spacing = nonempty_cluster_count > 1
                        ? run.letter_spacing * static_cast<float>(nonempty_cluster_count - 1) +
                            run.word_spacing * static_cast<float>(word_gap_count)
                        : 0.0f;
                    const float text_length_scale = run.text_length_resolved
                        ? run.resolved_text_length_scale
                        : (run.text_length && run.length_adjust == "spacingAndGlyphs" &&
                           placed.total_width + base_spacing > 0.0f
                            ? *run.text_length / (placed.total_width + base_spacing)
                            : 1.0f);
                    const float text_length_gap = run.text_length_resolved
                        ? run.resolved_text_length_gap
                        : (run.text_length && run.length_adjust == "spacing" &&
                           nonempty_cluster_count > 1
                            ? (*run.text_length - placed.total_width - base_spacing) /
                                static_cast<float>(nonempty_cluster_count - 1)
                            : 0.0f);
                    std::size_t character_index = 0;
                    const auto utf8_count = [](std::string_view value) {
                        std::size_t count = 0;
                        for (const unsigned char byte : value)
                            if ((byte & 0xc0u) != 0x80u) ++count;
                        return count;
                    };
                    for (std::size_t cluster_index = 0;
                         cluster_index < placed.clusters.size(); ++cluster_index) {
                        const auto& cluster = placed.clusters[cluster_index];
                        if (cluster.byte_len == 0 ||
                            cluster.byte_offset + cluster.byte_len > run.text.size())
                            continue;
                        const std::size_t cluster_characters = utf8_count(run.text.substr(
                            cluster.byte_offset, cluster.byte_len));
                        if (cluster_characters == 0) continue;
                        const auto has_inner_coordinate = [&](const std::vector<float>& coordinates) {
                            const auto first_inner = character_index + 1;
                            const auto end = character_index + cluster_characters;
                            return std::any_of(coordinates.begin() + std::min(first_inner, coordinates.size()),
                                coordinates.begin() + std::min(end, coordinates.size()),
                                [](float) { return true; });
                        };
                        if (has_inner_coordinate(run.x_coordinates) ||
                            has_inner_coordinate(run.y_coordinates) ||
                            has_inner_coordinate(run.dx_coordinates) ||
                            has_inner_coordinate(run.dy_coordinates))
                            return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                                "].shape.asset", "SVG coordinate list splits a shaped text cluster; split the run at that character",
                                "svg_text_coordinate_cluster_unsupported", "render-plan-svg"};
                        const auto has_inner_transformed = [&](const auto& coordinates) {
                            const auto first_inner = character_index + 1;
                            const auto end = character_index + cluster_characters;
                            return std::any_of(coordinates.begin() + std::min(first_inner, coordinates.size()),
                                coordinates.begin() + std::min(end, coordinates.size()),
                                [](const auto&) { return true; });
                        };
                        if (has_inner_transformed(run.transformed_position_coordinates) ||
                            has_inner_transformed(run.transformed_delta_coordinates))
                            return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                                "].shape.asset", "SVG coordinate list splits a shaped text cluster; split the run at that character",
                                "svg_text_coordinate_cluster_unsupported", "render-plan-svg"};
                        if (character_index < run.transformed_position_coordinates.size()) {
                            cursor_x = run.transformed_position_coordinates[character_index][0];
                            cursor_y = run.transformed_position_coordinates[character_index][1];
                        } else {
                            if (character_index < run.x_coordinates.size())
                            cursor_x = run.x_coordinates[character_index];
                            if (character_index < run.y_coordinates.size())
                            cursor_y = run.y_coordinates[character_index];
                        }
                        if (character_index < run.dx_coordinates.size())
                            cursor_x += run.dx_coordinates[character_index];
                        if (character_index < run.dy_coordinates.size())
                            cursor_y += run.dy_coordinates[character_index];
                        if (character_index < run.transformed_delta_coordinates.size()) {
                            cursor_x += run.transformed_delta_coordinates[character_index][0];
                            cursor_y += run.transformed_delta_coordinates[character_index][1];
                        }
                        const std::string cluster_text = run.text.substr(
                            cluster.byte_offset, cluster.byte_len);
                        auto cluster_run = run;
                        cluster_run.text = cluster_text;
                        cluster_run.positioned_text_cluster = true;
                        auto cluster_layer = text_layer;
                        cluster_layer.id += "_cluster_" + std::to_string(cluster_index);
                        cluster_layer.text = cluster_text;
                        float anchored_x = cursor_x;
                        const bool starts_positioned_chunk =
                            character_index < run.x_coordinates.size() ||
                            character_index < run.y_coordinates.size() ||
                            character_index < run.transformed_position_coordinates.size() ||
                            (!run.text_length_resolved && run.text_length.has_value() &&
                             character_index == 0);
                        if (starts_positioned_chunk) {
                            const float cluster_advance = cluster.advance * text_length_scale;
                            float chunk_advance = cluster_advance;
                            std::size_t next_character = character_index + cluster_characters;
                            for (std::size_t next_cluster_index = cluster_index + 1;
                                 next_cluster_index < placed.clusters.size(); ++next_cluster_index) {
                                const auto& next_cluster = placed.clusters[next_cluster_index];
                                if (next_cluster.byte_len == 0 ||
                                    next_cluster.byte_offset + next_cluster.byte_len > run.text.size())
                                    continue;
                                if (next_character < run.x_coordinates.size() ||
                                    next_character < run.y_coordinates.size() ||
                                    next_character < run.transformed_position_coordinates.size())
                                    break;
                                chunk_advance += next_cluster.advance * text_length_scale;
                                next_character += utf8_count(run.text.substr(
                                    next_cluster.byte_offset, next_cluster.byte_len));
                            }
                            if (run.text_anchor == "middle") anchored_x -= chunk_advance * 0.5f;
                            else if (run.text_anchor == "end") anchored_x -= chunk_advance;
                        }
                        const float cluster_advance =
                            cluster.advance * text_length_scale;
                        // The cluster frame is left-aligned inside a box whose
                        // centre is layer.position, and the layer scale expands
                        // around that centre. Compensate the half-box growth so
                        // the visual left edge stays at anchored_x.
                        const float glyph_scale_compensation =
                            (text_length_scale - 1.0f) * cluster_advance * 0.5f;
                        cluster_layer.position[0] = layer.position[0] + anchored_x +
                            cluster_advance * 0.5f + glyph_scale_compensation;
                        cluster_layer.position[1] = layer.position[1] + cursor_y -
                            text_metrics[text_index].ascent +
                            text_metrics[text_index].line_height * 0.5f;
                        cluster_layer.size[0] = std::max(1.0f, cluster_advance + 1.0f);
                        const bool scale_text_length_glyphs = run.text_length_resolved
                            ? run.resolved_text_length_scale != 1.0f
                            : (run.text_length &&
                               run.length_adjust == "spacingAndGlyphs");
                        if (scale_text_length_glyphs) {
                            cluster_layer.scale[0] *= text_length_scale;
                            cluster_layer.scale_dimensions = std::max<std::size_t>(
                                cluster_layer.scale_dimensions, 2);
                        }
                        if (character_index < run.rotate_degrees.size()) {
                            cluster_layer.rotation[2] += run.rotate_degrees[character_index];
                            cluster_layer.rotation_dimensions = 3;
                        } else if (!run.rotate_degrees.empty()) {
                            cluster_layer.rotation[2] += run.rotate_degrees.back();
                            cluster_layer.rotation_dimensions = 3;
                        }
                        current_text_layers.push_back(pending_svg_text_layers.size());
                        pending_svg_text_layers.emplace_back(std::move(cluster_run),
                                                             std::move(cluster_layer));
                        pending_svg_text_metrics.push_back(text_metrics[text_index]);
                        pending_svg_text_boundary_before.push_back(
                            text_index == 0 || text_chunk_boundary_safe[text_index]);
                        float spacing = 0.0f;
                        if (cluster_index + 1 < placed.clusters.size()) {
                            spacing += run.letter_spacing * text_length_scale +
                                text_length_gap;
                            if (cluster_text == " " || cluster_text == "\t" ||
                                cluster_text == "\n" || cluster_text == "\r")
                                spacing += run.word_spacing * text_length_scale;
                        }
                        cursor_x += cluster_advance + spacing;
                        character_index += cluster_characters;
                    }
                    continue;
                }
                current_text_layers.push_back(pending_svg_text_layers.size());
                pending_svg_text_layers.emplace_back(run, std::move(text_layer));
                pending_svg_text_metrics.push_back(text_metrics[text_index]);
                pending_svg_text_boundary_before.push_back(
                    text_index == 0 || text_chunk_boundary_safe[text_index]);
                continue;
            }
            const chronon3d::assets::SvgStyledElement* target_path = nullptr;
            chronon3d::PathShape text_path_geometry;
            std::size_t target_path_matches = 0;
            for (const auto& element : elements) {
                if (element.source_id == *run.text_path_id) {
                    target_path = &element;
                    ++target_path_matches;
                }
            }
            if (target_path_matches > 1)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                    "].shape.asset", "SVG textPath target is ambiguous after path expansion: " +
                    *run.text_path_id,
                    "svg_target_ambiguous", "render-plan-svg"};
            if (target_path_matches == 1 && target_path) {
                text_path_geometry = target_path->path;
                if (target_path->group_ids != run.coordinate_scope_ids)
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                        "].shape.asset", "SVG textPath and target path must share the same group coordinate scope",
                        "svg_text_path_scope_invalid", "render-plan-svg"};
                if (target_path->morph_animation || !target_path->animations.empty())
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                        "].shape.asset", "SVG textPath target paths must be static",
                        "svg_text_path_animated_target", "render-plan-svg"};
            } else if (run.text_path_geometry) {
                text_path_geometry = *run.text_path_geometry;
            } else {
                return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                    "].shape.asset", "SVG textPath target must resolve to path geometry: " +
                    *run.text_path_id, "svg_target_missing", "render-plan-svg"};
            }
            if (text_path_geometry.commands.empty())
                return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                    "].shape.asset", "SVG textPath target path has empty geometry",
                    "svg_text_path_invalid", "render-plan-svg"};

            chronon3d::FontSpec path_font;
            path_font.font_path = text_metrics[text_index].font_asset;
            if (const auto path = resolver.resolve_logical(path_font.font_path))
                path_font.font_path = path->string();
            path_font.font_family = run.font_family;
            path_font.font_weight = run.font_weight;
            path_font.font_style = run.font_style;
            path_font.font_size = run.font_size;
            chronon3d::TextShaping path_shaping;
            path_shaping.direction = run.direction == "rtl"
                ? chronon3d::TextDirection::RTL
                : run.direction == "ltr"
                    ? chronon3d::TextDirection::LTR
                    : chronon3d::TextDirection::Auto;
            const auto shaped = font_engine->shape_text(
                run.text, path_font, run.font_size, path_shaping);
            if (!shaped)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                    "].shape.asset", "SVG textPath text could not be shaped",
                    "svg_text_invalid", "render-plan-svg"};
            const auto placed = chronon3d::resolve_placed_glyph_run(
                *shaped, 0.0f, run.text);
            const auto sampler = chronon3d::PathSampler::compile(text_path_geometry);
            chronon3d::TextPathSpec path_spec;
            path_spec.direction = run.direction == "rtl" ? 2
                : run.direction == "ltr" ? 1 : 0;
            const float start_offset = run.text_path_start_offset_percent
                ? run.text_path_start_offset * sampler.total_length() * 0.01f
                : run.text_path_start_offset;
            const float initial_dx = run.dx_coordinates.empty() ? 0.0f : run.dx_coordinates.front();
            const float initial_dy = run.dy_coordinates.empty() ? 0.0f : run.dy_coordinates.front();
            path_spec.first_margin = start_offset + run.dx - initial_dx;
            if (run.text_anchor == "middle")
                path_spec.first_margin -= placed.total_width * 0.5f;
            else if (run.text_anchor == "end")
                path_spec.first_margin -= placed.total_width;
            path_spec.baseline_offset = run.dy - initial_dy + run.baseline_shift;
            if (run.dominant_baseline == "auto" ||
                run.dominant_baseline == "use-script")
                path_spec.baseline_offset += text_metrics[text_index].script_baseline_offset.value_or(0.0f);
            else if (run.dominant_baseline == "central")
                path_spec.baseline_offset += text_metrics[text_index].central_baseline_offset.value_or(
                    (text_metrics[text_index].ascent -
                     text_metrics[text_index].descent) * 0.5f);
            else if (run.dominant_baseline == "middle")
                path_spec.baseline_offset += text_metrics[text_index].x_height * 0.5f;
            else if (run.dominant_baseline == "hanging")
                path_spec.baseline_offset += text_metrics[text_index].hanging_baseline_offset.value_or(
                    text_metrics[text_index].ascent * 0.8f);
            else if (run.dominant_baseline == "ideographic")
                path_spec.baseline_offset += text_metrics[text_index].ideographic_baseline_offset.value_or(
                    text_metrics[text_index].descent);
            else if (run.dominant_baseline == "mathematical")
                path_spec.baseline_offset += text_metrics[text_index].mathematical_baseline_offset.value_or(
                    text_metrics[text_index].cap_height * 0.5f);
            else if (run.dominant_baseline == "text-before-edge")
                path_spec.baseline_offset += text_metrics[text_index].before_edge_baseline_offset.value_or(
                    text_metrics[text_index].ascent);
            else if (run.dominant_baseline == "text-after-edge")
                path_spec.baseline_offset += text_metrics[text_index].after_edge_baseline_offset.value_or(
                    -text_metrics[text_index].descent);
            path_spec.perpendicular_to_path = true;
            const auto path_placement = chronon3d::compose_text_on_path(
                placed, sampler, path_spec, run.text);
            if (path_placement.clusters.size() != placed.clusters.size())
                return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                    "].shape.asset", "SVG textPath cluster placement failed",
                    "svg_text_invalid", "render-plan-svg"};
            std::size_t path_character_index = 0;
            float path_dx_accumulated = 0.0f;
            float path_dy_accumulated = 0.0f;
            const auto path_origin = sampler.sample_distance(0.0f);
            const auto absolute_path_coordinates = [&](std::size_t character_index)
                -> std::optional<std::array<float, 2>> {
                if (character_index < run.transformed_position_coordinates.size()) {
                    const auto& point = run.transformed_position_coordinates[character_index];
                    const float x = point[0] - path_origin.position.x;
                    const float y = point[1] - path_origin.position.y;
                    return std::array<float, 2>{
                        x * path_origin.tangent.x + y * path_origin.tangent.y,
                        x * path_origin.normal.x + y * path_origin.normal.y};
                }
                if (character_index < run.x_coordinates.size() ||
                    character_index < run.y_coordinates.size()) {
                    const float point_x = character_index < run.x_coordinates.size()
                        ? run.x_coordinates[character_index] : path_origin.position.x;
                    const float point_y = character_index < run.y_coordinates.size()
                        ? run.y_coordinates[character_index] : path_origin.position.y;
                    const float x = point_x - path_origin.position.x;
                    const float y = point_y - path_origin.position.y;
                    return std::array<float, 2>{
                        x * path_origin.tangent.x + y * path_origin.tangent.y,
                        x * path_origin.normal.x + y * path_origin.normal.y};
                }
                return std::nullopt;
            };
            const auto transformed_path_delta = [&](std::size_t character_index)
                -> std::optional<std::array<float, 2>> {
                if (character_index >= run.transformed_delta_coordinates.size())
                    return std::nullopt;
                const auto& delta = run.transformed_delta_coordinates[character_index];
                return std::array<float, 2>{
                    delta[0] * path_origin.tangent.x + delta[1] * path_origin.tangent.y,
                    delta[0] * path_origin.normal.x + delta[1] * path_origin.normal.y};
            };
            const auto path_utf8_count = [](std::string_view value) {
                std::size_t count = 0;
                for (const unsigned char byte : value)
                    if ((byte & 0xc0u) != 0x80u) ++count;
                return count;
            };
            const auto path_paint_for = [&](std::size_t byte_offset)
                -> const chronon3d::assets::SvgTextPaintRange* {
                for (const auto& range : run.paint_ranges)
                    if (byte_offset >= range.byte_start &&
                        byte_offset < range.byte_end)
                        return &range;
                return nullptr;
            };
            for (std::size_t cluster_index = 0;
                 cluster_index < placed.clusters.size(); ++cluster_index) {
                const auto& cluster = placed.clusters[cluster_index];
                auto placement = path_placement.clusters[cluster_index];
                if (cluster.byte_len == 0 || cluster.byte_offset + cluster.byte_len > run.text.size())
                    continue;
                const std::string cluster_text = run.text.substr(
                    cluster.byte_offset, cluster.byte_len);
                if (cluster_text.empty()) continue;
                const auto cluster_characters = path_utf8_count(cluster_text);
                const auto has_inner_coordinate = [&](const auto& coordinates) {
                    const auto first_inner = path_character_index + 1;
                    const auto end = path_character_index + cluster_characters;
                    return std::any_of(coordinates.begin() + std::min(first_inner, coordinates.size()),
                        coordinates.begin() + std::min(end, coordinates.size()),
                        [](const auto&) { return true; });
                };
                if (has_inner_coordinate(run.x_coordinates) ||
                    has_inner_coordinate(run.y_coordinates) ||
                    has_inner_coordinate(run.transformed_position_coordinates) ||
                    has_inner_coordinate(run.dx_coordinates) ||
                    has_inner_coordinate(run.dy_coordinates) ||
                    has_inner_coordinate(run.transformed_delta_coordinates))
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                        "].shape.asset", "SVG textPath coordinate list splits a shaped cluster",
                        "svg_text_coordinate_cluster_unsupported", "render-plan-svg"};
                if (path_character_index > 0) {
                    if (path_character_index < run.dx_coordinates.size())
                        path_dx_accumulated += run.dx_coordinates[path_character_index];
                    if (path_character_index < run.dy_coordinates.size())
                        path_dy_accumulated += run.dy_coordinates[path_character_index];
                    if (const auto delta = transformed_path_delta(path_character_index)) {
                        path_dx_accumulated += delta->at(0);
                        path_dy_accumulated += delta->at(1);
                    }
                }
                const auto absolute_position = absolute_path_coordinates(path_character_index);
                const float adjusted_distance = absolute_position
                    ? absolute_position->at(0)
                    : placement.path_distance + path_dx_accumulated;
                if (adjusted_distance > sampler.total_length()) {
                    path_character_index += cluster_characters;
                    continue;
                }
                if (absolute_position || path_dx_accumulated != 0.0f ||
                    path_dy_accumulated != 0.0f) {
                    const auto sample = sampler.sample_distance(adjusted_distance);
                    placement.path_distance = adjusted_distance;
                    placement.position = sample.position + sample.normal *
                        (absolute_position ? absolute_position->at(1) :
                            path_spec.baseline_offset + path_dy_accumulated);
                    placement.rotation_deg = std::atan2(sample.tangent.y,
                        sample.tangent.x) * 57.29577951308232f;
                }
                auto cluster_run = run;
                cluster_run.text = cluster_text;
                auto cluster_layer = text_layer;
                cluster_layer.id += "_cluster_" + std::to_string(cluster_index);
                cluster_layer.text = cluster_text;
                if (const auto* paint = path_paint_for(cluster.byte_offset)) {
                    cluster_run.fill = paint->fill;
                    cluster_run.fill_gradient = paint->fill_gradient;
                    cluster_run.fill_pattern = paint->fill_pattern;
                    cluster_run.fill_opacity = paint->fill_opacity;
                    cluster_run.stroke = paint->stroke;
                    cluster_run.stroke_gradient = paint->stroke_gradient;
                    cluster_run.stroke_pattern = paint->stroke_pattern;
                    cluster_run.stroke_width = paint->stroke_width;
                    cluster_run.stroke_opacity = paint->stroke_opacity;
                    cluster_run.opacity = paint->opacity;
                    cluster_layer.opacity = std::clamp(
                        paint->opacity * layer.opacity.value_or(1.0f), 0.0f, 1.0f);
                    if (cluster_layer.style) {
                        cluster_layer.style->fill = paint->fill;
                        cluster_layer.style->fill_gradient = paint->fill_gradient;
                        cluster_layer.style->fill_pattern = paint->fill_pattern;
                        cluster_layer.style->fill_opacity = paint->fill_opacity;
                        if (paint->stroke && paint->stroke_width > 0.0f) {
                            StrokeStyle stroke = cluster_layer.style->stroke.value_or(
                                StrokeStyle{});
                            stroke.color = *paint->stroke;
                            stroke.width = paint->stroke_width;
                            stroke.gradient = paint->stroke_gradient;
                            stroke.opacity = paint->stroke_opacity;
                            cluster_layer.style->stroke = std::move(stroke);
                            cluster_layer.style->stroke_pattern = paint->stroke_pattern;
                        } else {
                            cluster_layer.style->stroke.reset();
                            cluster_layer.style->stroke_pattern.reset();
                        }
                    }
                }
                cluster_layer.position[0] = layer.position[0] + placement.position.x -
                    cluster.advance * 0.5f;
                cluster_layer.position[1] = layer.position[1] + placement.position.y -
                    text_metrics[text_index].ascent;
                cluster_layer.size[0] = std::max(1.0f, cluster.advance + 1.0f);
                cluster_layer.rotation[2] += placement.rotation_deg;
                if (path_character_index < run.rotate_degrees.size())
                    cluster_layer.rotation[2] += run.rotate_degrees[path_character_index];
                else if (!run.rotate_degrees.empty())
                    cluster_layer.rotation[2] += run.rotate_degrees.back();
                cluster_layer.rotation_dimensions = 3;
                current_text_layers.push_back(pending_svg_text_layers.size());
                pending_svg_text_layers.emplace_back(std::move(cluster_run),
                                                     std::move(cluster_layer));
                pending_svg_text_metrics.push_back(text_metrics[text_index]);
                pending_svg_text_boundary_before.push_back(
                    text_index == 0 || text_chunk_boundary_safe[text_index]);
                if (cluster_index + 1 < placed.clusters.size()) {
                    path_dx_accumulated += run.letter_spacing;
                    if (cluster_text == " " || cluster_text == "\t" ||
                        cluster_text == "\n" || cluster_text == "\r")
                        path_dx_accumulated += run.word_spacing;
                }
                path_character_index += cluster_characters;
            }
        }

    return std::nullopt;
}

} // namespace chronon3d::render_plan::detail
