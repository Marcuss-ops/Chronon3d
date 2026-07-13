#include "content/common/text/glyph_layout.hpp"

#include <stdexcept>

namespace chronon3d::content::text_reveal {

// measure_text_width — total advance width INCLUDING tracking, matching
// layout_glyphs output.  Returns 0.0f on shaping failure (fail-soft at the
// pre-split's choice — the caller is responsible for the engine + spec).
f32 measure_text_width(const std::string& text, f32 font_size,
                       const FontSpec& spec, f32 tracking,
                       FontEngine& engine) {
    auto run = engine.shape_text(text, spec, font_size);
    if (!run) return 0.0f;
    const size_t n = run->glyphs.size();
    return run->width + tracking * static_cast<f32>(n > 1 ? n - 1 : 0);
}

// layout_glyphs — per-glyph positions at FINAL locations.  Fail-loud
// (std::runtime_error) on HarfBuzz shaping failure (zero glyphs) per
// AGENTS.md §honesty.
std::vector<GlyphPos> layout_glyphs(
    const std::string& text, f32 font_size,
    const FontSpec& spec, f32 tracking,
    f32 ref_offset_x,
    FontEngine& engine) {
    auto run = engine.shape_text(text, spec, font_size);
    if (!run || run->glyphs.empty()) {
        throw std::runtime_error(
            "layout_glyphs: HarfBuzz shaping produced zero glyphs. "
            "font_path='" + spec.font_path +
            // font_size is f32; cast to int for compact diagnostics (avoid "72.000000")
            "' font_size=" + std::to_string(static_cast<int>(font_size)) +
            // truncate text to 60 chars (intentional, no ellipsis — keeps log lines bounded)
            " text='" + text.substr(0, 60) + "'. "
            "Check that the font file exists and the AssetResolver is mounted.");
    }

    std::vector<GlyphPos> out;
    out.reserve(run->glyphs.size());

    f32 cursor = ref_offset_x;
    for (size_t gi = 0; gi < run->glyphs.size(); ++gi) {
        const auto& g = run->glyphs[gi];
        size_t start = g.cluster;
        size_t end = text.size();
        for (size_t i = 0; i < run->glyphs.size(); ++i) {
            const auto& o = run->glyphs[i];
            if (o.cluster > start) { end = o.cluster; break; }
        }
        std::string ch = text.substr(start, end - start);
        if (ch.empty()) continue;
        out.push_back({ch, cursor + g.advance_x * 0.5f, g.advance_x});
        cursor += g.advance_x + tracking;
    }
    return out;
}

} // namespace chronon3d::content::text_reveal
