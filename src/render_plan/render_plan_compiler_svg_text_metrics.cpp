#include "render_plan_compiler_detail.hpp"
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/backends/text/text_unicode_grapheme.hpp>
#include <chronon3d/text/font_engine.hpp>
#include "../text/unicode/unicode_script.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::render_plan::detail {

std::optional<PlanDecodeError> measure_svg_text_runs(
    const std::vector<chronon3d::assets::SvgTextRun>& runs,
    std::size_t layer_index,
    chronon3d::assets::AssetResolver& resolver,
    std::unique_ptr<chronon3d::FontEngine>& font_engine,
    std::vector<SvgTextMetrics>& text_metrics) {
    std::vector<text::unicode::Script> effective_text_scripts(
        runs.size(), text::unicode::unknown_script);
        for (std::size_t text_index = 0; text_index < runs.size(); ++text_index) {
            const auto& run = runs[text_index];
            auto script_code = text::unicode::first_strong_script(run.text);
            if (script_code == text::unicode::unknown_script && text_index > 0) {
                const auto& previous = runs[text_index - 1];
                if (previous.text_element_id == run.text_element_id &&
                    previous.group_ids == run.group_ids &&
                    previous.coordinate_scope_ids == run.coordinate_scope_ids &&
                    previous.font_family == run.font_family &&
                    previous.font_size == run.font_size)
                    script_code = effective_text_scripts[text_index - 1];
            }
            effective_text_scripts[text_index] = script_code;
            if (run.text.empty() || run.font_family.empty() ||
                !std::isfinite(run.font_size) || run.font_size <= 0.0f ||
                !std::isfinite(run.opacity) || run.opacity < 0.0f || run.opacity > 1.0f ||
                !std::isfinite(run.fill_opacity) || run.fill_opacity < 0.0f || run.fill_opacity > 1.0f ||
                !std::isfinite(run.stroke_opacity) || run.stroke_opacity < 0.0f || run.stroke_opacity > 1.0f ||
                !std::isfinite(run.stroke_width) || run.stroke_width < 0.0f ||
                !std::isfinite(run.dx) || !std::isfinite(run.dy) ||
                !std::isfinite(run.baseline_shift))
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                    "SVG text run has invalid content, font size, position, or opacity",
                    "svg_text_invalid", "render-plan-svg"};
            std::string family_file = run.font_family;
            std::replace(family_file.begin(), family_file.end(), ' ', '-');
            if (family_file == "sans-serif" || family_file == "sans" || family_file == "system-ui" ||
                family_file == "Arial" || family_file == "Helvetica") {
                family_file = "Inter";
            } else if (family_file == "serif" || family_file == "Times" || family_file == "Times-New-Roman") {
                family_file = "FreeSerif";
            }
            if (family_file.empty() || family_file.find_first_of("/\\:") != std::string::npos)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                    "SVG font-family is not a safe bundled font name",
                    "svg_font_unresolved", "render-plan-svg"};
            const std::string face = run.font_weight >= 600
                ? (run.font_style == "normal" ? "Bold" : "BoldItalic")
                : (run.font_style == "normal" ? "Regular" : "Italic");
            const std::array<std::string, 6> font_candidates{
                "fonts/" + family_file + "-" + face + ".ttf",
                "fonts/" + family_file + "-" + face + ".otf",
                "fonts/" + family_file + "-Regular.ttf",
                "fonts/" + family_file + ".ttf",
                "fonts/Inter-" + face + ".ttf",
                "fonts/Inter-Regular.ttf"};
            std::optional<std::filesystem::path> resolved_font;
            std::string font_asset;
            for (const auto& candidate : font_candidates) {
                resolved_font = resolver.resolve_logical(std::filesystem::path(candidate));
                if (resolved_font) { font_asset = candidate; break; }
            }
            if (!resolved_font)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                    "SVG font-family has no matching font asset under fonts/: " + run.font_family,
                    "svg_font_unresolved", "render-plan-svg"};
            if (!font_engine)
                font_engine = std::make_unique<chronon3d::FontEngine>(resolver);
            chronon3d::FontSpec font_spec;
            font_spec.font_path = resolved_font->string();
            font_spec.font_family = run.font_family;
            font_spec.font_weight = run.font_weight;
            font_spec.font_style = run.font_style;
            font_spec.font_size = run.font_size;
            const float advance = font_engine->measure_text(
                run.text, font_spec, run.font_size);
            const auto metrics = font_engine->get_font_metrics(
                font_spec, run.font_size);
            if (!std::isfinite(advance) || advance < 0.0f ||
                !std::isfinite(metrics.ascent) || metrics.ascent <= 0.0f ||
                !std::isfinite(metrics.descent) || metrics.descent < 0.0f ||
                !std::isfinite(metrics.x_height) || metrics.x_height < 0.0f ||
                !std::isfinite(metrics.cap_height) || metrics.cap_height < 0.0f ||
                !std::isfinite(metrics.line_height) || metrics.line_height <= 0.0f)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                    "SVG text could not be measured with its resolved font asset",
                    "svg_font_unresolved", "render-plan-svg"};
            auto& resolved_metrics = text_metrics[text_index];
            resolved_metrics.font_asset = font_asset;
            resolved_metrics.advance = advance;
            resolved_metrics.ink_width = advance;
            resolved_metrics.ascent = metrics.ascent;
            resolved_metrics.descent = metrics.descent;
            resolved_metrics.x_height = metrics.x_height;
            resolved_metrics.cap_height = metrics.cap_height;
            resolved_metrics.line_height = metrics.line_height;
            if (script_code != text::unicode::unknown_script) {
                const char* script_name = text::unicode::short_name(script_code);
                if (script_name && std::strlen(script_name) == 4) {
                    const bool rtl_baseline = run.direction == "rtl" ||
                        (run.direction != "ltr" &&
                         text::unicode::is_right_to_left_script(script_code));
                    const auto baseline_offset = [&](std::string_view baseline) {
                        return font_engine->get_open_type_baseline_offset(
                            font_spec, run.font_size, baseline, script_name,
                            rtl_baseline
                                ? chronon3d::TextDirection::RTL
                                : chronon3d::TextDirection::LTR);
                    };
                    resolved_metrics.script_baseline_offset = baseline_offset("auto");
                    resolved_metrics.central_baseline_offset = baseline_offset("central");
                    resolved_metrics.hanging_baseline_offset = baseline_offset("hanging");
                    resolved_metrics.ideographic_baseline_offset = baseline_offset("ideographic");
                    resolved_metrics.mathematical_baseline_offset = baseline_offset("mathematical");
                    resolved_metrics.before_edge_baseline_offset = baseline_offset("text-before-edge");
                    resolved_metrics.after_edge_baseline_offset = baseline_offset("text-after-edge");
                }
            }
        }

    return std::nullopt;
}

} // namespace chronon3d::render_plan::detail
