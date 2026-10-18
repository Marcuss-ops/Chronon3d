// ═══════════════════════════════════════════════════════════════════════════
// text_unit_map.cpp — TEXT-UNM-01 implementation
// ═══════════════════════════════════════════════════════════════════════════
//
// Builds the 8 dense forward maps + count helpers for `TextUnitMap`.  All
// construction-time work; lookup methods are pure O(1) / O(log N) on
// precomputed vectors.
//
// Anti-duplication invariants (per the retired docs/ANTI_DUPLICATION_RULES.md):  // drift-class: historical (doc retired)
//   • ICU is the sole grapheme/word boundary authority; third-party Unicode
//     types do not escape the internal boundary adapter.
//   • Composes on existing UTF-8 decoder helpers for byte/codepoint mapping.
//   • Bit-exact deterministic: no threads, no time, no PRNG.
//
// Algorithm (single-pass-per-level, no stale loops):
//   Level 1  byte → codepoint       : utf-8 walk, each byte tagged with cp_idx
//   Level 2  codepoint → grapheme   : ICU UAX#29 byte boundary mapping
//   Level 3  grapheme → glyph       : map first-cp-of-grapheme → first-HB-cluster
//   Level 4  glyph → word           : ICU UAX#29 word segments + rule status
//   Level 5  word → line            : from PlacedParagraphLayout.lines
//   Level 6  line → paragraph       : 1-paragraph model (paragraph 0); follow-up
//                                     to extend to multi-paragraph layout.
//   Level 7  paragraph → span       : from SemanticSpanRef list; first wins (1-para).

#include <chronon3d/text/text_unit_map.hpp>
#include <chronon3d/text/font_engine.hpp>

#include "src/text/unicode/utf8_decoder.hpp"
#include "src/text/unicode/whitespace.hpp"
#include "text_boundary_resolver.hpp"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <utility>

