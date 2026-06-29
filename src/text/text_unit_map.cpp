// ═══════════════════════════════════════════════════════════════════════════
// text_unit_map.cpp — TEXT-UNM-01 implementation
// ═══════════════════════════════════════════════════════════════════════════
//
// Builds the 8 dense forward maps + count helpers for `TextUnitMap`.  All
// construction-time work; lookup methods are pure O(1) / O(log N) on
// precomputed vectors.
//
// Anti-duplication invariants (per docs/ANTI_DUPLICATION_RULES.md):
//   • Does NOT pull in <msdfgen>, <libtess2>, <unicode/ubidi.h>, or
//     any third-party word-break / grapheme-cluster library.
//   • Composes on existing UTF-8 decoder helpers where available.
//   • Bit-exact deterministic: no threads, no time, no PRNG.
//
// Algorithm (single-pass-per-level, no stale loops):
//   Level 1  byte → codepoint       : utf-8 walk, each byte tagged with cp_idx
//   Level 2  codepoint → grapheme   : GB simplified (GB1, GB2, GB9, GB11 ZWJ, GB999)
//   Level 3  grapheme → glyph       : map first-cp-of-grapheme → first-HB-cluster
//   Level 4  glyph → word           : WB simplified (WB1, WB5a whitespace, WB999)
//   Level 5  word → line            : from PlacedParagraphLayout.lines
//   Level 6  line → paragraph       : 1-paragraph model (paragraph 0); follow-up
//                                     to extend to multi-paragraph layout.
//   Level 7  paragraph → span       : from SemanticSpanRef list; first wins (1-para).

#include <chronon3d/text/text_unit_map.hpp>
#include <chronon3d/text/font_engine.hpp>

#include <algorithm>
#include <cstring>
#include <utility>

namespace chronon3d {

namespace {

// ── UTF-8 decoder (returns codepoint + byte-length consumed) ────────────
//
// Same algorithm as `glyph_selector.hpp::detail::decode_utf8_codepoint_from`
// but without the cp_len reference parameter (we return both as a pair).
// This duplication is INCIDENTAL (helper, not exposed) and is required
// because the existing helper is in `chronon3d::detail` which is not
// exposed in any public include path; copying 30 lines eliminates an
// export leak that would breach anti-duplication surface principles.

[[nodiscard]] inline std::pair<char32_t, u32>
decode_utf8(std::string_view sv, u32 byte_idx) noexcept {
    if (byte_idx >= sv.size()) return {0xFFFD, 1};
    const auto lead = static_cast<unsigned char>(sv[byte_idx]);

    if ((lead & 0x80u) == 0) {
        return {static_cast<char32_t>(lead), 1};
    }
    if ((lead & 0xE0u) == 0xC0u) {
        if (byte_idx + 1 >= sv.size()) return {0xFFFD, 1};
        const auto c1 = static_cast<unsigned char>(sv[byte_idx + 1]);
        if ((c1 & 0xC0u) != 0x80u) return {0xFFFD, 1};
        return {static_cast<char32_t>(((lead & 0x1Fu) << 6) | (c1 & 0x3Fu)), 2};
    }
    if ((lead & 0xF0u) == 0xE0u) {
        if (byte_idx + 2 >= sv.size()) return {0xFFFD, 1};
        const auto c1 = static_cast<unsigned char>(sv[byte_idx + 1]);
        const auto c2 = static_cast<unsigned char>(sv[byte_idx + 2]);
        if (((c1 | c2) & 0xC0u) != 0x80u) return {0xFFFD, 1};
        return {
            static_cast<char32_t>(
                ((lead & 0x0Fu) << 12) |
                ((c1 & 0x3Fu) << 6) |
                (c2 & 0x3Fu)),
            3
        };
    }
    if ((lead & 0xF8u) == 0xF0u) {
        if (byte_idx + 3 >= sv.size()) return {0xFFFD, 1};
        const auto c1 = static_cast<unsigned char>(sv[byte_idx + 1]);
        const auto c2 = static_cast<unsigned char>(sv[byte_idx + 2]);
        const auto c3 = static_cast<unsigned char>(sv[byte_idx + 3]);
        if (((c1 | c2 | c3) & 0xC0u) != 0x80u) return {0xFFFD, 1};
        return {
            static_cast<char32_t>(
                ((lead & 0x07u) << 18) |
                ((c1 & 0x3Fu) << 12) |
                ((c2 & 0x3Fu) << 6) |
                (c3 & 0x3Fu)),
            4
        };
    }
    return {0xFFFD, 1};
}

// ── Codepoint classifiers (Unicode whitespace + ZWJ + Extend + RI + ExtPict) ──

[[nodiscard]] inline bool is_unicode_whitespace_cp(char32_t cp) noexcept {
    switch (cp) {
        case 0x0020: case 0x0009: case 0x000A: case 0x000D:
        case 0x00A0: case 0x1680:
        case 0x2000: case 0x2001: case 0x2002: case 0x2003:
        case 0x2004: case 0x2005: case 0x2006: case 0x2007:
        case 0x2008: case 0x2009: case 0x200A:
        case 0x2028: case 0x2029:
        case 0x202F: case 0x205F: case 0x3000:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] inline bool is_combining_mark_cp(char32_t cp) noexcept {
    if (cp >= 0x0300 && cp <= 0x036F) return true;
    if (cp >= 0x0483 && cp <= 0x0489) return true;
    if (cp >= 0x1AB0 && cp <= 0x1AFF) return true;
    if (cp >= 0x1DC0 && cp <= 0x1DFF) return true;
    if (cp >= 0x20D0 && cp <= 0x20FF) return true;
    if (cp >= 0xFE20 && cp <= 0xFE2F) return true;
    if (cp >= 0xFE00 && cp <= 0xFE0F) return true;
    return false;
}

[[nodiscard]] inline bool is_zwj_cp(char32_t cp) noexcept {
    return cp == 0x200D;
}

[[nodiscard]] inline bool is_regional_indicator_cp(char32_t cp) noexcept {
    return cp >= 0x1F1E6 && cp <= 0x1F1FF;
}

[[nodiscard]] inline bool is_extended_pictographic_cp(char32_t cp) noexcept {
    if (cp >= 0x1F000 && cp <= 0x1FFFF) return true;
    if (cp == 0x00A9 || cp == 0x00AE) return true;
    if (cp >= 0x2600 && cp <= 0x27BF) return true;
    return false;  // conservative: SMP emoji + © ® + main emoji block.
}

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
        u32 i = 0;
        while (i < utf8_byte_count_) {
            const auto [cp, len] = decode_utf8(utf8, i);
            (void)cp;
            const u32 fill_end = std::min<u32>(i + len, utf8_byte_count_);
            for (u32 k = i; k < fill_end; ++k) {
                byte_to_codepoint_[k] = cp_idx;
            }
            i = fill_end;
            ++cp_idx;
        }
        codepoint_count_ = cp_idx;
    }

