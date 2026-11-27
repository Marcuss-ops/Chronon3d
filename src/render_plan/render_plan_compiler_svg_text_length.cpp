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


} // namespace

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


} // namespace chronon3d::render_plan::detail
