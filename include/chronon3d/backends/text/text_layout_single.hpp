#pragma once
// ─── text_layout_single.hpp ───────────────────────────────────────────────
//
// FASE 5 Step 1 — extracted from text_layout_engine.hpp.
// Single-run layout path: measurement helpers + layout_single_run().
// These are free functions in chronon3d::detail::text_layout, callable
// from TextLayoutEngine::layout() and text_layout_inline.hpp.
//
// All functions are inline (header-only) to preserve the original
// compilation model of text_layout_engine.hpp.
// ─────────────────────────────────────────────────────────────────────────

#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/text/font_engine.hpp>

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
namespace detail {
namespace text_layout {

// ═══════════════════════════════════════════════════════════════════════════
// Measurement helpers
// ═══════════════════════════════════════════════════════════════════════════

inline float measure_char_legacy(const TextLayoutInput& input, char c, float font_size) {
    if (input.char_width_fn) {
        return std::max(0.0f, input.char_width_fn(input.char_width_ctx, c, font_size));
    }
    return font_size * 0.6f;
}

inline float measure_string_legacy(const TextLayoutInput& input, std::string_view s, float font_size) {
    float width = 0.0f;
    const size_t cp_count = detail::utf8_length(s);
    for (char c : s) {
        width += measure_char_legacy(input, c, font_size);
    }
    const size_t n_clusters2 = detail::grapheme_cluster_count(s);
    width += input.style.tracking * static_cast<float>(n_clusters2 > 0 ? n_clusters2 - 1 : 0);
    (void)cp_count;
    return std::max(0.0f, width);
}

// Fast single-character width lookup using precomputed ASCII table.
// Prefers Blend2D when available (same engine as rasterization).
// Falls back to font_engine measurement for non-ASCII or font mismatch.
inline float measure_single_char(char c, bool have_precomputed,
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

inline float measure_string(const TextLayoutInput& input, const TextStyle& style, std::string_view s, float font_size) {
    // Blend2D path (preferred): uses same engine as rasterization,
    // eliminating the FreeType+HarfBuzz vs Blend2D width discrepancy.
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

inline float measure_run_width(const TextLayoutInput& input, const TextLayoutRun& run, float font_size) {
    if (run.use_advance_override) {
        return std::max(0.0f, run.advance_override);
    }
    return measure_string(input, run.style, run.text, font_size);
}

inline FontSpec resolve_font_spec(const TextLayoutInput& input, const TextStyle& style) {
    FontSpec spec;
    spec.font_path = style.font_path.empty() ? input.font_spec.font_path : style.font_path;
    spec.font_family = style.font_family.empty() ? input.font_spec.font_family : style.font_family;
    spec.font_weight = style.font_weight;
    spec.font_style = style.font_style;
    return spec;
}

inline TextLayoutLineRun make_run(
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

// ═══════════════════════════════════════════════════════════════════════════
// layout_single_run — single-style layout with word/char wrapping
// ═══════════════════════════════════════════════════════════════════════════

inline TextLayoutResult layout_single_run(const TextLayoutInput& input) {
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

} // namespace text_layout
} // namespace detail
} // namespace chronon3d
