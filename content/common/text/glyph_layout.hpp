#pragma once

// ── glyph_layout — FontEngine shaping + per-glyph measurement ────────────
//
// P1 refactor — extracted from `content/common/text_reveal_helpers.hpp`
// (Step 2 of 4).  Single-responsibility: shaping (HarfBuzz) and
// per-glyph layout (positioning).
//
// refactor(text): consolidate shape_text via ShapedGlyphLine + shape_glyph_line (Point 8) —
// Upstream already lands Point 8's single-shape caching via the
// `ShapedGlyphLine` class ctor (ctor calls `engine.shape_text` once and
// caches `m_run`; `.width()` / `.layout()` read from `m_run` with no
// re-shape).  This refactor introduces the FAIL-SOFT mirror — a free
// function `shape_glyph_line(...)` returning `std::optional<ShapedGlyphLine>`
// — and re-implements `measure_text_width` + `layout_glyphs` as 1-line
// thin-wrappers over it.  Byte-equivalence preserved verbatim.
//   - measure_text_width: return `shape_glyph_line(...)->width()` or 0.0f
//     on nullopt (fail-soft contract — same as upstream's try/catch wrapper
//     semantics).
//   - layout_glyphs: throw `std::runtime_error(make_shape_error_message(...))`
//     on nullopt (fail-loud contract per AGENTS.md §honesty + ADR-020
//     §fail-loud path); otherwise return positions with `ref_offset_x`
//     applied as a constant post-step (matches pre-refactor cursor-start
//     math).
//
// Namespace: chronon3d::content::text_reveal (single flat namespace per
// Cat-3 minimal-surface — preserves the 12 existing callers' `using`
// declarations).