namespace chronon3d {

namespace {

// ── Binary-search helpers on monotonic forward vectors ────────────────
//
// Forward maps `child[i] → parent[i]` are monotonic in the child index
// (children with the same parent form a contiguous range).  For inverse
// lookups we binary-search for the first child with the target parent.

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
// Construction (single-pass per level)
// ═══════════════════════════════════════════════════════════════════════════

TextUnitMap::TextUnitMap(std::string_view utf8,
                         const PlacedGlyphRun& placed,
                         const PlacedParagraphLayout& paragraph_layout,
                         const std::vector<SemanticSpanRef>& semantic_spans,
                         u32 max_source_bytes)
    :
    utf8_byte_count_{static_cast<u32>(
        std::min<size_t>(utf8.size(), max_source_bytes))},
    span_count_{static_cast<u32>(semantic_spans.size())} {

    // Span name lookup table: copy names from SemanticSpanRef[] for O(N)
    // span_index_by_name() resolution. Owned copy means the caller's
    // vector<SemanticSpanRef> may be temporary.
    span_names_.reserve(semantic_spans.size());
    for (const auto& s : semantic_spans) {
        span_names_.push_back(s.name);
    }

    // Cap silently: when source exceeds max_source_bytes, truncate.
    if (utf8.size() > max_source_bytes) {
        utf8 = std::string_view(utf8.data(), max_source_bytes);
    }

    // ── Level 1: byte → codepoint ─────────────────────────────────────
    // Walk UTF-8 byte-by-byte: each byte gets its parent cp index.
    byte_to_codepoint_.resize(utf8_byte_count_, InvalidIndex);
    {
        u32 cp_idx = 0;
        std::size_t i = 0;
        while (i < utf8_byte_count_) {
            const std::size_t before = i;
            const char32_t cp = text::unicode::decode_codepoint(utf8, i);
            (void)cp;
            const std::size_t consumed = i - before;
            const u32 fill_end = static_cast<u32>(
                std::min<std::size_t>(before + consumed, utf8_byte_count_));
            for (u32 k = static_cast<u32>(before); k < fill_end; ++k) {
                byte_to_codepoint_[k] = cp_idx;
            }
            ++cp_idx;
        }
        codepoint_count_ = cp_idx;
    }

    // ── Level 2: codepoint → grapheme ─────────────────────────────────
    // ICU owns UAX#29 grapheme segmentation.  Convert its UTF-8 byte
    // boundaries into the dense codepoint map consumed by TextUnitMap.
    text::boundary::IcuBoundaryResolver boundary_resolver;
    const auto boundary_map = boundary_resolver.resolve(utf8, {});
    cp_to_grapheme_.assign(codepoint_count_, InvalidIndex);
    grapheme_count_ = boundary_map.grapheme_boundaries.size() > 1
        ? static_cast<u32>(boundary_map.grapheme_boundaries.size() - 1)
        : 0;

    for (u32 cp_i = 0; cp_i < codepoint_count_; ++cp_i) {
        const u32 byte_idx = first_child_with_parent(byte_to_codepoint_, cp_i);
        if (byte_idx == InvalidIndex || boundary_map.grapheme_boundaries.empty()) {
            continue;
        }
        const auto it = std::upper_bound(
            boundary_map.grapheme_boundaries.begin(),
            boundary_map.grapheme_boundaries.end(),
            byte_idx);
        if (it != boundary_map.grapheme_boundaries.begin()) {
            cp_to_grapheme_[cp_i] = static_cast<u32>(
                std::distance(boundary_map.grapheme_boundaries.begin(), it) - 1);
        }
    }

    // ── Level 3: grapheme → glyph ─────────────────────────────────────
    //
    // Each PlacedGlyphRun::Cluster is one HarfBuzz cluster, which
    // corresponds to one grapheme cluster (or multi-codepoint ligature).
    // We build glyph→grapheme, then pull out grapheme→glyph (first match).
    glyph_count_ = static_cast<u32>(placed.clusters.size());
    std::vector<u32> glyph_to_grapheme(glyph_count_, InvalidIndex);
    for (u32 gi = 0; gi < glyph_count_; ++gi) {
        const auto& cl = placed.clusters[gi];
        const u32 cluster_byte = static_cast<u32>(cl.byte_offset);
        if (cluster_byte >= byte_to_codepoint_.size()) continue;
        const u32 cp_at = byte_to_codepoint_[cluster_byte];
        if (cp_at >= cp_to_grapheme_.size()) continue;
        glyph_to_grapheme[gi] = cp_to_grapheme_[cp_at];
    }
    grapheme_to_glyph_.resize(grapheme_count_, InvalidIndex);
    // For each glyph i, if its grapheme == g AND it's the FIRST such
    // glyph we encounter, mark grapheme_to_glyph_[g] = i.
    for (u32 gi = 0; gi < glyph_count_; ++gi) {
        const u32 g = glyph_to_grapheme[gi];
        if (g != InvalidIndex && g < grapheme_count_ && grapheme_to_glyph_[g] == InvalidIndex) {
            grapheme_to_glyph_[g] = gi;
        }
    }

    // ── Level 4: glyph → word ─────────────────────────────────────────
    // ICU supplies the UAX#29 word segments and rule status.  Only segments
    // classified as words receive dense indices; punctuation and whitespace
    // remain attached to the nearest word for selector compatibility.
    glyph_to_word_.assign(glyph_count_, InvalidIndex);
    if (glyph_count_ > 0) {
        std::vector<u32> byte_to_word(utf8.size(), InvalidIndex);
        u32 word_idx = 0;
        for (const auto& segment : boundary_map.word_segments) {
            if (!segment.is_word || segment.byte_start >= utf8.size()) continue;
            const auto end = std::min(segment.byte_end, utf8.size());
            for (std::size_t byte = segment.byte_start; byte < end; ++byte) {
                byte_to_word[byte] = word_idx;
            }
            ++word_idx;
        }

        word_count_ = word_idx;
        for (u32 gi = 0; gi < glyph_count_; ++gi) {
            const auto& cl = placed.clusters[gi];
            const std::size_t byte = std::min<std::size_t>(cl.byte_offset, utf8.size());
            glyph_to_word_[gi] = byte < byte_to_word.size()
                ? byte_to_word[byte] : InvalidIndex;
        }

        // Non-word clusters (spaces/punctuation) follow the preceding word,
        // or the next word for leading separators.  This preserves the
        // selector API's historical whitespace exclusion behavior without
        // reimplementing word segmentation locally.
        u32 previous_word = InvalidIndex;
        for (u32 gi = 0; gi < glyph_count_; ++gi) {
            if (glyph_to_word_[gi] != InvalidIndex) {
                previous_word = glyph_to_word_[gi];
            } else if (previous_word != InvalidIndex) {
                glyph_to_word_[gi] = previous_word;
            }
        }
        u32 next_word = InvalidIndex;
        for (u32 gi = glyph_count_; gi > 0; --gi) {
            const u32 index = gi - 1;
            if (glyph_to_word_[index] != InvalidIndex) {
                next_word = glyph_to_word_[index];
            } else if (next_word != InvalidIndex) {
                glyph_to_word_[index] = next_word;
            }
        }

        if (word_count_ == 0) {
            word_count_ = 1;
            std::fill(glyph_to_word_.begin(), glyph_to_word_.end(), 0);
        }
    } else {
        word_count_ = 0;
    }

    // ── Level 5: word → line ──────────────────────────────────────────
    //
    // PlacedParagraphLayout.lines: each line references [first_word,
    // word_count).
    word_to_line_.assign(word_count_, 0);
    line_count_ = 0;
    if (word_count_ > 0 && !paragraph_layout.lines.empty()) {
        u32 line_idx = 0;
        u32 word_cursor = 0;
        for (size_t li = 0; li < paragraph_layout.lines.size(); ++li) {
            const auto& line = paragraph_layout.lines[li];
            const u32 first_w = std::min<u32>(line.first_word_idx, word_count_ - 1);
            const u32 last_w_excl = std::min<u32>(first_w + line.word_count, word_count_);
            for (u32 wi = first_w; wi < last_w_excl; ++wi) {
                word_to_line_[wi] = line_idx;
            }
            if (line.word_count > 0) ++line_idx;
        }
        // Words that were never assigned go to line 0.
        for (u32 wi = 0; wi < word_count_; ++wi) {
            if (word_to_line_[wi] == InvalidIndex) word_to_line_[wi] = 0;
        }
        line_count_ = line_idx > 0 ? line_idx : (word_count_ > 0 ? 1u : 0u);
    } else if (word_count_ > 0) {
        line_count_ = 1;
        word_to_line_.assign(word_count_, 0);  // all words → line 0
    }

    // ── Level 6: line → paragraph ─────────────────────────────────────
    // 1-paragraph model: all lines → paragraph 0.  Multi-paragraph
    // support requires extending PlacedParagraphLayout; follow-up atom.
    line_to_paragraph_.assign(line_count_, 0);
    paragraph_count_ = (line_count_ > 0) ? 1u : 0u;

    // ── Level 7: paragraph → span ─────────────────────────────────────
    paragraph_to_span_.assign(paragraph_count_, InvalidIndex);
    if (paragraph_count_ > 0 && span_count_ > 0) {
        // First span wins for the (single) paragraph.  This is the
        // simplified model; multi-paragraph attribution is follow-up.
        paragraph_to_span_[0] = 0;
    }
}

// ── Remaining method definitions (forward/inverse/count/identity_at_byte/
// span_index_by_name) are in text_unit_map_lookups.cpp (FASE 12).

} // namespace chronon3d
