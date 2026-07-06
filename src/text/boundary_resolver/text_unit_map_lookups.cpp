// ═══════════════════════════════════════════════════════════════════════════
// text_unit_map_lookups.cpp — forward/inverse/count lookups (FASE 12)
// ═══════════════════════════════════════════════════════════════════════════
//
// Member function definitions for TextUnitMap lookup methods.
// Extracted from text_unit_map.cpp to keep the constructor TU focused.

#include <chronon3d/text/text_unit_map.hpp>

#include <algorithm>
#include <cstring>
#include <string>

namespace chronon3d {

namespace {

// ── Binary-search helpers on monotonic forward vectors ────────────────
// Copied from text_unit_map.cpp for TU-local visibility.

template <typename ForwardVec>
[[nodiscard]] u32 first_child_with_parent(const ForwardVec& fwd, u32 target) noexcept {
    if (fwd.empty()) return InvalidIndex;
    if (target == InvalidIndex) return InvalidIndex;
    auto it = std::lower_bound(fwd.begin(), fwd.end(), target);
    if (it == fwd.end() || *it != target) return InvalidIndex;
    return static_cast<u32>(it - fwd.begin());
}

template <typename ForwardVec>
[[nodiscard]] u32 child_count_with_parent(const ForwardVec& fwd, u32 target) noexcept {
    if (fwd.empty() || target == InvalidIndex) return 0;
    auto lo = std::lower_bound(fwd.begin(), fwd.end(), target);
    if (lo == fwd.end() || *lo != target) return 0;
    auto hi = std::upper_bound(lo, fwd.end(), target);
    return static_cast<u32>(hi - lo);
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Forward lookups (O(1))
// ═══════════════════════════════════════════════════════════════════════════

std::optional<u32>
TextUnitMap::byte_to_codepoint(u32 byte_idx) const noexcept {
    if (byte_idx >= byte_to_codepoint_.size()) return std::nullopt;
    if (byte_to_codepoint_[byte_idx] == InvalidIndex) return std::nullopt;
    return byte_to_codepoint_[byte_idx];
}

std::optional<u32>
TextUnitMap::codepoint_to_grapheme(u32 cp_idx) const noexcept {
    if (cp_idx >= cp_to_grapheme_.size()) return std::nullopt;
    if (cp_to_grapheme_[cp_idx] == InvalidIndex) return std::nullopt;
    return cp_to_grapheme_[cp_idx];
}

std::optional<u32>
TextUnitMap::grapheme_to_glyph(u32 grapheme_idx) const noexcept {
    if (grapheme_idx >= grapheme_to_glyph_.size()) return std::nullopt;
    return grapheme_to_glyph_[grapheme_idx];
}

std::optional<u32>
TextUnitMap::glyph_to_word(u32 glyph_idx) const noexcept {
    if (glyph_idx >= glyph_to_word_.size()) return std::nullopt;
    if (glyph_to_word_[glyph_idx] == InvalidIndex) return std::nullopt;
    return glyph_to_word_[glyph_idx];
}

std::optional<u32>
TextUnitMap::word_to_line(u32 word_idx) const noexcept {
    if (word_idx >= word_to_line_.size()) return std::nullopt;
    if (word_to_line_[word_idx] == InvalidIndex) return std::nullopt;
    return word_to_line_[word_idx];
}

std::optional<u32>
TextUnitMap::line_to_paragraph(u32 line_idx) const noexcept {
    if (line_idx >= line_to_paragraph_.size()) return std::nullopt;
    if (line_to_paragraph_[line_idx] == InvalidIndex) return std::nullopt;
    return line_to_paragraph_[line_idx];
}

std::optional<u32>
TextUnitMap::paragraph_to_span(u32 para_idx) const noexcept {
    if (para_idx >= paragraph_to_span_.size()) return std::nullopt;
    if (paragraph_to_span_[para_idx] == InvalidIndex) return std::nullopt;
    return paragraph_to_span_[para_idx];
}

// ═══════════════════════════════════════════════════════════════════════════
// Inverse lookups (O(log N) via lower_bound)
// ═══════════════════════════════════════════════════════════════════════════

std::optional<u32>
TextUnitMap::codepoint_to_byte(u32 cp_idx) const noexcept {
    u32 first = first_child_with_parent(byte_to_codepoint_, cp_idx);
    if (first == InvalidIndex) return std::nullopt;
    return first;
}

std::optional<u32>
TextUnitMap::grapheme_to_codepoint(u32 grapheme_idx) const noexcept {
    u32 first = first_child_with_parent(cp_to_grapheme_, grapheme_idx);
    if (first == InvalidIndex) return std::nullopt;
    return first;
}

std::optional<u32>
TextUnitMap::glyph_to_grapheme(u32 glyph_idx) const noexcept {
    if (glyph_count_ == 0 || glyph_idx >= glyph_count_) return std::nullopt;
    const u32 N = static_cast<u32>(grapheme_to_glyph_.size());
    for (u32 g = 0; g < N; ++g) {
        const u32 g_first = grapheme_to_glyph_[g];
        if (g_first == InvalidIndex) continue;
        if (g_first > glyph_idx) continue;
        const u32 g_next_first = (g + 1 < N)
            ? (grapheme_to_glyph_[g + 1] == InvalidIndex
                ? glyph_count_ : grapheme_to_glyph_[g + 1])
            : glyph_count_;
        if (glyph_idx < g_next_first) return g;
    }
    return std::nullopt;
}

std::optional<u32>
TextUnitMap::word_to_glyph(u32 word_idx) const noexcept {
    u32 first = first_child_with_parent(glyph_to_word_, word_idx);
    if (first == InvalidIndex) return std::nullopt;
    return first;
}

std::optional<u32>
TextUnitMap::line_to_word(u32 line_idx) const noexcept {
    u32 first = first_child_with_parent(word_to_line_, line_idx);
    if (first == InvalidIndex) return std::nullopt;
    return first;
}

std::optional<u32>
TextUnitMap::paragraph_to_line(u32 para_idx) const noexcept {
    u32 first = first_child_with_parent(line_to_paragraph_, para_idx);
    if (first == InvalidIndex) return std::nullopt;
    return first;
}

std::optional<u32>
TextUnitMap::span_to_paragraph(u32 span_idx) const noexcept {
    for (u32 p = 0; p < paragraph_to_span_.size(); ++p) {
        if (paragraph_to_span_[p] == span_idx) return p;
    }
    return std::nullopt;
}

// ═══════════════════════════════════════════════════════════════════════════
// Count helpers
// ═══════════════════════════════════════════════════════════════════════════

u32
TextUnitMap::byte_count_for_codepoint(u32 codepoint_idx) const noexcept {
    return child_count_with_parent(byte_to_codepoint_, codepoint_idx);
}

u32
TextUnitMap::codepoint_count_for_grapheme(u32 grapheme_idx) const noexcept {
    return child_count_with_parent(cp_to_grapheme_, grapheme_idx);
}

u32
TextUnitMap::glyph_count_for_grapheme(u32 grapheme_idx) const noexcept {
    if (glyph_count_ == 0 || grapheme_idx >= grapheme_count_) return 0;
    const u32 g_first = grapheme_to_glyph_[grapheme_idx];
    if (g_first == InvalidIndex) return 0;
    u32 g = grapheme_idx + 1;
    while (g < grapheme_count_ && grapheme_to_glyph_[g] == InvalidIndex) ++g;
    const u32 g_next_first = (g < grapheme_count_)
        ? grapheme_to_glyph_[g]
        : glyph_count_;
    return (g_next_first >= g_first) ? (g_next_first - g_first) : 0;
}

u32
TextUnitMap::glyph_count_for_word(u32 word_idx) const noexcept {
    return child_count_with_parent(glyph_to_word_, word_idx);
}

u32
TextUnitMap::word_count_for_line(u32 line_idx) const noexcept {
    return child_count_with_parent(word_to_line_, line_idx);
}

u32
TextUnitMap::line_count_for_paragraph(u32 para_idx) const noexcept {
    return child_count_with_parent(line_to_paragraph_, para_idx);
}

u32
TextUnitMap::paragraph_count_for_span(u32 span_idx) const noexcept {
    if (paragraph_count_ == 0 || span_idx == InvalidIndex) return 0;
    u32 count = 0;
    for (u32 p = 0; p < paragraph_to_span_.size(); ++p) {
        if (paragraph_to_span_[p] == span_idx) ++count;
    }
    return count;
}

// ── Single-call 8-level ladder query ───────────────────────────────────

TextUnitIndices
TextUnitMap::identity_at_byte(u32 byte_idx) const noexcept {
    TextUnitIndices out{};
    if (byte_idx >= byte_to_codepoint_.size()) {
        out.byte_idx = InvalidIndex;
        return out;
    }

    const u32 cp_at = byte_to_codepoint_[byte_idx];
    out.byte_idx = byte_idx;
    out.char_idx = cp_at;

    if (cp_at < cp_to_grapheme_.size()) {
        out.grapheme_idx = cp_to_grapheme_[cp_at];
    }

    if (auto g_glyph = grapheme_to_glyph(out.grapheme_idx)) {
        out.glyph_idx = *g_glyph;
    }

    if (auto w = glyph_to_word(out.glyph_idx)) {
        out.word_idx = *w;
    }

    if (auto l = word_to_line(out.word_idx)) {
        out.line_idx = *l;
    }

    if (auto p = line_to_paragraph(out.line_idx)) {
        out.para_idx = *p;
    }

    if (auto s = paragraph_to_span(out.para_idx)) {
        out.span_idx = *s;
    }

    return out;
}

// ── Semantic span name → idx (O(N) linear; span sets are small) ────────

u32
TextUnitMap::span_index_by_name(std::string_view name) const noexcept {
    if (span_names_.empty() || name.empty()) return InvalidIndex;
    for (u32 i = 0; i < span_names_.size(); ++i) {
        if (span_names_[i] == name) return i;
    }
    return InvalidIndex;
}

} // namespace chronon3d
