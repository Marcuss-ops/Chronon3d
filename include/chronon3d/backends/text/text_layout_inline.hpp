#pragma once
// ─── text_layout_inline.hpp ───────────────────────────────────────────────
//
// FASE 5 Step 2 — extracted from text_layout_engine.hpp.
// Inline-runs layout path: layout_inline_runs() with bidi-run support,
// per-run styling, word/char wrapping, and ellipsis truncation.
// Free function in chronon3d::detail::text_layout.
//
// Depends on text_layout_single.hpp for measure_string, measure_run_width,
// and the detail::utf8_* / detail::grapheme_* utilities.
// ─────────────────────────────────────────────────────────────────────────

#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/text/font_engine.hpp>

#include "text_unicode_utils.hpp"
#include "text_layout_types.hpp"
#include "text_layout_single.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d {
namespace detail {
namespace text_layout {

// ═══════════════════════════════════════════════════════════════════════════
// layout_inline_runs — per-run styling with word/char wrapping
// ═══════════════════════════════════════════════════════════════════════════

inline TextLayoutResult layout_inline_runs(const TextLayoutInput& input) {
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

} // namespace text_layout
} // namespace detail
} // namespace chronon3d
