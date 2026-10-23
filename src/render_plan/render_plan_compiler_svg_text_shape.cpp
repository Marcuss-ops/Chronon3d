#include "render_plan_compiler_detail.hpp"
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/backends/text/text_unicode_grapheme.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_path_composer.hpp>
#include "../text/unicode/unicode_script.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d::render_plan::detail {

std::optional<PlanDecodeError> shape_svg_text_runs(
    const std::vector<chronon3d::assets::SvgTextRun>& runs,
    const std::string& layer_id,
    chronon3d::assets::AssetResolver& resolver,
    std::unique_ptr<chronon3d::FontEngine>& font_engine,
    std::vector<SvgTextMetrics>& text_metrics,
    std::vector<bool>& text_chunk_boundary_safe) {
    const auto group_key_for = [&](const chronon3d::assets::SvgTextRun& run,
                                   std::size_t index) {
        return layer_id + "#" + (run.text_element_id.empty()
            ? (run.source_id.empty() ? "run_" + std::to_string(index) : run.source_id)
            : run.text_element_id);
    };
        const auto has_no_text_positioning = [](const auto& run) {
            return run.dx == 0.0f && run.dy == 0.0f && run.x_coordinates.empty() &&
                run.y_coordinates.empty() && run.dx_coordinates.empty() &&
                run.dy_coordinates.empty() &&
                run.transformed_position_coordinates.empty() &&
                run.transformed_delta_coordinates.empty() && run.rotate_degrees.empty() &&
                run.letter_spacing == 0.0f && run.word_spacing == 0.0f &&
                !run.text_length && !run.text_path_id;
        };
        const auto same_shaping_scope = [&](std::size_t left, std::size_t right) {
            const auto& a = runs[left];
            const auto& b = runs[right];
            return group_key_for(a, left) == group_key_for(b, right) &&
                a.coordinate_scope_ids == b.coordinate_scope_ids &&
                a.text_anchor_scope_id == b.text_anchor_scope_id &&
                !b.x_explicit && !b.y_explicit &&
                a.font_family == b.font_family && a.font_size == b.font_size &&
                a.font_weight == b.font_weight && a.font_style == b.font_style &&
                a.direction == b.direction &&
                a.text_anchor == b.text_anchor &&
                a.dominant_baseline == b.dominant_baseline &&
                a.baseline_shift == b.baseline_shift &&
                has_no_text_positioning(a) && has_no_text_positioning(b);
        };
        for (std::size_t begin = 0; begin < runs.size();) {
            std::size_t end = begin + 1;
            while (end < runs.size() &&
                   same_shaping_scope(end - 1, end)) ++end;
            if (has_no_text_positioning(runs[begin])) {
                const auto& first = runs[begin];
                chronon3d::FontSpec font_spec;
                font_spec.font_path = text_metrics[begin].font_asset;
                if (const auto path = resolver.resolve_logical(font_spec.font_path))
                    font_spec.font_path = path->string();
                font_spec.font_family = first.font_family;
                font_spec.font_weight = first.font_weight;
                font_spec.font_style = first.font_style;
                font_spec.font_size = first.font_size;
                std::string combined;
                std::vector<std::size_t> run_start_bytes(end - begin, 0);
                for (std::size_t index = begin; index < end; ++index) {
                    if (index > begin)
                        run_start_bytes[index - begin] = combined.size();
                    combined += runs[index].text;
                }
                bool chunk_boundaries_safe = false;
                std::optional<text::unicode::Script> previous_script;
                bool mixed_scripts = false;
                for (std::size_t index = begin; index < end; ++index) {
                    const auto script = text::unicode::first_strong_script(
                        runs[index].text);
                    if (script == text::unicode::unknown_script) continue;
                    if (previous_script && *previous_script != script)
                        mixed_scripts = true;
                    previous_script = script;
                }
                const auto primary_script = text::unicode::first_strong_script(combined);
                const bool right_to_left_script =
                    text::unicode::is_right_to_left_script(primary_script);
                if (mixed_scripts) {
                    // A single HarfBuzz buffer guesses one script from its
                    // first strong character. Measure the authored chunks
                    // independently across script boundaries; the canonical
                    // text shaper joins their glyphs with these byte ranges.
                    chunk_boundaries_safe = true;
                    for (std::size_t index = begin; index < end; ++index) {
                        const auto& run = runs[index];
                        const auto shaped_run = font_engine->shape_text(
                            run.text, font_spec, first.font_size);
                        if (!shaped_run) {
                            chunk_boundaries_safe = false;
                            break;
                        }
                        const auto placed = chronon3d::resolve_placed_glyph_run(
                            *shaped_run, 0.0f, run.text);
                        text_metrics[index].advance = placed.total_width;
                        float ink_right = 0.0f;
                        for (const auto& glyph : placed.glyphs)
                            ink_right = std::max(ink_right,
                                glyph.x + glyph.bbox_x1);
                        text_metrics[index].ink_width = std::max(
                            placed.total_width, ink_right);
                    }
                } else if (right_to_left_script) {
                    // RTL clusters are stored in logical byte order while
                    // their glyph positions run in visual order. Derive
                    // paint-chunk widths from logical cluster advances;
                    // x positions are not monotone and cannot be used as
                    // chunk boundaries as they are for LTR text.
                    const auto shaped = font_engine->shape_text(
                        combined, font_spec, first.font_size);
                    if (shaped) {
                        const auto placed = chronon3d::resolve_placed_glyph_run(
                            *shaped, 0.0f, combined);
                        std::vector<std::size_t> byte_boundaries = run_start_bytes;
                        byte_boundaries.push_back(0);
                        byte_boundaries.push_back(combined.size());
                        std::sort(byte_boundaries.begin(), byte_boundaries.end());
                        byte_boundaries.erase(std::unique(byte_boundaries.begin(),
                            byte_boundaries.end()), byte_boundaries.end());
                        bool complete_boundaries = true;
                        for (std::size_t offset = 1; offset + 1 < byte_boundaries.size(); ++offset) {
                            if (std::none_of(placed.clusters.begin(), placed.clusters.end(),
                                [&](const auto& cluster) {
                                    return cluster.byte_offset == byte_boundaries[offset];
                                })) {
                                complete_boundaries = false;
                                break;
                            }
                        }
                        if (complete_boundaries) {
                            chunk_boundaries_safe = true;
                            for (std::size_t offset = 0; offset < run_start_bytes.size(); ++offset) {
                                const auto byte_start = run_start_bytes[offset];
                                const auto byte_end = offset + 1 < run_start_bytes.size()
                                    ? run_start_bytes[offset + 1] : combined.size();
                                float advance = 0.0f;
                                for (const auto& cluster : placed.clusters)
                                    if (cluster.byte_offset >= byte_start &&
                                        cluster.byte_offset < byte_end)
                                        advance += cluster.advance;
                                text_metrics[begin + offset].advance = advance;
                                text_metrics[begin + offset].ink_width = advance;
                            }
                        }
                    }
                } else {
                const auto shaped = font_engine->shape_text(
                    combined, font_spec, first.font_size);
                if (shaped) {
                    const auto placed = chronon3d::resolve_placed_glyph_run(
                        *shaped, 0.0f, combined);
                    std::vector<std::optional<float>> run_starts(end - begin);
                    run_starts.front() = 0.0f;
                    bool complete_boundaries = true;
                    bool safe_grapheme_boundaries = true;
                    for (std::size_t offset = 1; offset < run_starts.size(); ++offset) {
                        const auto byte = run_start_bytes[offset];
                        const auto cluster = std::find_if(placed.clusters.begin(),
                            placed.clusters.end(), [byte](const auto& candidate) {
                                return candidate.byte_offset >= byte;
                            });
                        if (cluster == placed.clusters.end() ||
                            cluster->byte_offset != byte ||
                            cluster->start_glyph >= placed.glyphs.size()) {
                            complete_boundaries = false;
                            bool is_grapheme_edge = false;
                            const auto graphemes = chronon3d::detail::grapheme_cluster_count(combined);
                            for (std::size_t grapheme = 0; grapheme <= graphemes; ++grapheme) {
                                if (chronon3d::detail::grapheme_byte_offset_at(combined, grapheme) == byte) {
                                    is_grapheme_edge = true;
                                    break;
                                }
                            }
                            if (is_grapheme_edge) safe_grapheme_boundaries = false;
                            continue;
                        }
                        run_starts[offset] = placed.glyphs[cluster->start_glyph].x;
                    }
                    if (!complete_boundaries && safe_grapheme_boundaries) {
                        // The paint boundary falls inside one extended
                        // grapheme. Keep the authored paint spans together
                        // so shaping can retain the indivisible glyph
                        // cluster instead of reshaping its code points.
                        chunk_boundaries_safe = true;
                    } else if (complete_boundaries && std::isfinite(placed.total_width)) {
                        chunk_boundaries_safe = true;
                        for (std::size_t offset = 0; offset < run_starts.size(); ++offset) {
                            const float next = offset + 1 < run_starts.size()
                                ? *run_starts[offset + 1] : placed.total_width;
                            if (next >= *run_starts[offset]) {
                                text_metrics[begin + offset].advance =
                                    next - *run_starts[offset];
                                float ink_right = 0.0f;
                                const std::size_t byte_start = run_start_bytes[offset];
                                const std::size_t byte_end = offset + 1 < run_start_bytes.size()
                                    ? run_start_bytes[offset + 1] : combined.size();
                                for (const auto& glyph : placed.glyphs) {
                                    if (glyph.byte_offset < byte_start ||
                                        glyph.byte_offset >= byte_end) continue;
                                    ink_right = std::max(ink_right,
                                        glyph.x + glyph.bbox_x1 - *run_starts[offset]);
                                }
                                text_metrics[begin + offset].ink_width = std::max(
                                    text_metrics[begin + offset].advance, ink_right);
                            }
                        }
                    }
                }
                }
                if (!chunk_boundaries_safe)
                    for (std::size_t index = begin + 1; index < end; ++index)
                        text_chunk_boundary_safe[index] = false;
            }
            begin = end;
        }

    return std::nullopt;
}

} // namespace chronon3d::render_plan::detail
