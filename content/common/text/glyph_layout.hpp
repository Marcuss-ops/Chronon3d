#pragma once

// ── glyph_layout — FontEngine shaping + per-glyph measurement ────────────
//
// P1 refactor — extracted from `content/common/text_reveal_helpers.hpp`
// (Step 2 of 4).  Single-responsibility: shaping (HarfBuzz) and
// per-glyph layout (positioning).
//
// Canonical shaping primitive: `shape_glyph_line(...)` returns a
// `std::optional<ShapedGlyphLine>` and owns the single `FontEngine::shape_text`
// call. The legacy `ShapedGlyphLine::try_shape(...)` factory remains as a
// compatibility adapter during migration. Width and per-glyph layout are
// read from the returned shape without additional shaping calls.
//
// The offset-bearing overload is the canonical form; the zero-offset overload
// remains source-compatible for existing callers. `measure_text_width` and
// `layout_glyphs` remain transitional adapters until their callers migrate.
// Byte-equivalence is preserved verbatim.
//   - measure_text_width: return `shape_glyph_line(...)->width()` or 0.0f
//     on nullopt (fail-soft contract — same as upstream's try/catch wrapper
//     semantics).
//   - layout_glyphs: throw `std::runtime_error(make_shape_error_message(...))`
//     on nullopt (fail-loud contract per AGENTS.md §honesty + ADR-020
//     §fail-loud path); otherwise return positions from the offset-bearing
//     canonical shape.
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

// Single shaped line of text. Shapes once via FontEngine and exposes
// width, per-glyph layout, cursor positions, bbox and reveal helpers
// without re-shaping the text.
//
// Public API contract:
//   - Fail-soft `shape_glyph_line` is the canonical construction path and
//     returns `std::nullopt` on shaping failure.
//   - `try_shape` is a compatibility adapter over that primitive.
//   - `measure_text_width` and `layout_glyphs` are transitional adapters over
//     the same cached shaping path.
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

    // ── Public fail-soft compatibility factory ──
    // Returns `std::nullopt` on shaping failure instead of throwing.
    // Delegates to the canonical `shape_glyph_line` primitive.
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
    // Called from the private constructor.  The private constructor is
    // noexcept, so a bad_alloc here terminates — same as any noexcept
    // function that allocates.
    void rebuild_prefix_advances();

    // Private constructor used by try_shape factory — populate from a valid
    // GlyphRun directly (does NOT throw).
    ShapedGlyphLine(GlyphRun run, std::string text,
                    f32 tracking, f32 ref_offset_x);

    // Friend declarations allow the canonical free functions to construct
    // the line without exposing another public constructor.
    friend std::optional<ShapedGlyphLine> shape_glyph_line(
        std::string_view text, f32 font_size, const FontSpec& spec,
        f32 tracking, f32 ref_offset_x, FontEngine& engine);
    friend std::optional<ShapedGlyphLine> shape_glyph_line(
        std::string_view text, f32 font_size, const FontSpec& spec,
        f32 tracking, FontEngine& engine);

    // Test/internal support needs access to the cached GlyphRun.
    friend const std::optional<GlyphRun>& test_support::get_raw_run(const ShapedGlyphLine&) noexcept;
};

// shape_glyph_line — canonical fail-soft free-function entry point.
//
// Returns `std::optional<ShapedGlyphLine>` (std::nullopt on engine.shape_text
// failure OR run->glyphs.empty()). The returned line owns the requested
// reference offset, so width/cursor/layout accessors all describe the same
// shaped snapshot without another shaping call.
[[nodiscard]] std::optional<ShapedGlyphLine> shape_glyph_line(
    std::string_view text, f32 font_size, const FontSpec& font,
    f32 tracking, f32 ref_offset_x, FontEngine& engine);

// Compatibility overload: canonical zero-offset shaping.
[[nodiscard]] std::optional<ShapedGlyphLine> shape_glyph_line(
    std::string_view text, f32 font_size, const FontSpec& font,
    f32 tracking, FontEngine& engine);

// measure_text_width — total advance width INCLUDING tracking, matching
// layout_glyphs output.  Returns 0.0f if shaping fails (fail-soft; layout_glyphs
// fail-loud via throw).
//
// Transitional thin-wrapper over shape_glyph_line() — fail-soft width
// measurement with one engine.shape_text call for this returned snapshot.
[[nodiscard]] f32 measure_text_width(const std::string& text, f32 font_size,
                                     const FontSpec& spec, f32 tracking,
                                     FontEngine& engine);

// layout_glyphs — per-glyph positions at FINAL locations (only opacity /
// position animate per frame so the text block stays perfectly stable).
// Throws std::runtime_error on HarfBuzz shaping failure (zero glyphs)
// per AGENTS.md §honesty (fail-loud path = font resolution / AssetResolver
// errors land here).
//
// Transitional thin-wrapper over shape_glyph_line() — fail-loud layout
// materialization from the returned snapshot.
// Byte-equivalence with pre-refactor version preserved via:
//   - shape_glyph_line stores the caller's ref_offset_x in the snapshot
//   - layout_glyphs reads positions directly from that offset-bearing snapshot
[[nodiscard]] std::vector<GlyphPos> layout_glyphs(
    const std::string& text, f32 font_size,
    const FontSpec& spec, f32 tracking,
    f32 ref_offset_x,
    FontEngine& engine);

} // namespace chronon3d::content::text_reveal
