#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_unit_map.hpp — TEXT-UNM-01 canonical 8-level text identity ladder
// ═══════════════════════════════════════════════════════════════════════════
//
// Replaces the implicit `Character == Grapheme` conflation that lived in
// `glyph_selector.hpp::TextUnitMap` (narrow 3-level: grapheme/word/line) with
// an explicit 8-level ladder anchored at the byte level:
//
//     Byte  ⇄  CodePoint  ⇄  GraphemeCluster  ⇄  ShapedGlyph
//          ⇄  Word  ⇄  Line  ⇄  Paragraph  ⇄  SemanticSpan
//
// All forward + inverse lookups are OOB-safe: out-of-range inputs return
// `std::nullopt` (caller-side) or `u32 `InvalidIndex`` sentinel (struct-side).
//
// Anti-duplication invariants:
//   • UAX#29 GB rule coverage is a bounded subset (GB1, GB2, GB11 ZWJ emoji
//     sequences, GB999 default break) — composes on the existing
//     `chronon3d::detail::UAX#29` helpers where present.
//   • UAX#29 WB rule coverage is also bounded (WB1, WB5a whitespace, WB999
//     default no-break) — no external ICU/Boost import.
//   • GLYPH→WORD mapping composes on `PlacedGlyphRun::Cluster` byte ranges
//     already computed by `FontEngine` HBB shaping — does NOT re-shape.
//   • WORD→LINE / LINE→PARAGRAPH mapping composes on `PlacedParagraphLayout`
//     supplied by the caller (the resolved text tree).
//
// The narrow `TextUnitMap` in `glyph_selector.hpp` is preserved unchanged
// for back-compat with the 30+ callers that use the selector API
// (`TextSelectorUnit::{Glyph, Grapheme, Character, Word, Line}`).
// Migrating them to the 8-level ladder is a follow-up atom
// (TICKET-046 / next track).
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/text/font_engine.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d {

// ── Sentinel for OOB or unset indices ──────────────────────────────────
//
// Standard project-wide pattern (cf. `kInvalidGraphInstanceId` etc.).
// Default-init "unset" marker.  Tests assert that this value is NEVER
// produced by valid forward/inverse mappings within range.

inline constexpr u32 InvalidIndex{UINT32_MAX};

// ── TextUnitKind — the 8 levels of text identity ──────────────────────
//
// Used as a discriminator for the TextUnitMap::identity_at_byte() ladder
// query.  Caller-side, the integer value is used as the row-index of the
// ladder table (top-to-bottom = byte→span).

enum class TextUnitKind : u8 {
    Byte = 0,
    CodePoint = 1,
    GraphemeCluster = 2,
    ShapedGlyph = 3,
    Word = 4,
    Line = 5,
    Paragraph = 6,
    SemanticSpan = 7,
};

// ── TextUnitIndices — bundle of all 8 indices for a single anchor ─────
//
// Returned by `identity_at_byte()` to answer "which unit does byte i
// belong to at every level of the ladder?" with a single call.

struct TextUnitIndices {
    u32 byte_idx{InvalidIndex};
    u32 char_idx{InvalidIndex};     // = codepoint_idx, kept for back-compat with selector API.
    u32 grapheme_idx{InvalidIndex};
    u32 glyph_idx{InvalidIndex};
    u32 word_idx{InvalidIndex};
    u32 line_idx{InvalidIndex};
    u32 para_idx{InvalidIndex};
    u32 span_idx{InvalidIndex};
};

// ── SemanticSpanRef — caller-supplied named byte range ────────────────
//
// Optional input to the TextUnitMap constructor.  Caller passes arbitrary
// programmatic semantic tags (e.g., from a DSL parser).  The map records
// which paragraph the span falls into (paragraph_to_span mapping).

struct SemanticSpanRef {
    std::string name{};     // owned string for deterministic lookup (string_view would dangle)
    u32 byte_start{0};
    u32 byte_end{0};
};

// ── PlacedParagraphLayout — minimal paragraph/line aggregation ────────
//
// Composable view of paragraph→line→word structure the caller (typically
// `text_resolver::resolve_text_run_tree()`) supplies.  Kept minimal here
// because the engine's `ParagraphLayout` is a richer type that includes
// line metrics + composition data we don't need for TextUnitMap.  The
// caller is responsible for projecting ParagraphLayout into this minimal
// form before passing to the TextUnitMap constructor.
//
// Invariant: `lines.size() == total word_count when summed.`

struct PlacedParagraphLayout {
    struct PlacedLineRef {
        u32 first_word_idx{0};
        u32 word_count{0};
    };
    std::vector<PlacedLineRef> lines;
};

// ── TextUnitMap — canonical 8-level identity ladder ──────────────────

class TextUnitMap {
public:
    // ── Construction ──────────────────────────────────────────────────
    //
    // Builds all 7 forward maps (byte→codepoint → paragraph→span) from
    // (utf8, placed, paragraph_layout, semantic_spans).  Empty / partial
    // inputs are valid; missing levels produce a count of 0 and forward
    // maps of length 0 (all lookups return nullopt).
    //
    // @param utf8              Source UTF-8 text (treated as opaque byte
    //                         buffer for offsets).  View is borrowed; caller
    //                         owns the underlying buffer.
    // @param placed            HarfBuzz-shaped glyph run.  Uses
    //                         `Cluster::byte_offset` + `byte_len` for the
    //                         grapheme→glyph and glyph→word tables.
    // @param paragraph_layout  Minimal projection of paragraph/line/word slots.
    //                         Empty is valid (paragraph_count=0).
    // @param semantic_spans    Optional named byte ranges.  Each span is
    //                         mapped to the containing paragraph (or invalid
    //                         if no paragraph covers it).
    // @param max_source_bytes  Hard cap on source size to bound memory;
    //                         default 1MB prevents unbounded user input.
    TextUnitMap(std::string_view utf8,
               const PlacedGlyphRun& placed,
               const PlacedParagraphLayout& paragraph_layout = {},
               const std::vector<SemanticSpanRef>& semantic_spans = {},
               u32 max_source_bytes = 1'000'000);

    // ── Counts ────────────────────────────────────────────────────────
    [[nodiscard]] u32 byte_count()      const noexcept { return utf8_byte_count_; }
    [[nodiscard]] u32 codepoint_count() const noexcept { return codepoint_count_; }
    [[nodiscard]] u32 grapheme_count()  const noexcept { return grapheme_count_; }
    [[nodiscard]] u32 glyph_count()     const noexcept { return glyph_count_; }
    [[nodiscard]] u32 word_count()      const noexcept { return word_count_; }
    [[nodiscard]] u32 line_count()      const noexcept { return line_count_; }
    [[nodiscard]] u32 paragraph_count() const noexcept { return paragraph_count_; }
    [[nodiscard]] u32 span_count()      const noexcept { return span_count_; }

    // ── Forward lookups (child → parent index) ────────────────────────
    //
    // All O(1) on dense vectors.  Returns `nullopt` when:
    //   - the level is empty (count == 0 for that level)
    //   - the input index is out of range
    //   - the level's forward map is shorter than the input span
    //     (e.g. codepoint fewer than byte — should never happen but guards)
    [[nodiscard]] std::optional<u32>
        byte_to_codepoint(u32 byte_idx)         const noexcept;
    [[nodiscard]] std::optional<u32>
        codepoint_to_grapheme(u32 codepoint_idx) const noexcept;
    [[nodiscard]] std::optional<u32>
        grapheme_to_glyph(u32 grapheme_idx)    const noexcept;
    [[nodiscard]] std::optional<u32>
        glyph_to_word(u32 glyph_idx)            const noexcept;
    [[nodiscard]] std::optional<u32>
        word_to_line(u32 word_idx)              const noexcept;
    [[nodiscard]] std::optional<u32>
        line_to_paragraph(u32 line_idx)         const noexcept;
    [[nodiscard]] std::optional<u32>
        paragraph_to_span(u32 para_idx)         const noexcept;

    // ── Inverse lookups (parent → child range) ────────────────────────
    //
    // We return the first child for the given parent (children occupy a
    // contiguous range under the forward map).  `child_count_for_parent`
    // provides the size of that range.  All O(log N) via binary search on
    // the forward map (cache-coalescing friendly vs unordered_map).

    [[nodiscard]] std::optional<u32>
        codepoint_to_byte(u32 codepoint_idx)    const noexcept;
    [[nodiscard]] std::optional<u32>
        grapheme_to_codepoint(u32 grapheme_idx) const noexcept;
    [[nodiscard]] std::optional<u32>
        glyph_to_grapheme(u32 glyph_idx)        const noexcept;
    [[nodiscard]] std::optional<u32>
        word_to_glyph(u32 word_idx)             const noexcept;
    [[nodiscard]] std::optional<u32>
        line_to_word(u32 line_idx)              const noexcept;
    [[nodiscard]] std::optional<u32>
        paragraph_to_line(u32 para_idx)         const noexcept;
    [[nodiscard]] std::optional<u32>
        span_to_paragraph(u32 span_idx)         const noexcept;

    // ── Range queries (parent → child count) ─────────────────────────
    //
    // Companion to the inverse lookups: returns the number of children
    // that belong to a given parent.  Useful for "loop over the word /
    // line / paragraph" without exposing the inverse pair as a vector.

    [[nodiscard]] u32
        byte_count_for_codepoint(u32 codepoint_idx)        const noexcept;
    [[nodiscard]] u32
        codepoint_count_for_grapheme(u32 grapheme_idx)    const noexcept;
    [[nodiscard]] u32
        glyph_count_for_grapheme(u32 grapheme_idx)         const noexcept;
    [[nodiscard]] u32
        glyph_count_for_word(u32 word_idx)                 const noexcept;
    [[nodiscard]] u32
        word_count_for_line(u32 line_idx)                  const noexcept;
    [[nodiscard]] u32
        line_count_for_paragraph(u32 para_idx)             const noexcept;
    [[nodiscard]] u32
        paragraph_count_for_span(u32 span_idx)             const noexcept;

    // ── 8-level ladder query (byte anchor → all 8 indices) ───────────
    //
    // Single-call convenience for "what unit does byte i belong to at
    // every ladder level?"  Returns a struct with `InvalidIndex` set on
    // any unused level.

    [[nodiscard]] TextUnitIndices
        identity_at_byte(u32 byte_idx) const noexcept;

    // ── Semantic span name → span_idx (O(N) linear, span sets are small) ─
    [[nodiscard]] u32 span_index_by_name(std::string_view name) const noexcept;

    // ── Adapter API (TICKET-046 Phase 0) — narrow struct TextUnitMap compatibility ──
    //
    // Drop-in replacement for the narrow `struct TextUnitMap` in
    // `glyph_selector.hpp`.  Migrating callers swap `#include
    // <glyph_selector.hpp>` for `<text_unit_map.hpp>` and use these
    // accessors instead of direct vector field access.

    /// Glyph→grapheme O(1) lookup.  Returns InvalidIndex if glyph_idx OOB.
    [[nodiscard]] u32 compat_glyph_to_grapheme(u32 glyph_idx) const noexcept {
        if (auto g = glyph_to_grapheme(glyph_idx)) return *g;
        return InvalidIndex;
    }

    /// Glyph→word O(1) direct lookup.  Returns InvalidIndex if glyph_idx OOB.
    [[nodiscard]] u32 compat_glyph_to_word(u32 glyph_idx) const noexcept {
        return glyph_idx < glyph_to_word_.size() ? glyph_to_word_[glyph_idx] : InvalidIndex;
    }

    /// Glyph→line O(1) via glyph→word→line chain.  Returns InvalidIndex if OOB.
    [[nodiscard]] u32 glyph_to_line(u32 glyph_idx) const noexcept;

    /// First glyph index for a given word (O(n) scan).  Returns InvalidIndex if none.
    [[nodiscard]] u32 first_glyph_for_word(u32 word_idx) const noexcept;

    /// First glyph index for a given line (O(n) scan).  Returns InvalidIndex if none.
    [[nodiscard]] u32 first_glyph_for_line(u32 line_idx) const noexcept;

    /// Drop-in compatible unit_index(): maps a glyph index to its unit index
    /// for the given TextUnitKind.  Returns InvalidIndex on OOB or empty level.
    [[nodiscard]] u32 unit_index(u32 glyph_index, TextUnitKind unit) const noexcept;

    /// Drop-in compatible unit_count(): returns the total number of units
    /// for the given TextUnitKind.  Returns 0 for empty/unset levels.
    [[nodiscard]] u32 unit_count(TextUnitKind unit) const noexcept;

private:
    // ── Forward dense maps ────────────────────────────────────────────
    std::vector<u32> byte_to_codepoint_;   // size = byte_count
    std::vector<u32> cp_to_grapheme_;       // size = codepoint_count
    std::vector<u32> grapheme_to_glyph_;    // size = grapheme_count
    std::vector<u32> glyph_to_word_;        // size = glyph_count
    std::vector<u32> word_to_line_;         // size = word_count
    std::vector<u32> line_to_paragraph_;    // size = line_count
    std::vector<u32> paragraph_to_span_;    // size = paragraph_count (per-paragraph first-span)

    // ── Counts (cached at construction) ───────────────────────────────
    u32 utf8_byte_count_{0};
    u32 codepoint_count_{0};
    u32 grapheme_count_{0};
    u32 glyph_count_{0};
    u32 word_count_{0};
    u32 line_count_{0};
    u32 paragraph_count_{0};
    u32 span_count_{0};

    // ---- Span name lookup table -------------------------------------
    // Owned copy of SemanticSpanRef.name strings, indexed parallel to
    // semantic_spans for O(N) by-name lookup. Lifetime-safe because
    // the ctor copies names; the ctor's const vector<SemanticSpanRef>&
    // parameter is a borrowed view of caller-owned storage.
    std::vector<std::string> span_names_;  // size = span_count_
};

// ── Forward declaration of the constructor's logging hook ────────────────
//
// Builders may produce OOB / duplicate / malformed mappings when given
// pathological inputs.  We don't log inline (the constructors are
// noexcept) — instead the TextUnitMap records the same statistics a
// post-build `TextUnitMap::audit()` call would surface.

} // namespace chronon3d
