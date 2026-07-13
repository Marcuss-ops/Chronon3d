#include "content/common/text/glyph_layout.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace chronon3d::content::text_reveal {

namespace {

// Build the diagnostic message used when shaping fails.
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

// measure_text_width — total advance width INCLUDING tracking, matching
// layout_glyphs output.  Returns 0.0f on shaping failure (fail-soft at the
// pre-split's choice — the caller is responsible for the engine + spec).
f32 measure_text_width(const std::string& text, f32 font_size,
                       const FontSpec& spec, f32 tracking,
                       FontEngine& engine) {
    try {
        return ShapedGlyphLine{text, font_size, spec, tracking, 0.0f, engine}.width();
    } catch (const std::runtime_error&) {
        return 0.0f;
    }
}

// layout_glyphs — per-glyph positions at FINAL locations.  Fail-loud
// (std::runtime_error) on HarfBuzz shaping failure (zero glyphs) per
// AGENTS.md §honesty.
std::vector<GlyphPos> layout_glyphs(
    const std::string& text, f32 font_size,
    const FontSpec& spec, f32 tracking,
    f32 ref_offset_x,
    FontEngine& engine) {
    return ShapedGlyphLine{text, font_size, spec, tracking, ref_offset_x, engine}.layout();
}

} // namespace chronon3d::content::text_reveal
