#pragma once

// ── glyph_layout — FontEngine shaping + per-glyph measurement ────────────
//
// P1 refactor — extracted from `content/common/text_reveal_helpers.hpp`
// (Step 2 of 4).  Single-responsibility: shaping (HarfBuzz) and
// per-glyph layout (positioning).
//
// Namespace: chronon3d::content::text_reveal (single flat namespace per
// Cat-3 minimal-surface — preserves the 12 existing callers' `using`
// declarations).

#include <chronon3d/core/types/types.hpp>  // f32, Vec2 (canonical SDK types header)
#include <chronon3d/text/font_engine.hpp>  // FontEngine, FontSpec, GlyphRun

#include <optional>
#include <string>
#include <vector>

namespace chronon3d::content::text_reveal {

// Per-glyph position result (centre-X + advance width, post-shaping).
struct GlyphPos {
    std::string ch;
    f32         center_x{0.0f};
    f32         width{0.0f};
};

// Axis-aligned bounding box for a shaped line of text (pixels).
struct GlyphLineBBox {
    f32 x0{0.0f};
    f32 y0{0.0f};
    f32 x1{0.0f};
    f32 y1{0.0f};

    [[nodiscard]] f32 width() const noexcept { return x1 - x0; }
    [[nodiscard]] f32 height() const noexcept { return y1 - y0; }
};

// Single shaped line of text. Shapes once via FontEngine and exposes
// width, per-glyph layout, cursor positions, bbox and reveal helpers
// without re-shaping the text.
class ShapedGlyphLine {
public:
    // Shapes the text. Throws std::runtime_error on shaping failure
    // (zero glyphs / missing font) — same fail-loud contract as layout_glyphs.
    ShapedGlyphLine(const std::string& text, f32 font_size,
                    const FontSpec& spec, f32 tracking,
                    f32 ref_offset_x, FontEngine& engine);

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

private:
    std::string m_text;
    FontSpec    m_spec;
    f32         m_font_size{0.0f};
    f32         m_tracking{0.0f};
    f32         m_ref_offset_x{0.0f};
    std::optional<GlyphRun> m_run;
};

// measure_text_width — total advance width INCLUDING tracking, matching
// layout_glyphs output.  Returns 0.0f if shaping fails (fail-soft at the
// pre-split's choice; layout_glyphs fail-loud via throw).
[[nodiscard]] f32 measure_text_width(const std::string& text, f32 font_size,
                                     const FontSpec& spec, f32 tracking,
                                     FontEngine& engine);

// layout_glyphs — per-glyph positions at FINAL locations (only opacity /
// position animate per frame so the text block stays perfectly stable).
// Throws std::runtime_error on HarfBuzz shaping failure (zero glyphs).
[[nodiscard]] std::vector<GlyphPos> layout_glyphs(
    const std::string& text, f32 font_size,
    const FontSpec& spec, f32 tracking,
    f32 ref_offset_x,
    FontEngine& engine);

} // namespace chronon3d::content::text_reveal
