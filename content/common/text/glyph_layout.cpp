#include "content/common/text/glyph_layout.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace chronon3d::content::text_reveal {

namespace {

// Build the diagnostic message used when shaping fails (layout_glyphs
// fail-loud path).  The message preserves `font_path` + `font_size` +
// 60-char text snippet + AssetResolver remediation hint (per pre-refactor
// content + ADR-020 §fail-loud path).
[[nodiscard]] std::string make_shape_error_message(
    const std::string& text,
    const FontSpec& spec,
    f32 font_size)
{
    std::string msg = "ShapedGlyphLine/layout_glyphs: HarfBuzz shaping produced zero glyphs. ";
    msg += "font_path='" + spec.font_path + "' ";
    // font_size is f32; cast to int for compact diagnostics (avoid "72.000000")
    msg += "font_size=" + std::to_string(static_cast<int>(font_size)) + " ";
    // truncate text to 60 chars (intentional, no ellipsis — keeps log lines bounded)
    msg += "text='" + text.substr(0, 60) + "'. ";
    msg += "Check that the font file exists and the AssetResolver is mounted.";
    return msg;
}

} // anonymous namespace

// ── ShapedGlyphLine fail-loud primary ctor (public) ────────────────────
//
// Throws on shaping failure (zero glyphs / missing font).  Same
// fail-loud contract as `layout_glyphs` — preserved verbatim for
// backward-compat with cinematic showcase callers that REQUIRE shape
// result (e.g., `abyss_freefall_stagger.cpp`).
//
// Per AGENTS.md `### C++ default-arg uniqueness per TU`, no default
// args here — declared ONLY in the .hpp.
ShapedGlyphLine::ShapedGlyphLine(const std::string& text, f32 font_size,
                                 const FontSpec& spec, f32 tracking,
                                 f32 ref_offset_x, FontEngine& engine)
    : m_text(text), m_spec(spec), m_font_size(font_size),
      m_tracking(tracking), m_ref_offset_x(ref_offset_x)
{
    m_run = engine.shape_text(text, spec, font_size);
    if (!m_run || m_run->glyphs.empty()) {
        throw std::runtime_error(make_shape_error_message(text, spec, font_size));
    }
}

// ── ShapedGlyphLine private ctor (used by try_shape factory) ────────────
//
// Private ctor that populates fields from a valid GlyphRun directly —
// does NOT throw.  Called by `try_shape` static factory only.
ShapedGlyphLine::ShapedGlyphLine(GlyphRun run, std::string text, FontSpec spec,
                                 f32 font_size, f32 tracking, f32 ref_offset_x) noexcept
    : m_text(std::move(text)), m_spec(std::move(spec)),
      m_font_size(font_size), m_tracking(tracking), m_ref_offset_x(ref_offset_x),
      m_run(std::move(run))
{}

// ── ShapedGlyphLine read-only accessors (unchanged from upstream) ────────
//
// These methods read from `m_run` + `m_tracking` + `m_ref_offset_x`
// (cached state populated by ctor / try_shape factory).  No re-shape
// calls — single engine.shape_text invocation per ShapedGlyphLine
// instance (Point 8 single-shape efficiency).
f32 ShapedGlyphLine::width() const noexcept {
    if (!m_run) return 0.0f;
    const size_t n = m_run->glyphs.size();
    return m_run->width + m_tracking * static_cast<f32>(n > 1 ? n - 1 : 0);
}

std::vector<GlyphPos> ShapedGlyphLine::layout() const {
    std::vector<GlyphPos> out;
    if (!m_run) return out;

    out.reserve(m_run->glyphs.size());

    f32 cursor = m_ref_offset_x;
    for (size_t gi = 0; gi < m_run->glyphs.size(); ++gi) {
        const auto& g = m_run->glyphs[gi];
        size_t start = g.cluster;
        size_t end = m_text.size();
        // Inner O(n²) cluster-boundary scan; preserved verbatim from
        // upstream.  Future optimization opportunity: replace with single-pass
        // O(n) groupBy (Point 9 perf) in a follow-up refactor — orthogonal
        // to the current Point 8 single-shape efficiency.
        for (size_t i = 0; i < m_run->glyphs.size(); ++i) {
            const auto& o = m_run->glyphs[i];
            if (o.cluster > start) { end = o.cluster; break; }
        }
        std::string ch = m_text.substr(start, end - start);
        if (ch.empty()) continue;
        out.push_back({ch, cursor + g.advance_x * 0.5f, g.advance_x});
        cursor += g.advance_x + m_tracking;
    }
    return out;
}

f32 ShapedGlyphLine::cursor_position(size_t index) const noexcept {
    if (!m_run) return m_ref_offset_x;
    f32 x = m_ref_offset_x;
    const size_t n = m_run->glyphs.size();
    const size_t limit = std::min(index, n);
    for (size_t i = 0; i < limit; ++i) {
        x += m_run->glyphs[i].advance_x + m_tracking;
    }
    return x;
}

f32 ShapedGlyphLine::cursor_at_end() const noexcept {
    if (!m_run) return m_ref_offset_x;
    return cursor_position(m_run->glyphs.size());
}

