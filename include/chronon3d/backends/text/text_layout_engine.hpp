#pragma once

#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/text/font_engine.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d {

// ── UTF-8 code-point helpers ───────────────────────────────────────
// Used to make wrapping / ellipsis / splitting grapheme-aware instead
// of byte-aware, preventing broken UTF-8 sequences for non-Latin text.

namespace detail {

/// Return the byte length of the UTF-8 sequence starting at the given byte.
/// Returns 1 for ASCII, 2–4 for multi-byte sequences, 1 for invalid.
inline size_t utf8_seq_len(unsigned char lead) noexcept {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1; // invalid continuation byte — skip one
}

/// Decode a single UTF-8 code point at the given position.
/// Advances `pos` by the sequence length.  Returns U+FFFD on error.
inline char32_t utf8_decode(const std::string& s, size_t& pos) {
    if (pos >= s.size()) return 0;
    const unsigned char c0 = static_cast<unsigned char>(s[pos]);
    const size_t len = utf8_seq_len(c0);
    if (pos + len > s.size()) { ++pos; return 0xFFFD; }
    char32_t cp;
    switch (len) {
        case 1: cp = c0; break;
        case 2: cp = ((c0 & 0x1F) << 6) | (static_cast<unsigned char>(s[pos+1]) & 0x3F); break;
        case 3: cp = ((c0 & 0x0F) << 12) | ((static_cast<unsigned char>(s[pos+1]) & 0x3F) << 6) | (static_cast<unsigned char>(s[pos+2]) & 0x3F); break;
        case 4: cp = ((c0 & 0x07) << 18) | ((static_cast<unsigned char>(s[pos+1]) & 0x3F) << 12) | ((static_cast<unsigned char>(s[pos+2]) & 0x3F) << 6) | (static_cast<unsigned char>(s[pos+3]) & 0x3F); break;
        default: cp = 0xFFFD; break;
    }
    pos += len;
    return cp;
}

/// Remove the last code point from a UTF-8 string (in-place).
/// Safe for multi-byte sequences — scans backwards from end.
inline void utf8_pop_back(std::string& s) {
    if (s.empty()) return;
    size_t pos = s.size() - 1;
    // Skip continuation bytes (10xxxxxx)
    while (pos > 0 && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80)
        --pos;
    s.resize(pos);
}

/// Return the number of code points in a UTF-8 string (O(n)).
inline size_t utf8_length(const std::string& s) {
    size_t count = 0;
    for (size_t i = 0; i < s.size();) {
        i += utf8_seq_len(static_cast<unsigned char>(s[i]));
        ++count;
    }
    return count;
}

/// Overload for std::string_view — avoids copying into std::string.
inline size_t utf8_length(std::string_view sv) {
    size_t count = 0;
    for (size_t i = 0; i < sv.size();) {
        i += utf8_seq_len(static_cast<unsigned char>(sv[i]));
        ++count;
    }
    return count;
}

} // namespace detail

struct TextLayoutRun {
    std::string text;
    TextStyle style{};
    bool is_line_break{false};
    bool is_space{false};
    bool is_decorative_star{false};
    bool use_advance_override{false};
    float advance_override{0.0f};
    float star_inner_radius{0.0f};
    float star_outer_radius{0.0f};
    int star_points{8};
};

struct TextLayoutLineRun {
    std::string text;
    Vec2 position{0.0f, 0.0f};
    float width{0.0f};
    TextStyle style{};
    bool is_space{false};
    bool is_decorative_star{false};
    float star_inner_radius{0.0f};
    float star_outer_radius{0.0f};
    int star_points{8};
};

struct TextLayoutInput {
    std::string text;
    std::vector<TextLayoutRun> runs;
    TextStyle style{};
    TextBox box{};
    // Legacy per-char callback (used when font_engine is null)
    const void* char_width_ctx{nullptr};
    float (*char_width_fn)(const void* ctx, char c, float font_size){nullptr};
    // FreeType/HarfBuzz-based measurement (used by TextAnimator for per-glyph bbox)
    const FontEngine* font_engine{nullptr};
    FontSpec font_spec{};
    // Blend2D-based measurement (preferred for layout; eliminates FT+HB vs B2D
    // discrepancy since the same engine measures and renders).  bl_font_ptr
    // points to a BLFontFace; bl_measure_fn creates BLFont at requested size.
    const void* bl_font_ptr{nullptr};
    float (*bl_measure_fn)(const void* font_face, std::string_view text, float font_size){nullptr};
};

