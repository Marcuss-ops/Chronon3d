#pragma once

#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/backends/text/bidi_segmenter.hpp>

// ── Extracted modules (keep includes before namespace block) ──────
// text_unicode_utils.hpp: UTF-8 codec + grapheme cluster (chronon3d::detail)
// text_layout_types.hpp: TextLayoutRun/Input/Line/Result data structures
#include "text_unicode_utils.hpp"
#include "text_layout_types.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d {

class TextLayoutEngine {
private:
    static float measure_char_legacy(const TextLayoutInput& input, char c, float font_size) {
        if (input.char_width_fn) {
            return std::max(0.0f, input.char_width_fn(input.char_width_ctx, c, font_size));
        }
        return font_size * 0.6f;
    }

    static float measure_string_legacy(const TextLayoutInput& input, std::string_view s, float font_size) {
        float width = 0.0f;
        const size_t cp_count = detail::utf8_length(s);
        // Legacy path uses per-char callback; iterate byte-by-byte for ASCII
        // since legacy callbacks only support single-byte chars anyway
        for (char c : s) {
            width += measure_char_legacy(input, c, font_size);
        }
        const size_t n_clusters2 = detail::grapheme_cluster_count(s);
        width += input.style.tracking * static_cast<float>(n_clusters2 > 0 ? n_clusters2 - 1 : 0);
        return std::max(0.0f, width);
    }

    // Fast single-character width lookup using precomputed ASCII table.
    // Prefers Blend2D when available (same engine as rasterization).
    // Falls back to font_engine measurement for non-ASCII or font mismatch.
    static float measure_single_char(char c, bool have_precomputed,
                                      const std::array<float, 256>& char_widths,
                                      const TextLayoutInput& input,
                                      const TextStyle& style, float font_size) {
        // Precomputed table check (works for both B2D and FT+HB paths)
        if (have_precomputed && style.font_path.empty() && style.size <= 0.0f) {
            const unsigned char uc = static_cast<unsigned char>(c);
            if (char_widths[uc] > 0.0f) return char_widths[uc];
        }
        // Blend2D fast path for single character
        if (input.bl_measure_fn && input.bl_font_ptr && style.font_path.empty()) {
            char buf[2] = {c, '\0'};
            return input.bl_measure_fn(input.bl_font_ptr, std::string_view(buf, 1), font_size);
        }
        if (input.font_engine) {
            const FontSpec spec = FontSpec{
                .font_path = style.font_path.empty() ? input.font_spec.font_path : style.font_path,
                .font_family = style.font_family.empty() ? input.font_spec.font_family : style.font_family,
                .font_weight = style.font_weight ? style.font_weight : input.font_spec.font_weight,
                .font_style = style.font_style.empty() ? input.font_spec.font_style : style.font_style,
            };
            char buf[2] = {c, '\0'};
            return input.font_engine->measure_text(buf, spec, font_size, style.shaping);
        }
        return measure_char_legacy(input, c, font_size);
    }

    static float measure_string(const TextLayoutInput& input, const TextStyle& style, std::string_view s, float font_size) {
        // Blend2D path (preferred): uses same engine as rasterization,
        // eliminating the FreeType+HarfBuzz vs Blend2D width discrepancy.
        // Only used when the style inherits the input font (no per-run override).
        if (input.bl_measure_fn && input.bl_font_ptr && style.font_path.empty()) {
            float width = input.bl_measure_fn(input.bl_font_ptr, s, font_size);
            const size_t n_clusters = detail::grapheme_cluster_count(s);
        width += style.tracking * static_cast<float>(n_clusters > 0 ? n_clusters - 1 : 0);
            return std::max(0.0f, width);
        }
        if (input.font_engine) {
            const FontSpec spec = FontSpec{
                .font_path = style.font_path.empty() ? input.font_spec.font_path : style.font_path,
                .font_family = style.font_family.empty() ? input.font_spec.font_family : style.font_family,
                .font_weight = style.font_weight ? style.font_weight : input.font_spec.font_weight,
                .font_style = style.font_style.empty() ? input.font_spec.font_style : style.font_style,
            };
            float width = input.font_engine->measure_text(s, spec, font_size, style.shaping);
            const size_t n_clusters = detail::grapheme_cluster_count(s);
        width += style.tracking * static_cast<float>(n_clusters > 0 ? n_clusters - 1 : 0);
            return std::max(0.0f, width);
        }
        return measure_string_legacy(input, s, font_size);
    }

