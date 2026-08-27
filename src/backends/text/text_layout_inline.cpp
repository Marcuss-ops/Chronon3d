#include <chronon3d/backends/text/text_layout_inline.hpp>
#include <chronon3d/backends/text/text_layout_single.hpp>
#include <chronon3d/backends/text/text_unicode_utils.hpp>
#include <chronon3d/text/font_engine.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d::detail::text_layout {
namespace {

struct InlineLineState {
    std::vector<TextLayoutLineRun> runs;
    float width{0.0f};
    float ascent{0.0f};
    float descent{0.0f};
};

struct InlineLayoutContext {
    TextLayoutInput input;
    float font_size_hint{1.0f};
    float max_width_limit{0.0f};
    bool wrapping_enabled{false};
    bool word_wrap{false};
    bool char_wrap{false};
    std::array<float, 256> char_widths{};
    std::vector<InlineLineState> lines;
};

InlineLayoutContext normalize_input(const TextLayoutInput& input) {
    InlineLayoutContext ctx;
    ctx.input = input;
    ctx.font_size_hint = std::max(1.0f, input.style.size);
    ctx.max_width_limit = input.box.enabled && input.box.size.x > 0.0f ? input.box.size.x : 0.0f;
    ctx.wrapping_enabled = ctx.max_width_limit > 0.0f && input.style.wrap != TextWrap::None;
    ctx.word_wrap = ctx.wrapping_enabled && input.style.wrap == TextWrap::Word;
    ctx.char_wrap = ctx.wrapping_enabled && input.style.wrap == TextWrap::Character;
    return ctx;
}

void shape_measurement_alphabet(InlineLayoutContext& ctx) {
    const auto& input = ctx.input;
    if (input.bl_measure_fn && input.bl_font_ptr) {
        for (int c = 32; c < 127; ++c) {
            char buf[2] = {static_cast<char>(c), '\0'};
            ctx.char_widths[c] = input.bl_measure_fn(input.bl_font_ptr, std::string_view(buf, 1), ctx.font_size_hint);
        }
        return;
    }
    if (!input.font_engine) return;
    std::string ascii;
    ascii.reserve(95);
    for (int c = 32; c < 127; ++c) ascii.push_back(static_cast<char>(c));
    auto shaped = input.font_engine->shape_text(ascii, input.font_spec, ctx.font_size_hint, input.style.shaping);
    if (shaped && shaped->glyphs.size() == 95) {
        for (size_t i = 0; i < 95; ++i) ctx.char_widths[32 + static_cast<int>(i)] = shaped->glyphs[i].advance_x;
        return;
    }
    for (int c = 32; c < 127; ++c) {
        char buf[2] = {static_cast<char>(c), '\0'};
        ctx.char_widths[c] = input.font_engine->measure_text(buf, input.font_spec, ctx.font_size_hint, input.style.shaping);
    }
}

void push_line(InlineLayoutContext& ctx, InlineLineState& current) {
    ctx.lines.push_back(std::move(current));
    current = InlineLineState{};
}

void append_piece(InlineLayoutContext& ctx, InlineLineState& current, const TextLayoutRun& source, std::string text) {
    if (text.empty() && !source.is_space && !source.is_decorative_star && !source.use_advance_override) return;
    TextLayoutRun piece = source;
    piece.text = std::move(text);
    if (piece.is_decorative_star) piece.text.clear();
    if (piece.style.font_path.empty()) piece.style.font_path = ctx.input.style.font_path;
    if (piece.style.font_family.empty()) piece.style.font_family = ctx.input.style.font_family;
    if (piece.style.font_weight == 0) piece.style.font_weight = ctx.input.style.font_weight;
    if (piece.style.font_style.empty()) piece.style.font_style = ctx.input.style.font_style;
    if (piece.style.size <= 0.0f) piece.style.size = ctx.input.style.size;
    if (piece.style.line_height <= 0.0f) piece.style.line_height = ctx.input.style.line_height;

    const float size = std::max(1.0f, piece.style.size);
    float ascent = size * 0.78f;
    float descent = size * 0.22f;
    if (ctx.input.font_engine) {
        const auto fm = ctx.input.font_engine->get_font_metrics(resolve_font_spec(ctx.input, piece.style), size);
        if (fm.ascent > 0.0f) ascent = fm.ascent;
        if (fm.descent > 0.0f) descent = fm.descent;
    }
    TextLayoutLineRun run;
    run.text = piece.text;
    run.style = piece.style;
    run.width = measure_run_width(ctx.input, piece, size);
    run.is_space = piece.is_space;
    run.is_decorative_star = piece.is_decorative_star;
    run.star_inner_radius = piece.star_inner_radius;
    run.star_outer_radius = piece.star_outer_radius;
    run.star_points = piece.star_points;
    run.position = {current.width, 0.0f};
    current.width += run.width;
    current.ascent = std::max(current.ascent, ascent);
    current.descent = std::max(current.descent, descent);
    current.runs.push_back(std::move(run));
}

void split_character_wrapped(InlineLayoutContext& ctx, InlineLineState& current, const TextLayoutRun& run, std::string_view text) {
    const size_t total = grapheme_cluster_count(text);
    const float full_width = measure_string(ctx.input, run.style, text, std::max(1.0f, run.style.size));
    const float average = total ? full_width / static_cast<float>(total) : std::max(1.0f, run.style.size) * 0.6f;
    for (size_t offset = 0; offset < text.size();) {
        std::string_view suffix(text.data() + offset, text.size() - offset);
        const size_t length = grapheme_byte_offset_at(suffix, 1);
        if (length == 1 && text[offset] == '\n') { if (!current.runs.empty()) push_line(ctx, current); ++offset; continue; }
        if (current.width + average > ctx.max_width_limit && !current.runs.empty()) push_line(ctx, current);
        if (length == 1 && (text[offset] == ' ' || text[offset] == '\t') && current.runs.empty()) { ++offset; continue; }
        append_piece(ctx, current, run, std::string(text, offset, length));
        offset += length;
    }
}

void split_word_wrapped(InlineLayoutContext& ctx, InlineLineState& current, const TextLayoutRun& run, std::string_view text) {
    std::string token;
    bool token_space = false;
    auto flush = [&]() {
        if (token.empty()) return;
        const float width = measure_string(ctx.input, run.style, token, std::max(1.0f, run.style.size));
        if (token_space) {
            if (current.runs.empty()) { token.clear(); return; }
            if (current.width + width > ctx.max_width_limit) { push_line(ctx, current); token.clear(); return; }
            append_piece(ctx, current, run, token); token.clear(); return;
        }
        if (current.width > 0.0f && current.width + width > ctx.max_width_limit) push_line(ctx, current);
        if (width > ctx.max_width_limit && current.runs.empty()) split_character_wrapped(ctx, current, run, token);
        else append_piece(ctx, current, run, token);
        token.clear();
    };
    for (size_t offset = 0; offset < text.size();) {
        const size_t length = utf8_seq_len(static_cast<unsigned char>(text[offset]));
        if (length == 1 && text[offset] == '\r') { ++offset; continue; }
        if (length == 1 && text[offset] == '\n') { flush(); if (!current.runs.empty()) push_line(ctx, current); ++offset; continue; }
        const bool space = length == 1 && (text[offset] == ' ' || text[offset] == '\t');
        if (token.empty()) token_space = space;
        if (!token.empty() && token_space != space) { flush(); token_space = space; }
        token.append(text, offset, length);
        offset += length;
    }
    flush();
}

void append_text(InlineLayoutContext& ctx, InlineLineState& current, const TextLayoutRun& run, std::string_view text) {
    if (text.empty()) { if (run.is_space || run.is_decorative_star || run.use_advance_override) append_piece(ctx, current, run, ""); return; }
    if (!ctx.wrapping_enabled) { append_piece(ctx, current, run, std::string(text)); return; }
    if (ctx.char_wrap) split_character_wrapped(ctx, current, run, text);
    else split_word_wrapped(ctx, current, run, text);
}

void discover_lines(InlineLayoutContext& ctx) {
    InlineLineState current;
    auto add_run = [&](const TextLayoutRun& run) {
        if (run.is_line_break) { if (!current.runs.empty()) push_line(ctx, current); return; }
        size_t start = 0;
        for (size_t i = 0; i <= run.text.size(); ++i) {
            if (i == run.text.size() || run.text[i] == '\n') {
                append_text(ctx, current, run, std::string_view(run.text).substr(start, i - start));
                if (i < run.text.size() && !current.runs.empty()) push_line(ctx, current);
                start = i + 1;
            }
        }
    };
    if (ctx.input.runs.empty()) { TextLayoutRun run; run.text = ctx.input.text; run.style = ctx.input.style; add_run(run); }
    else for (const auto& run : ctx.input.runs) add_run(run);
    if (!current.runs.empty() || ctx.lines.empty()) ctx.lines.push_back(std::move(current));
}

void apply_ellipsis(InlineLayoutContext& ctx) {
    if (!(ctx.input.style.ellipsis || ctx.input.style.overflow == TextOverflow::Ellipsis)) return;
    for (auto& line : ctx.lines) {
        if (line.width <= ctx.max_width_limit || line.runs.size() != 1) continue;
        auto& run = line.runs.front();
        const float size = std::max(1.0f, run.style.size <= 0.0f ? ctx.font_size_hint : run.style.size);
        const float ellipsis_width = measure_string(ctx.input, run.style, "...", size);
        while (!run.text.empty() && measure_string(ctx.input, run.style, run.text + "...", size) + 0.001f > ctx.max_width_limit) {
            const size_t end = grapheme_byte_offset_at(std::string_view(run.text), 1);
            if (end == 0) break;
            run.text.erase(run.text.size() - end);
        }
        run.text += "...";
        run.width = measure_string(ctx.input, run.style, run.text, size);
        line.width = run.width;
        (void)ellipsis_width;
    }
}

TextLayoutResult emit_layout(const InlineLayoutContext& ctx) {
    TextLayoutResult result;
    result.font_size = ctx.font_size_hint;
    float max_width = 0.0f;
    for (const auto& line : ctx.lines) max_width = std::max(max_width, line.width);
    const float line_height = std::max(1.0f, ctx.input.style.size * ctx.input.style.line_height);
    result.size = {max_width, static_cast<float>(ctx.lines.size()) * line_height};
    for (size_t index = 0; index < ctx.lines.size(); ++index) {
        const auto& state = ctx.lines[index];
        TextLayoutLine line;
        line.width = state.width;
        line.ascent = std::max(state.ascent, ctx.font_size_hint * 0.78f);
        line.descent = std::max(state.descent, ctx.font_size_hint * 0.22f);
        line.baseline = line.ascent;
        line.position.y = static_cast<float>(index) * line_height;
        for (const auto& run : state.runs) line.text += run.text;
        const float alignment_width = ctx.max_width_limit > 0.0f ? ctx.max_width_limit : max_width;
        line.position.x = ctx.input.style.align == TextAlign::Center ? (alignment_width - line.width) * 0.5f : ctx.input.style.align == TextAlign::Right ? alignment_width - line.width : 0.0f;
        float cursor = line.position.x;
        for (auto run : state.runs) { run.position = {cursor, line.position.y}; cursor += run.width; line.runs.push_back(std::move(run)); }
        result.lines.push_back(std::move(line));
    }
    return result;
}

} // namespace

TextLayoutResult layout_inline_runs(const TextLayoutInput& input) {
    auto ctx = normalize_input(input);
    shape_measurement_alphabet(ctx);
    discover_lines(ctx);
    apply_ellipsis(ctx);
    return emit_layout(ctx);
}

} // namespace chronon3d::detail::text_layout