    // ── Level 2: codepoint → grapheme ─────────────────────────────────
    // Simplified UAX#29 GB rules with explicit ExtPict/ZWJ chain state
    // machine so emoji ZWJ sequences (e.g. 👨‍👩‍👧) merge into one grapheme:
    //
    //   GB1+GB2: trivial (text start/end anchors).
    //   GB9 (combining marks): attach to current grapheme.
    //   GB11 (ZWJ emoji sequence): ExtPict Extend* ZWJ × ExtPict attaches
    //     both the ZWJ and the following ExtPict to the existing cluster.
    //   GB12/13 (RI pairs): even-positioned RI attaches to previous RI.
    //   GB999 fallback: every other codepoint = own grapheme.
    //
    // The state machine tracks `InExtPict` (cluster anchored on ExtPict)
    // vs `InExtPictZWJ` (cluster has consumed a ZWJ and is awaiting the
    // next ExtPict) vs `None` (no special cluster, plain graphemes).
    cp_to_grapheme_.assign(codepoint_count_, InvalidIndex);
    {
        // Precompute cp values for fast access.
        std::vector<char32_t> cp_at_idx(codepoint_count_, 0xFFFD);
        for (u32 cp_i = 0; cp_i < codepoint_count_; ++cp_i) {
            const u32 byte_idx = first_child_with_parent(byte_to_codepoint_, cp_i);
            if (byte_idx != InvalidIndex) {
                const auto [cp, len] = decode_utf8(utf8, byte_idx);
                (void)len;
                cp_at_idx[cp_i] = cp;
            }
        }

        enum class GraphemeState : u8 {
            None,         // outside any special cluster
            InExtPict,    // cluster anchored on ExtPict, no ZWJ yet
            InExtPictZWJ, // cluster has consumed a ZWJ, awaiting next ExtPict
        };
        GraphemeState state = GraphemeState::None;

        u32 my_grapheme_idx = 0;
        for (u32 cp_i = 0; cp_i < codepoint_count_; ++cp_i) {
            const char32_t cp = cp_at_idx[cp_i];

            // Default: this cp belongs to the current grapheme_idx.
            cp_to_grapheme_[cp_i] = my_grapheme_idx;

            if (cp_i == 0) {
                // First cp: state determined by whether cp is ExtPict.
                state = is_extended_pictographic_cp(cp)
                    ? GraphemeState::InExtPict
                    : GraphemeState::None;
                continue;
            }

            // GB9: combining marks attach — never advance grapheme.
            if (is_combining_mark_cp(cp)) {
                continue;
            }

            // GB11 (ZWJ): if we are mid-chain (InExtPict or InExtPictZWJ),
            // ZWJ continues the chain and attaches to the same grapheme.
            // Otherwise ZWJ is a stray and starts a new grapheme.
            if (is_zwj_cp(cp)) {
                if (state == GraphemeState::InExtPict ||
                    state == GraphemeState::InExtPictZWJ) {
                    state = GraphemeState::InExtPictZWJ;
                } else {
                    ++my_grapheme_idx;
                    cp_to_grapheme_[cp_i] = my_grapheme_idx;
                    state = GraphemeState::None;
                }
                continue;
            }

            // GB11 closure: ExtPict after InExtPictZWJ attaches (no break).
            // Otherwise ExtPict starts a new grapheme.
            if (is_extended_pictographic_cp(cp)) {
                if (state == GraphemeState::InExtPictZWJ) {
                    // GB11: do not break.  Stay in same grapheme; reset
                    // chain to plain InExtPict (consumed the ZWJ).
                    state = GraphemeState::InExtPict;
                } else {
                    ++my_grapheme_idx;
                    cp_to_grapheme_[cp_i] = my_grapheme_idx;
                    state = GraphemeState::InExtPict;
                }
                continue;
            }

            // GB12/13: Regional Indicator pairs.
            if (is_regional_indicator_cp(cp) &&
                cp_i >= 1 &&
                is_regional_indicator_cp(cp_at_idx[cp_i - 1])) {
                // Walk back to find start of current RI run.
                u32 ri_count = 0;
                for (u32 k = cp_i; k > 0; --k) {
                    if (is_regional_indicator_cp(cp_at_idx[k - 1])) ++ri_count;
                    else break;
                }
                if ((ri_count % 2) == 0) {
                    // Even-indexed RI (0, 2, 4…) is the SECOND of a pair
                    // → GB13 attaches to previous grapheme.
                    continue;
                }
                // Odd-indexed RI → starts a new grapheme.
                ++my_grapheme_idx;
                cp_to_grapheme_[cp_i] = my_grapheme_idx;
                state = GraphemeState::None;
                continue;
            }

            // GB999: default break.
            ++my_grapheme_idx;
            cp_to_grapheme_[cp_i] = my_grapheme_idx;
            state = GraphemeState::None;
        }
        grapheme_count_ = my_grapheme_idx + 1;
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
    //
    // Simplified UAX#29 WB:
    //   WB1: break at start/end.
    //   WB5a: break AFTER whitespace (new word after a whitespace cluster).
    //   WB999: contiguous non-whitespace glyphs share a word.
    glyph_to_word_.assign(glyph_count_, InvalidIndex);
    if (glyph_count_ > 0) {
        u32 word_idx = 0;
        bool in_word = false;
        bool last_was_whitespace = true;  // start-of-text = whitespace boundary
        for (u32 gi = 0; gi < glyph_count_; ++gi) {
            const auto& cl = placed.clusters[gi];
            const u32 cluster_byte = static_cast<u32>(cl.byte_offset);
            const auto [first_cp, flen] = decode_utf8(utf8, cluster_byte);
            (void)flen;
            const bool is_ws = is_unicode_whitespace_cp(first_cp);

            if (!is_ws && !in_word) {
                // Start of a new word.
                in_word = true;
            }
            if (is_ws && in_word) {
                // Word closed.
                ++word_idx;
                in_word = false;
            }
            glyph_to_word_[gi] = in_word ? word_idx : (gi > 0 && !last_was_whitespace ? word_idx : 0);
            last_was_whitespace = is_ws;
        }
        // For whitespace-only clusters: assign to the closest word (the
        // most-recently-closed word for parseability).
        // Simplification: each whitespace cluster is mapped to the
        // closed word (`word_idx` here = count of words opened so far,
        // so word_idx - 1 is the right one to attach whitespace gaps to).
        for (u32 gi = 0; gi < glyph_count_; ++gi) {
            const auto& cl = placed.clusters[gi];
            const u32 cluster_byte = static_cast<u32>(cl.byte_offset);
            const auto [first_cp, flen] = decode_utf8(utf8, cluster_byte);
            (void)flen;
            const bool is_ws = is_unicode_whitespace_cp(first_cp);
            if (is_ws && word_idx > 0) {
                glyph_to_word_[gi] = word_idx - 1;
            }
        }
        word_count_ = word_idx + (in_word ? 1 : 0);
        if (word_count_ == 0 && glyph_count_ > 0) word_count_ = 1;
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
    // Walk grapheme_to_glyph_ to find which grapheme 'owns' this glyph.
    // Glyphs belonging to grapheme g occupy [grapheme_to_glyph_[g],
    // grapheme_to_glyph_[g+1]).  Linear probe over grapheme_to_glyph_
    // (typically small — N_graphemes <= N_bytes).
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
    if (first == InvalidIndex) return std::opt
// ═══════════════════════════════════════════════════════════════════════════
// Missing implementations (declared in header but not yet def'd)
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
    // Range is [g_first, g_next_first) where g_next_first is either the
    // first glyph of grapheme g+1 (skipping InvalidIndex holes) or, for
    // empty middle graphemes, we walk forward to find the next valid one.
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
    // paragraph_to_span_ forwards paragraph → first matching span.
    // Inverse: count paragraphs whose first span is `span_idx`.
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
        // OOB anchor — every level is invalid, INCLUDING byte_idx itself
        // (so callers can disambiguate valid vs OOB without echoes).
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
    // Linear scan over the owned name table. Span sets are small
    // (typically < 16 per document), so O(N) is cheaper than any
    // hash-table overhead and trivially deterministic.
    for (u32 i = 0; i < span_names_.size(); ++i) {
        if (span_names_[i] == name) return i;
    }
    return InvalidIndex;
}