GlyphLineBBox ShapedGlyphLine::bbox() const noexcept {
    GlyphLineBBox box;
    box.x0 = m_ref_offset_x;
    box.x1 = m_ref_offset_x;
    if (!m_run || m_run->glyphs.empty()) return box;

    f32 min_x = std::numeric_limits<f32>::max();
    f32 min_y = std::numeric_limits<f32>::max();
    f32 max_x = std::numeric_limits<f32>::lowest();
    f32 max_y = std::numeric_limits<f32>::lowest();
    // Glyph bboxes use a y-up convention where bbox_y0 is the top and
    // bbox_y1 is the bottom, so y0 can be greater than y1. Normalize the
    // final box so callers can rely on x0<=x1 and y0<=y1.

    f32 cursor = m_ref_offset_x;
    for (const auto& g : m_run->glyphs) {
        const f32 gx0 = cursor + g.bbox_x0;
        const f32 gy0 = g.bbox_y0;
        const f32 gx1 = cursor + g.bbox_x1;
        const f32 gy1 = g.bbox_y1;

        min_x = std::min(min_x, gx0);
        min_y = std::min(min_y, gy0);
        max_x = std::max(max_x, gx1);
        max_y = std::max(max_y, gy1);

        cursor += g.advance_x + m_tracking;
    }

    box.x0 = min_x;
    box.y0 = std::min(min_y, max_y);
    box.x1 = max_x;
    box.y1 = std::max(min_y, max_y);
    return box;
}

size_t ShapedGlyphLine::reveal_count(f32 progress) const noexcept {
    if (!m_run) return 0;
    if (progress <= 0.0f) return 0;
    if (progress >= 1.0f) return m_run->glyphs.size();
    return static_cast<size_t>(static_cast<f32>(m_run->glyphs.size()) * progress);
}

// ── try_shape static factory (Point 8 fail-soft entry point) ────────────
//
// Returns `std::optional<ShapedGlyphLine>`:
//   - engine.shape_text returned std::nullopt           → return std::nullopt
//   - run->glyphs.empty()                                → return std::nullopt
//   - otherwise                                          → populated instance
//
// Does NOT throw.  Forwarded by `shape_glyph_line` free function below.
std::optional<ShapedGlyphLine> ShapedGlyphLine::try_shape(
    std::string_view text, f32 font_size, const FontSpec& spec,
    f32 tracking, f32 ref_offset_x, FontEngine& engine) noexcept
{
    auto run_opt = engine.shape_text(std::string(text), spec, font_size);
    if (!run_opt || run_opt->glyphs.empty()) return std::nullopt;
    return ShapedGlyphLine(
        std::move(*run_opt),
        std::string(text),
        spec,
        font_size,
        tracking,
        ref_offset_x);
}

// ── shape_glyph_line free function (Point 8 single-shape entry point) ─
//
// 1-line delegation to `ShapedGlyphLine::try_shape` with
// `ref_offset_x = 0.0f` (the caller applies offset as a post-step in
// `layout_glyphs`).  Fail-soft contract: `std::nullopt` on shaping
// failure.
[[nodiscard]] std::optional<ShapedGlyphLine> shape_glyph_line(
    std::string_view text, f32 font_size, const FontSpec& font,
    f32 tracking, FontEngine& engine) noexcept
{
    return ShapedGlyphLine::try_shape(text, font_size, font, tracking,
                                      /*ref_offset_x=*/0.0f, engine);
}

// ── measure_text_width — thin-wrapper over shape_glyph_line (fail-soft) ──
//
// Returns 0.0f if shape_glyph_line returns std::nullopt (fail-soft —
// pre-refactor semantics preserved verbatim).  Single engine.shape_text
// call (shared with layout_glyphs when both are invoked consecutively).
f32 measure_text_width(const std::string& text, f32 font_size,
                       const FontSpec& spec, f32 tracking,
                       FontEngine& engine) {
    auto shaped = shape_glyph_line(text, font_size, spec, tracking, engine);
    if (!shaped) return 0.0f;
    return shaped->width();
}

// ── layout_glyphs — thin-wrapper over shape_glyph_line (fail-loud) ──
//
// Throws std::runtime_error(make_shape_error_message(...)) on shaping
// failure (fail-loud per AGENTS.md §honesty + ADR-020 §fail-loud path).
// Post-step: applies `ref_offset_x` as a constant offset to each
// glyph's `center_x` (byte-equivalent to pre-refactor cursor-init-at-
// ref_offset_x math).
std::vector<GlyphPos> layout_glyphs(
    const std::string& text, f32 font_size,
    const FontSpec& spec, f32 tracking,
    f32 ref_offset_x,
    FontEngine& engine)
{
    auto shaped = shape_glyph_line(text, font_size, spec, tracking, engine);
    if (!shaped) {
        throw std::runtime_error(make_shape_error_message(text, spec, font_size));
    }
    auto positions = shaped->layout();
    // Pre-refactor byte-equivalence: the cursor inside ShapedGlyphLine's
    // `layout()` starts at `m_ref_offset_x`; we applied 0.0f via
    // shape_glyph_line, so positions are RELATIVE to 0.  Adding the
    // caller-provided `ref_offset_x` as a constant offset is
    // mathematically equivalent to starting the cursor at `ref_offset_x`.
    for (auto& p : positions) {
        p.center_x += ref_offset_x;
    }
    return positions;
}

} // namespace chronon3d::content::text_reveal