struct TextLayoutLine {
    std::string text;
    Vec2 position{0.0f, 0.0f};
    float width{0.0f};
    float ascent{0.0f};
    float descent{0.0f};
    float baseline{0.0f};
    std::vector<TextLayoutLineRun> runs;
};

struct TextLayoutResult {
    Vec2 size{0.0f, 0.0f};
    std::vector<TextLayoutLine> lines;
    float font_size{0.0f};
};

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
        width += input.style.tracking * static_cast<float>(cp_count);
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
            return input.font_engine->measure_text(buf, spec, font_size);
        }
        return measure_char_legacy(input, c, font_size);
    }

    static float measure_string(const TextLayoutInput& input, const TextStyle& style, std::string_view s, float font_size) {
        // Blend2D path (preferred): uses same engine as rasterization,
        // eliminating the FreeType+HarfBuzz vs Blend2D width discrepancy.
        // Only used when the style inherits the input font (no per-run override).
        if (input.bl_measure_fn && input.bl_font_ptr && style.font_path.empty()) {
            float width = input.bl_measure_fn(input.bl_font_ptr, s, font_size);
            width += style.tracking * static_cast<float>(detail::utf8_length(s));
            return std::max(0.0f, width);
        }
        if (input.font_engine) {
            const FontSpec spec = FontSpec{
                .font_path = style.font_path.empty() ? input.font_spec.font_path : style.font_path,
                .font_family = style.font_family.empty() ? input.font_spec.font_family : style.font_family,
                .font_weight = style.font_weight ? style.font_weight : input.font_spec.font_weight,
                .font_style = style.font_style.empty() ? input.font_spec.font_style : style.font_style,
            };
            float width = input.font_engine->measure_text(s, spec, font_size);
            width += style.tracking * static_cast<float>(detail::utf8_length(s));
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

        // Precompute ASCII character widths at this font size for O(1)
        // lookup during wrapping.  Uses Blend2D when available (same engine
        // as rasterization), falling back to FT+HarfBuzz otherwise.
        std::array<float, 256> char_widths{};
        const bool use_bl2d = input.bl_measure_fn != nullptr && input.bl_font_ptr != nullptr;
        const bool have_precomputed = use_bl2d || input.font_engine != nullptr;
        if (have_precomputed) {
            for (int c = 32; c < 127; ++c) {
                char buf[2] = {static_cast<char>(c), '\0'};
                if (use_bl2d) {
                    char_widths[c] = input.bl_measure_fn(
                        input.bl_font_ptr, std::string_view(buf, 1), font_size);
                } else {
                    char_widths[c] = input.font_engine->measure_text(
                        buf, input.font_spec, font_size);
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
                return input.font_engine->measure_text(buf, input.font_spec, font_size);
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

        // UTF-8 aware tokenization: iterate by code-point, not by byte
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
            // Non-space, non-newline (multi-byte or regular char)
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

        const bool wrapping_enabled = max_width > 0.0f && input.style.wrap != TextWrap::None;

        for (const auto& token_pair : tokens) {
            const std::string& token = token_pair.first;
            const bool is_space = token_pair.second;

            if (token == "\n") {
                push_current_line();
                continue;
            }

            if (!wrapping_enabled) {
                current_line += token;
                current_width += measure_string_input(token);
                continue;
            }

            const float token_w = measure_string_input(token);

            if (input.style.wrap == TextWrap::Word) {
                if (current_width + token_w > max_width) {
                    if (!current_line.empty()) {
                        push_current_line();
                        if (is_space) continue;
                    }
                    if (token_w > max_width) {
                    // UTF-8 aware character-level fallback for overlong tokens
                    for (size_t ci = 0; ci < token.size();) {
                        size_t clen = detail::utf8_seq_len(static_cast<unsigned char>(token[ci]));
                        // Use per-char measurement for ASCII; average for multi-byte
                        const float cw = (clen == 1)
                            ? measure_char(token[ci]) + input.style.tracking
                            : (token_w / static_cast<float>(detail::utf8_length(token))) + input.style.tracking;
                        if (current_width + cw > max_width && !current_line.empty()) {
                            push_current_line();
                        }
                        current_line.append(token.data() + ci, clen);
                        current_width += cw;
                        ci += clen;
                    }
                    } else {
                        current_line = token;
                        current_width = token_w;
                    }
                } else {
                    current_line += token;
                    current_width += token_w;
                }
            } else if (input.style.wrap == TextWrap::Character) {
                // UTF-8 aware character wrap
                for (size_t ci = 0; ci < token.size();) {
                    size_t clen = detail::utf8_seq_len(static_cast<unsigned char>(token[ci]));
                    // Use per-char measurement for ASCII; average for multi-byte
                    const float cw = (clen == 1)
                        ? measure_char(token[ci]) + input.style.tracking
                        : (token_w / static_cast<float>(detail::utf8_length(token))) + input.style.tracking;
                    if (current_width + cw > max_width && !current_line.empty()) {
                        push_current_line();
                    }
                    current_line.append(token.data() + ci, clen);
                    current_width += cw;
                    ci += clen;
                }
            }
        }

        if (!current_line.empty() || raw_lines.empty()) {
            raw_lines.push_back(std::move(current_line));
        }

        const int max_allowed_lines = input.style.max_lines;
        const bool apply_ellipsis = input.style.ellipsis || input.style.overflow == TextOverflow::Ellipsis;

        if (max_allowed_lines > 0 && static_cast<int>(raw_lines.size()) > max_allowed_lines) {
            raw_lines.resize(max_allowed_lines);
            if (apply_ellipsis && !raw_lines.empty()) {
                std::string& last_line = raw_lines.back();
                const float ellipsis_w = measure_string_input("...");
                while (!last_line.empty() && measure_string_input(last_line) + ellipsis_w > max_width) {
                    detail::utf8_pop_back(last_line);
                }
                last_line += "...";
            }
        }

        float max_seen_width = 0.0f;
        std::vector<float> line_widths;
        for (const auto& line_str : raw_lines) {
            const float w = measure_string_input(line_str);
            line_widths.push_back(w);
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

        // Precompute ASCII character widths (same approach as layout_single_run).
        // Uses Blend2D when available, falling back to FT+HarfBuzz.
        std::array<float, 256> char_widths{};
        const bool use_bl2d = input.bl_measure_fn != nullptr && input.bl_font_ptr != nullptr;
        const bool have_precomputed = use_bl2d || input.font_engine != nullptr;
        if (have_precomputed) {
            for (int c = 32; c < 127; ++c) {
                char buf[2] = {static_cast<char>(c), '\0'};
                if (use_bl2d) {
                    char_widths[c] = input.bl_measure_fn(
                        input.bl_font_ptr, std::string_view(buf, 1), font_size_hint);
                } else {
                    char_widths[c] = input.font_engine->measure_text(
                        buf, input.font_spec, font_size_hint);
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
                // keep explicit style
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
                // UTF-8 aware character wrap: iterate by code-point, not by byte
                for (size_t ci = 0; ci < text.size();) {
                    size_t clen = detail::utf8_seq_len(static_cast<unsigned char>(text[ci]));
                    if (clen == 1 && text[ci] == '\r') { ci += 1; continue; }
                    if (clen == 1 && text[ci] == '\n') {
                        if (!current.runs.empty()) {
                            push_current();
                        }
                        ci += 1;
                        continue;
                    }

                    const float cw = (clen == 1)
                        ? measure_single_char(text[ci], have_precomputed, char_widths,
                            input, run.style, run_size)
                        : run_size * 0.6f + input.style.tracking;
                    if (current.width + cw > max_width_limit && !current.runs.empty()) {
                        push_current();
                    }
                    if (clen == 1 && (text[ci] == ' ' || text[ci] == '\t')) {
                        if (current.runs.empty()) {
                            ci += 1;
                            continue;
                        }
                    }
                    std::string glyph(text, ci, clen);
                    append_piece(run, glyph);
                    ci += clen;
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
                    // UTF-8 aware character-level fallback for overlong tokens
                    const float avg_cw = token_w / static_cast<float>(detail::utf8_length(token));
                    for (size_t ci = 0; ci < token.size();) {
                        size_t clen = detail::utf8_seq_len(static_cast<unsigned char>(token[ci]));
                        const float cw = (clen == 1)
                            ? measure_single_char(token[ci], have_precomputed, char_widths,
                                input, run.style, run_size)
                            : avg_cw + input.style.tracking;
                        if (current.width + cw > max_width_limit && !current.runs.empty()) {
                            push_current();
                        }
                        const std::string glyph(token, ci, clen);
                        append_piece(run, glyph);
                        ci += clen;
                    }
                } else {
                    append_piece(run, token);
                }

                token.clear();
            };

            // UTF-8 aware word-wrap tokenization
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
                    while (!run.text.empty() && line.width > max_width_limit) {
                        detail::utf8_pop_back(run.text);
                        run.width = measure_string(input, run.style, run.text, std::max(1.0f, run.style.size));
                        line.width = run.width;
                    }
                    run.text += "...";
                    run.width = measure_string(input, run.style, run.text, std::max(1.0f, run.style.size));
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