#include <chronon3d/core/types/types.hpp>  // f32, Vec2 (canonical SDK types header)
#include <chronon3d/text/font_engine.hpp>  // FontEngine, FontSpec, GlyphRun

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::content::text_reveal {

// Forward declarations.
class ShapedGlyphLine;

namespace test_support {
    [[nodiscard]] const std::optional<GlyphRun>& get_raw_run(const ShapedGlyphLine&) noexcept;
}

// Per-glyph position result (centre-X + advance width, post-shaping).
struct GlyphPos {
    std::string ch;
    f32         center_x{0.0f};
    f32         width{0.0f};
};

// Source-text span for a shaped glyph.  Encapsulates the byte range in
// the original text that corresponds to one glyph, plus the glyph's
// advance (including tracking).  Used by ShapedGlyphLine::layout() to
// replace the previous O(n²) inner scan with an O(n) next-greater-
// element pass over cluster values.
struct GlyphClusterSpan {
    size_t start_glyph{0};   // index into GlyphRun::glyphs
    size_t end_glyph{0};     // exclusive (currently always start_glyph+1)
    size_t byte_offset{0};   // byte offset in source text
    size_t byte_len{0};      // bytes in source text
    f32    advance{0.0f};    // glyph advance_x + tracking

    // Build per-glyph cluster spans in O(n) using a next-greater-element
    // stack over the HarfBuzz cluster values.  Preserves the exact
    // "first by index" semantics of the legacy O(n²) inner scan.
    [[nodiscard]] static std::vector<GlyphClusterSpan> build(
        const std::vector<GlyphPosition>& glyphs,
        std::string_view text,
        f32 tracking);
};

// Axis-aligned bounding box for a shaped line of text (pixels).
struct GlyphLineBBox {
    f32 x0{0.0f};
    f32 y0{0.0f};
    f32 x1{0.0f};
    f32 y1{0.0f};

    [[nodiscard]] f32 width()  const noexcept { return x1 - x0; }
    [[nodiscard]] f32 height() const noexcept { return y1 - y0; }
};

// Single shaped line of text.  Shapes once via FontEngine and exposes
// width, per-glyph layout, cursor positions, bbox and reveal helpers
// without re-shaping the text.
//
// Public API contract:
//   - Fail-loud primary ctor (throws on shaping failure) — used by the
//     5 cinematic showcase callers (e.g., abyss_freefall_stagger.cpp) that
//     REQUIRE a valid shape result.
//   - Fail-soft `try_shape` static factory (returns `std::nullopt` on
//     shaping failure) — used by the `shape_glyph_line` free function
//     + the `measure_text_width` + `layout_glyphs` thin-wrappers.
//
// Per AGENTS.md `## Regole di lint documentale` `### C++ default-arg
// uniqueness per TU`, default args live ONLY in this .hpp; both the
// public fail-loud ctor and `try_shape` factory have all parameters
// explicit (no in-class default args).
class ShapedGlyphLine {
public:
    // ── Public read-only accessors (unchanged from upstream) ──
    [[nodiscard]] bool valid() const noexcept { return m_run.has_value(); }

    // Total advance width INCLUDING tracking, matching the legacy
    // measure_text_width output.
    [[nodiscard]] f32 width() const noexcept;

    // Per-glyph positions at FINAL locations.
    [[nodiscard]] std::vector<GlyphPos> layout() const;

    // X coordinate of the cursor before glyph `index` (0 == left edge).
    [[nodiscard]] f32 cursor_position(size_t index) const noexcept;

    // X coordinate of the cursor at the end of the line.
    [[nodiscard]] f32 cursor_at_end() const noexcept;

    // Axis-aligned bounding box of the shaped line.
    [[nodiscard]] GlyphLineBBox bbox() const noexcept;

    // Number of glyphs to reveal for a progress in [0, 1].
    [[nodiscard]] size_t reveal_count(f32 progress) const noexcept;

    // ── Public fail-loud ctor (the primary constructor used by 5 showcases) ──
    // Shapes the text. Throws std::runtime_error on shaping failure
    // (zero glyphs / missing font) — same fail-loud contract as
    // layout_glyphs (preserved for backward compat with cinematic showcase
    // callers that REQUIRE shape result).
    ShapedGlyphLine(const std::string& text, f32 font_size,
                    const FontSpec& spec, f32 tracking,
                    f32 ref_offset_x, FontEngine& engine);

    // ── Public fail-soft static factory (Point 8 entry point) ──
    // Returns `std::nullopt` on shaping failure instead of throwing.
    // Forwarded by `shape_glyph_line` free function.
    [[nodiscard]] static std::optional<ShapedGlyphLine> try_shape(
        std::string_view text, f32 font_size, const FontSpec& spec,
        f32 tracking, f32 ref_offset_x, FontEngine& engine);

private:
    std::string m_text;
    f32         m_tracking{0.0f};
    f32         m_ref_offset_x{0.0f};
    std::optional<GlyphRun> m_run;

    // Precalculated prefix sums of (advance_x + tracking) for O(1)
    // cursor queries.  m_prefix_advances[i] = cursor before glyph i,
    // with m_prefix_advances[0] = m_ref_offset_x.
    std::vector<float> m_prefix_advances;

    // Rebuild m_prefix_advances from m_run + m_tracking + m_ref_offset_x.
    // Called from both constructors.  The private try_shape ctor is
    // noexcept, so a bad_alloc here terminates — same as any noexcept
    // function that allocates.
    void rebuild_prefix_advances();

    // Private ctor used by try_shape factory — populate from a valid
    // GlyphRun directly (does NOT throw).
    ShapedGlyphLine(GlyphRun run, std::string text,
                    f32 tracking, f32 ref_offset_x);

    // Friend declaration allows shape_glyph_line free function to be
    // declared in the .hpp file and defined in the .cpp file without
    // the namespace pollution of "friend free function bodies".
    friend std::optional<ShapedGlyphLine> shape_glyph_line(
        std::string_view text, f32 font_size, const FontSpec& spec,
        f32 tracking, FontEngine& engine);

    // Test/internal support needs access to the cached GlyphRun.
    friend const std::optional<GlyphRun>& test_support::get_raw_run(const ShapedGlyphLine&) noexcept;
};

// shape_glyph_line — fail-soft free-function entry point (Point 8 mirror).
//
// Returns `std::optional<ShapedGlyphLine>` (std::nullopt on engine.shape_text
// failure OR run->glyphs.empty()).  When a caller needs a valid shape
// result, prefer:
//   - measure_text_width (fail-soft: silently returns 0.0f on nullopt)
//   - layout_glyphs (fail-loud: throws std::runtime_error on nullopt)
//   - ShapedGlyphLine public ctor (fail-loud: throws std::runtime_error)
//
// Byte-equivalence with the upstream (pre-refactor) measure_text_width +
// layout_glyphs is preserved verbatim — the underlying ShapedGlyphLine
// class ctor logic (engine.shape_text + cached m_run) is unchanged.
[[nodiscard]] std::optional<ShapedGlyphLine> shape_glyph_line(
    std::string_view text, f32 font_size, const FontSpec& font,
    f32 tracking, FontEngine& engine);

// measure_text_width — total advance width INCLUDING tracking, matching
// layout_glyphs output.  Returns 0.0f if shaping fails (fail-soft; layout_glyphs
// fail-loud via throw).
//
// Point 8 thin-wrapper over shape_glyph_line() — single engine.shape_text
// call shared with layout_glyphs when both are invoked consecutively
// (e.g., the typewriter reveal flow).
[[nodiscard]] f32 measure_text_width(const std::string& text, f32 font_size,
                                     const FontSpec& spec, f32 tracking,
                                     FontEngine& engine);

// layout_glyphs — per-glyph positions at FINAL locations (only opacity /
// position animate per frame so the text block stays perfectly stable).
// Throws std::runtime_error on HarfBuzz shaping failure (zero glyphs)
// per AGENTS.md §honesty (fail-loud path = font resolution / AssetResolver
// errors land here).
//
// Point 8 thin-wrapper over shape_glyph_line() — single engine.shape_text
// call shared with measure_text_width when both are invoked consecutively.
// Byte-equivalence with pre-refactor version preserved via:
//   - shape_glyph_line populates glyph positions starting at cursor=0 (relative)
//   - layout_glyphs post-applies ref_offset_x as a constant offset (matches
//     pre-refactor cursor math `cursor = ref_offset_x` start state)
[[nodiscard]] std::vector<GlyphPos> layout_glyphs(
    const std::string& text, f32 font_size,
    const FontSpec& spec, f32 tracking,
    f32 ref_offset_x,
    FontEngine& engine);

} // namespace chronon3d::content::text_reveal