    static float measure_run_width(const TextLayoutInput& input, const TextLayoutRun& run, float font_size) {
        if (run.use_advance_override) {
            return std::max(0.0f, run.advance_override);
        }
        return measure_string(input, run.style, run.text, font_size);
    }

    static FontSpec resolve_font_spec(const TextLayoutInput& input, const TextStyle& style) {
        FontSpec spec;
        spec.font_path = style.font_path.empty() ? input.font_spec.font_path : style.font_path;
        spec.font_family = style.font_family.empty() ? input.font_spec.font_family : style.font_family;
        spec.font_weight = style.font_weight;
        spec.font_style = style.font_style;
        return spec;
    }

    static TextLayoutLineRun make_run(
        const TextLayoutInput& input,
        const TextLayoutRun& run,
        float x,
        float y
    ) {
        TextLayoutLineRun out;
        out.text = run.text;
        out.position = {x, y};
        out.style = run.style;
        out.is_space = run.is_space;
        out.is_decorative_star = run.is_decorative_star;
        out.star_inner_radius = run.star_inner_radius;
        out.star_outer_radius = run.star_outer_radius;
        out.star_points = run.star_points;
        out.width = measure_run_width(input, run, std::max(1.0f, run.style.size));
        return out;
    }

    static TextLayoutResult layout_single_run(const TextLayoutInput& input) {
        TextLayoutResult result;
        const float font_size = std::max(1.0f, input.style.size);
        result.font_size = font_size;
        const float line_height = std::max(1.0f, font_size * input.style.line_height);
        const float max_width = input.box.enabled && input.box.size.x > 0.0f ? input.box.size.x : 0.0f;

        std::array<float, 256> char_widths{};
        const bool use_bl2d = input.bl_measure_fn != nullptr && input.bl_font_ptr != nullptr;
        const bool have_precomputed = use_bl2d || input.font_engine != nullptr;
        if (use_bl2d) {
            for (int c = 32; c < 127; ++c) {
                char buf[2] = {static_cast<char>(c), '\0'};
                char_widths[c] = input.bl_measure_fn(
                    input.bl_font_ptr, std::string_view(buf, 1), font_size);
            }
        } else if (input.font_engine) {
            std::string ascii_batch;
            ascii_batch.reserve(95);
            for (int c = 32; c < 127; ++c) ascii_batch.push_back(static_cast<char>(c));
            auto batch_run = input.font_engine->shape_text(
                ascii_batch, input.font_spec, font_size, input.style.shaping);
            if (batch_run && batch_run->glyphs.size() == 95) {
                for (size_t i = 0; i < 95; ++i) {
                    const int c = 32 + static_cast<int>(i);
                    char_widths[c] = batch_run->glyphs[i].advance_x;
                }
            } else {
                for (int c = 32; c < 127; ++c) {
                    char buf[2] = {static_cast<char>(c), '\0'};
                    char_widths[c] = input.font_engine->measure_text(
                        buf, input.font_spec, font_size, input.style.shaping);
                }
            }
        }

        auto measure_char = [&](char c) -> float {
            if (have_precomputed) {
                const unsigned char uc = static_cast<unsigned char>(c);
                if (char_widths[uc] > 0.0f) return char_widths[uc];
            }
            if (use_bl2d) {
                char buf[2] = {c, '\0'};
                return input.bl_measure_fn(
                    input.bl_font_ptr, std::string_view(buf, 1), font_size);
            }
            if (input.font_engine) {
                char buf[2] = {c, '\0'};
                return input.font_engine->measure_text(buf, input.font_spec, font_size, input.style.shaping);
            }
            return measure_char_legacy(input, c, font_size);
        };

        auto measure_string_input = [&](std::string_view s) -> float {
            return measure_string(input, input.style, s, font_size);
        };

        std::vector<std::string> raw_lines;
        std::string current_line;
        float current_width = 0.0f;

        auto push_current_line = [&]() {
            raw_lines.push_back(std::move(current_line));
            current_line.clear();
            current_width = 0.0f;
        };

        std::vector<std::pair<std::string, bool>> tokens;
        std::string current_token;
        bool in_space = false;

        for (size_t i = 0; i < input.text.size();) {
            size_t len = detail::utf8_seq_len(static_cast<unsigned char>(input.text[i]));
            if (len == 1) {
                const char c = input.text[i];
                if (c == '\n') {
                    if (!current_token.empty()) {
                        tokens.push_back({current_token, in_space});
                        current_token.clear();
                    }
                    tokens.push_back({"\n", false});
                    i += 1;
                    continue;
                } else if (c == ' ' || c == '\t') {
                    if (!current_token.empty() && !in_space) {
                        tokens.push_back({current_token, false});
                        current_token.clear();
                    }
                    in_space = true;
                    current_token.push_back(c);
                    i += 1;
                    continue;
                }
            }
            if (!current_token.empty() && in_space) {
                tokens.push_back({current_token, true});
                current_token.clear();
            }
            in_space = false;
            current_token.append(input.text, i, len);
            i += len;
        }
        if (!current_token.empty()) {
            tokens.push_back({current_token, in_space});
        }

        std::vector<float> line_widths;
        int segments_on_line = 0;
        const float tracking_amount = input.style.tracking;

        auto push_line_with_width = [&]() {
            raw_lines.push_back(std::move(current_line));
            line_widths.push_back(current_width);
            current_line.clear();
            current_width = 0.0f;
            segments_on_line = 0;
        };

        const bool wrapping_enabled = max_width > 0.0f && input.style.wrap != TextWrap::None;

        for (const auto& token_pair : tokens) {
            const std::string& token = token_pair.first;
            const bool is_space = token_pair.second;

            if (token == "\n") {
                push_line_with_width();
                continue;
            }

            if (!wrapping_enabled) {
                current_line += token;
                current_width += measure_string_input(token);
                if (segments_on_line > 0) current_width += tracking_amount;
                ++segments_on_line;
                continue;
            }

            const float token_w = measure_string_input(token);

            if (input.style.wrap == TextWrap::Word) {
                const float inter_token_gap = (segments_on_line > 0) ? tracking_amount : 0.0f;
                if (current_width + inter_token_gap + token_w > max_width) {
                    if (!current_line.empty()) {
                        push_line_with_width();
                        if (is_space) continue;
                    }
                    if (token_w > max_width) {
                    const size_t total_clusters = detail::grapheme_cluster_count(token);
                    const float raw_token_w = (total_clusters > 1)
                        ? token_w - tracking_amount * static_cast<float>(total_clusters - 1)
                        : token_w;
                    const float avg_raw_cluster_w = total_clusters > 0
                        ? raw_token_w / static_cast<float>(total_clusters) : token_w;
                    bool had_content_on_line = false;
                    int clusters_on_line = 0;
                    for (size_t ci = 0; ci < token.size();) {
                        std::string_view suffix(token.data() + ci, token.size() - ci);
                        const size_t cluster_len = detail::grapheme_byte_offset_at(suffix, 1);
                        const float cw = avg_raw_cluster_w +
                            (clusters_on_line > 0 ? tracking_amount : 0.0f);
                        if (current_width + cw > max_width && !current_line.empty()) {
                            push_line_with_width();
                            had_content_on_line = false;
                            clusters_on_line = 0;
                        }
                        current_line.append(token.data() + ci, cluster_len);
                        current_width += cw;
                        had_content_on_line = true;
                        ++clusters_on_line;
                        ci += cluster_len;
                    }
                    if (had_content_on_line) segments_on_line = 1;
                    } else {
                        current_line = token;
                        current_width = token_w;
                        segments_on_line = 1;
                    }
                } else {
                    current_line += token;
                    current_width += token_w;
                    if (segments_on_line > 0) current_width += tracking_amount;
                    ++segments_on_line;
                }
            } else if (input.style.wrap == TextWrap::Character) {
                const size_t total_clusters = detail::grapheme_cluster_count(token);
                const float raw_token_w = (total_clusters > 1)
                    ? token_w - tracking_amount * static_cast<float>(total_clusters - 1)
                    : token_w;
                const float avg_raw_cluster_w = total_clusters > 0
                    ? raw_token_w / static_cast<float>(total_clusters) : token_w;
                int clusters_on_line = 0;
                for (size_t ci = 0; ci < token.size();) {
                    std::string_view suffix(token.data() + ci, token.size() - ci);
                    const size_t cluster_len = detail::grapheme_byte_offset_at(suffix, 1);
                    const float cw = avg_raw_cluster_w +
                        (clusters_on_line > 0 ? tracking_amount : 0.0f);
                    if (current_width + cw > max_width && !current_line.empty()) {
                        push_line_with_width();
                        clusters_on_line = 0;
                    }
                    current_line.append(token.data() + ci, cluster_len);
                    current_width += cw;
                    ++clusters_on_line;
                    ci += cluster_len;
                }
            }
        }

        if (!current_line.empty() || raw_lines.empty()) {
            raw_lines.push_back(std::move(current_line));
            line_widths.push_back(current_width);
        }

        tokens.clear();
        tokens.shrink_to_fit();

        const int max_allowed_lines = input.style.max_lines;
        const bool apply_ellipsis = input.style.ellipsis || input.style.overflow == TextOverflow::Ellipsis;
        if (max_allowed_lines > 0 && static_cast<int>(raw_lines.size()) > max_allowed_lines) {
            raw_lines.resize(max_allowed_lines);
            line_widths.resize(max_allowed_lines);
            if (apply_ellipsis && !raw_lines.empty()) {
                std::string& last_line = raw_lines.back();
                float& last_line_w = line_widths.back();
                std::vector<size_t> cluster_offsets;
                std::vector<size_t> cluster_ends;
                std::vector<float> cluster_widths;
                {
                    size_t off = 0;
                    while (off < last_line.size()) {
                        cluster_offsets.push_back(off);
                        const size_t next = detail::grapheme_byte_offset_at(
                            std::string_view(last_line.data() + off,
                                             last_line.size() - off), 1);
                        cluster_ends.push_back(off + next);
                        const float cw = (next > 0)
                            ? measure_string_input(std::string_view(
                                  last_line.data() + off, next))
                            : 0.0f;
                        cluster_widths.push_back(cw);
                        off += next;
                        if (next == 0) break;
                    }
                }
                const size_t num_clusters = cluster_offsets.size();
                std::vector<float> cluster_reverse_w(num_clusters + 1, 0.0f);
                for (size_t k = num_clusters; k > 0; --k) {
                    cluster_reverse_w[k - 1] = cluster_widths[k - 1]
                        + cluster_reverse_w[k];
                }
                const float total_w = cluster_reverse_w[0];
                const float ellipsis_w = measure_string_input("...");
                size_t trim_to_clusters = num_clusters;
                while (trim_to_clusters > 0) {
                    const float prefix_w = total_w - cluster_reverse_w[trim_to_clusters];
                    if (prefix_w + ellipsis_w <= max_width) break;
                    --trim_to_clusters;
                }
                if (trim_to_clusters == 0) {
                    last_line.clear();
                    last_line_w = 0.0f;
                } else {
                    last_line.resize(cluster_ends[trim_to_clusters - 1]);
                    last_line_w = total_w - cluster_reverse_w[trim_to_clusters];
                }
                last_line += "...";
                last_line_w = measure_string_input(last_line);
            }
        }

        float max_seen_width = 0.0f;
        for (const auto w : line_widths) {
            max_seen_width = std::max(max_seen_width, w);
        }

        result.size.x = max_seen_width;
        result.size.y = static_cast<float>(raw_lines.size()) * line_height;

        for (size_t i = 0; i < raw_lines.size(); ++i) {
            TextLayoutLine line;
            line.text = raw_lines[i];
            line.width = line_widths[i];
            line.ascent = font_size * 0.78f;
            line.descent = font_size * 0.22f;
            line.baseline = line.ascent;

            float dx = 0.0f;
            if (input.style.align == TextAlign::Center) {
                dx = (max_seen_width - line.width) * 0.5f;
            } else if (input.style.align == TextAlign::Right) {
                dx = max_seen_width - line.width;
            }
            line.position = {dx, static_cast<float>(i) * line_height};

            TextLayoutLineRun run;
            run.text = line.text;
            run.position = {line.position.x, 0.0f};
            run.width = line.width;
            run.style = input.style;
            line.runs.push_back(std::move(run));

            result.lines.push_back(std::move(line));
        }

        return result;
    }

    static TextLayoutResult layout_inline_runs(const TextLayoutInput& input) {
        TextLayoutResult result;
        const float font_size_hint = std::max(1.0f, input.style.size);
        result.font_size = font_size_hint;

        std::array<float, 256> char_widths{};
        const bool use_bl2d = input.bl_measure_fn != nullptr && input.bl_font_ptr != nullptr;
        const bool have_precomputed = use_bl2d || input.font_engine != nullptr;
        if (use_bl2d) {
            for (int c = 32; c < 127; ++c) {
                char buf[2] = {static_cast<char>(c), '\0'};
                char_widths[c] = input.bl_measure_fn(
                    input.bl_font_ptr, std::string_view(buf, 1), font_size_hint);
            }
        } else if (input.font_engine) {
            std::string ascii_batch;
            ascii_batch.reserve(95);
            for (int c = 32; c < 127; ++c) ascii_batch.push_back(static_cast<char>(c));
            auto batch_run = input.font_engine->shape_text(
                ascii_batch, input.font_spec, font_size_hint, input.style.shaping);
            if (batch_run && batch_run->glyphs.size() == 95) {
                for (size_t i = 0; i < 95; ++i) {
                    const int c = 32 + static_cast<int>(i);
                    char_widths[c] = batch_run->glyphs[i].advance_x;
                }
            } else {
                for (int c = 32; c < 127; ++c) {
                    char buf[2] = {static_cast<char>(c), '\0'};
                    char_widths[c] = input.font_engine->measure_text(
                        buf, input.font_spec, font_size_hint, input.style.shaping);
                }
            }
        }

        struct LineState {
            std::vector<TextLayoutLineRun> runs;
            float width{0.0f};
            float ascent{0.0f};
            float descent{0.0f};
        };

        std::vector<LineState> lines;
        LineState current;
        const float max_width_limit = input.box.enabled && input.box.size.x > 0.0f ? input.box.size.x : 0.0f;
        const bool wrapping_enabled = max_width_limit > 0.0f && input.style.wrap != TextWrap::None;
        const bool word_wrap = wrapping_enabled && input.style.wrap == TextWrap::Word;
        const bool char_wrap = wrapping_enabled && input.style.wrap == TextWrap::Character;

        auto push_current = [&]() {
            lines.push_back(std::move(current));
            current = LineState{};
        };

        auto append_piece = [&](const TextLayoutRun& source, std::string text_piece) {
            if (text_piece.empty() && !source.is_space && !source.is_decorative_star && !source.use_advance_override) {
                return;
            }

            TextLayoutRun piece = source;
            piece.text = std::move(text_piece);
            if (piece.is_decorative_star) {
                piece.text.clear();
            }
            if (!piece.style.font_path.empty()) {
            } else {
                piece.style.font_path = input.style.font_path;
            }
            if (piece.style.font_family.empty()) {
                piece.style.font_family = input.style.font_family;
            }
            if (piece.style.font_weight == 0) {
                piece.style.font_weight = input.style.font_weight;
            }
            if (piece.style.font_style.empty()) {
                piece.style.font_style = input.style.font_style;
            }
            if (piece.style.size <= 0.0f) {
                piece.style.size = input.style.size;
            }
            if (piece.style.line_height <= 0.0f) {
                piece.style.line_height = input.style.line_height;
            }

            const float run_size = std::max(1.0f, piece.style.size);
            const float ascent = run_size * 0.78f;
            const float descent = run_size * 0.22f;
            const float width = measure_run_width(input, piece, run_size);

            TextLayoutLineRun layout_run;
            layout_run.text = piece.text;
            layout_run.style = piece.style;
            layout_run.width = width;
            layout_run.is_space = piece.is_space;
            layout_run.is_decorative_star = piece.is_decorative_star;
            layout_run.star_inner_radius = piece.star_inner_radius;
            layout_run.star_outer_radius = piece.star_outer_radius;
            layout_run.star_points = piece.star_points;
            layout_run.position = {current.width, 0.0f};

            current.width += width;
            current.ascent = std::max(current.ascent, ascent);
            current.descent = std::max(current.descent, descent);
            current.runs.push_back(std::move(layout_run));
        };

        auto append_split_text = [&](const TextLayoutRun& run, std::string_view text) {
            if (text.empty()) {
                if (run.is_space || run.is_decorative_star || run.use_advance_override) {
                    append_piece(run, "");
                }
                return;
            }

            const float run_size = std::max(1.0f, run.style.size <= 0.0f ? input.style.size : run.style.size);

            if (!wrapping_enabled) {
                append_piece(run, std::string(text));
                return;
            }

            if (char_wrap) {
                const float full_width = measure_string(input, run.style, text, run_size);
                const size_t total_clusters = detail::grapheme_cluster_count(text);
                const float avg_cluster_w = total_clusters > 0
                    ? full_width / static_cast<float>(total_clusters) : run_size * 0.6f;
                for (size_t ci = 0; ci < text.size();) {
                    std::string_view suffix(text.data() + ci, text.size() - ci);
                    const size_t cluster_len = detail::grapheme_byte_offset_at(suffix, 1);
                    if (cluster_len == 1) {
                        if (text[ci] == '\r') { ci += 1; continue; }
                        if (text[ci] == '\n') {
                            if (!current.runs.empty()) {
                                push_current();
                            }
                            ci += 1;
                            continue;
                        }
                    }

                    const float cw = avg_cluster_w;
                    if (current.width + cw > max_width_limit && !current.runs.empty()) {
                        push_current();
                    }
                    if (cluster_len == 1 && (text[ci] == ' ' || text[ci] == '\t')) {
                        if (current.runs.empty()) {
                            ci += 1;
                            continue;
                        }
                    }
                    std::string glyph(text, ci, cluster_len);
                    append_piece(run, glyph);
                    ci += cluster_len;
                }
                return;
            }

            // Word wrap
            std::string token;
            bool token_is_space = false;

            auto flush_token = [&]() {
                if (token.empty()) {
                    return;
                }

                const float token_w = measure_string(input, run.style, token, run_size);

                if (token_is_space) {
                    if (current.runs.empty()) {
                        token.clear();
                        return;
                    }
                    if (current.width + token_w > max_width_limit && !current.runs.empty()) {
                        push_current();
                        token.clear();
                        return;
                    }
                    append_piece(run, token);
                    token.clear();
                    return;
                }

                if (current.width > 0.0f && current.width + token_w > max_width_limit) {
                    push_current();
                }

                if (token_w > max_width_limit && current.runs.empty()) {
                    const size_t total_clusters = detail::grapheme_cluster_count(token);
                    const float avg_cluster_w = total_clusters > 0
                        ? token_w / static_cast<float>(total_clusters) : token_w;
                    for (size_t ci = 0; ci < token.size();) {
                        std::string_view suffix(token.data() + ci, token.size() - ci);
                        const size_t cluster_len = detail::grapheme_byte_offset_at(suffix, 1);
                        const float cw = avg_cluster_w;
                        if (current.width + cw > max_width_limit && !current.runs.empty()) {
                            push_current();
                        }
                        const std::string glyph(token, ci, cluster_len);
                        append_piece(run, glyph);
                        ci += cluster_len;
                    }
                } else {
                    append_piece(run, token);
                }

                token.clear();
            };

            for (size_t ci = 0; ci < text.size();) {
                size_t clen = detail::utf8_seq_len(static_cast<unsigned char>(text[ci]));
                if (clen == 1 && text[ci] == '\r') {
                    ci += 1;
                    continue;
                }
                if (clen == 1 && text[ci] == '\n') {
                    flush_token();
                    if (!current.runs.empty()) {
                        push_current();
                    }
                    ci += 1;
                    continue;
                }

                const bool is_space = (clen == 1 && (text[ci] == ' ' || text[ci] == '\t'));
                if (token.empty()) {
                    token_is_space = is_space;
                    if (clen == 1) token.push_back(text[ci]);
                    else token.append(text, ci, clen);
                    ci += clen;
                    continue;
                }
                if (token_is_space != is_space) {
                    flush_token();
                    token_is_space = is_space;
                }
                if (clen == 1) token.push_back(text[ci]);
                else token.append(text, ci, clen);
                ci += clen;
            }
            flush_token();
        };

        auto split_and_add = [&](const TextLayoutRun& run) {
            if (run.is_line_break) {
                if (!current.runs.empty()) {
                    push_current();
                }
                return;
            }

            const std::string& text = run.text;
            size_t start = 0;
            for (size_t i = 0; i <= text.size(); ++i) {
                if (i == text.size() || text[i] == '\n') {
                    append_split_text(run, text.substr(start, i - start));
                    if (i < text.size() && text[i] == '\n') {
                        if (!current.runs.empty()) {
                            push_current();
                        }
                    }
                    start = i + 1;
                }
            }
        };

        if (input.runs.empty()) {
            TextLayoutRun single;
            single.text = input.text;
            single.style = input.style;
            split_and_add(single);
        } else {
            for (const auto& run : input.runs) {
                split_and_add(run);
            }
        }

        if (!current.runs.empty() || lines.empty()) {
            lines.push_back(std::move(current));
        }

        const float line_height = std::max(1.0f, input.style.size * input.style.line_height);
        float max_width = 0.0f;
        for (const auto& line : lines) {
            max_width = std::max(max_width, line.width);
        }

        const bool apply_ellipsis = input.style.ellipsis || input.style.overflow == TextOverflow::Ellipsis;

        if (apply_ellipsis) {
            for (auto& line : lines) {
                if (line.width <= max_width_limit || line.runs.empty()) continue;
                if (line.runs.size() == 1) {
                    auto& run = line.runs.front();
                    const float run_size = std::max(1.0f, run.style.size <= 0.0f ? font_size_hint : run.style.size);
                    std::vector<size_t> cluster_offsets;
                    std::vector<size_t> cluster_ends;
                    std::vector<float> cluster_widths;
                    {
                        size_t off = 0;
                        while (off < run.text.size()) {
                            cluster_offsets.push_back(off);
                            const size_t next = detail::grapheme_byte_offset_at(
                                std::string_view(run.text.data() + off,
                                                 run.text.size() - off), 1);
                            cluster_ends.push_back(off + next);
                            const float cw = (next > 0)
                                ? measure_string(input, run.style,
                                    std::string_view(run.text.data() + off, next), run_size)
                                : 0.0f;
                            cluster_widths.push_back(cw);
                            off += next;
                            if (next == 0) break;
                        }
                    }
                    const size_t num_clusters = cluster_offsets.size();
                    std::vector<float> cluster_reverse_w(num_clusters + 1, 0.0f);
                    for (size_t k = num_clusters; k > 0; --k) {
                        cluster_reverse_w[k - 1] = cluster_widths[k - 1]
                            + cluster_reverse_w[k];
                    }
                    const float total_w = cluster_reverse_w[0];
                    const float ellip_w = measure_string(input, run.style, "...", run_size);
                    size_t trim_to_clusters = num_clusters;
                    while (trim_to_clusters > 0) {
                        const float prefix_w = total_w - cluster_reverse_w[trim_to_clusters];
                        if (prefix_w + ellip_w <= max_width_limit) break;
                        --trim_to_clusters;
                    }
                    const float new_width = (trim_to_clusters > 0)
                        ? total_w - cluster_reverse_w[trim_to_clusters] : 0.0f;
                    line.width -= (run.width - new_width);
                    run.width = new_width;
                    if (trim_to_clusters == 0) {
                        run.text.clear();
                    } else {
                        run.text.resize(cluster_ends[trim_to_clusters - 1]);
                    }
                    run.text += "...";
                    run.width = measure_string(input, run.style, run.text, run_size);
                    line.width = run.width;
                }
            }
        }

        result.size.x = max_width;
        result.size.y = static_cast<float>(lines.size()) * line_height;

        for (size_t i = 0; i < lines.size(); ++i) {
            auto& line_state = lines[i];
            TextLayoutLine line;
            line.width = line_state.width;
            line.ascent = std::max(line_state.ascent, font_size_hint * 0.78f);
            line.descent = std::max(line_state.descent, font_size_hint * 0.22f);
            line.baseline = line.ascent;
            line.position.y = static_cast<float>(i) * line_height;
            line.text.clear();
            for (const auto& run : line_state.runs) {
                line.text += run.text;
            }

            float dx = 0.0f;
            if (input.style.align == TextAlign::Center) {
                dx = (max_width - line.width) * 0.5f;
            } else if (input.style.align == TextAlign::Right) {
                dx = max_width - line.width;
            }
            line.position.x = dx;

            float cursor_x = dx;
            for (auto run : line_state.runs) {
                run.position.x = cursor_x;
                run.position.y = line.position.y;
                cursor_x += run.width;
                line.runs.push_back(std::move(run));
            }

            result.lines.push_back(std::move(line));
        }

        return result;
    }

public:
    [[nodiscard]] static TextLayoutResult layout(const TextLayoutInput& input) {
        TextLayoutInput current_input = input;
        if (current_input.runs.empty() &&
            current_input.style.shaping.direction == TextDirection::Auto) {
            auto bidi_runs = segment_bidi_runs(current_input.text);
            if (!bidi_runs.empty()) {
                bool has_rtl = false;
                for (const auto& br : bidi_runs) {
                    if (br.direction == TextDirection::RTL) {
                        has_rtl = true;
                        break;
                    }
                }
                if (has_rtl) {
                    for (const auto& br : bidi_runs) {
                        TextLayoutRun r;
                        r.text = br.text;
                        r.style = current_input.style;
                        r.style.shaping.direction = br.direction;
                        current_input.runs.push_back(std::move(r));
                    }
                }
            }
        }
        const bool auto_fit = input.style.auto_fit || input.style.auto_scale;

        const auto measure_with_size = [&](float size) {
            TextLayoutInput test_input = current_input;
            test_input.style.size = size;
            return test_input.runs.empty() ? layout_single_run(test_input) : layout_inline_runs(test_input);
        };

        if (auto_fit && input.box.enabled && input.box.size.x > 0.0f && input.box.size.y > 0.0f) {
            float low = std::max(1.0f, input.style.min_size);
            float high = std::max(low, input.style.size);
            if (input.style.max_size > 0.0f) {
                high = std::min(high, input.style.max_size);
            }
            float best_size = low;

            for (int step = 0; step < 8; ++step) {
                const float mid = (low + high) * 0.5f;
                TextLayoutResult temp = measure_with_size(mid);
                if (temp.size.x <= input.box.size.x && temp.size.y <= input.box.size.y) {
                    best_size = mid;
                    low = mid + 0.1f;
                } else {
                    high = mid - 0.1f;
                }
                if (high < low) break;
            }

            current_input.style.size = best_size;
        }

        return current_input.runs.empty()
            ? layout_single_run(current_input)
            : layout_inline_runs(current_input);
    }
};

} // namespace chronon3d
