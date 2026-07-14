#include "content/common/text/typewriter_block.hpp"

#include "content/common/text/glyph_layout.hpp"  // shape_glyph_line (Post-Point-8 single-shape)
#include "content/common/text/text_reveal.hpp"   // build_text_reveal_line, TextRevealDescriptor, font_regular

#include <algorithm>  // std::max
#include <cstddef>    // std::size_t
#include <stdexcept>  // std::runtime_error

namespace chronon3d::content::text_reveal {

// build_2line_typewriter — implementation
//
// refactor(typewriter): TwoLineTypewriterSpec drives Anim* (Point 11).
//
// Post-Point-11 contract:
//   1. Single-shape both lines (Point 8 single-shape efficiency —
//      shape_glyph_line() returns std::optional<ShapedGlyphLine> with
//      the engine.shape_text results cached in m_run; measure_text_width
//      + layout_glyphs share the same instance when invoked consecutively
//      via the ShapedGlyphLine class API).
//   2. Returns TypewriterBlockResult with pre-computed geometry for
//      downstream consumers (e.g., add_cursor on AnimTypewriterCursor).
//
// Fail-loud: throws std::runtime_error if either line fails to shape
// (font resolution / AssetResolver-mount failures land here per ADR-020
// §fail-loud path).
TypewriterBlockResult build_2line_typewriter(
    SceneBuilder& s,
    const TwoLineTypewriterSpec& spec)
{
    auto font = font_regular();
    auto& engine = *s.font_engine();

    // Single-shape both lines (Point 8 — shape_glyph_line delegates to
    // ShapedGlyphLine::try_shape factory, fail-soft contract).
    auto shape1_opt = shape_glyph_line(
        spec.first.text, spec.first.font_size, font, spec.tracking, engine);
    auto shape2_opt = shape_glyph_line(
        spec.second.text, spec.second.font_size, font, spec.tracking, engine);

    if (!shape1_opt || !shape2_opt) {
        throw std::runtime_error(
            std::string{"build_2line_typewriter: HarfBuzz shaping produced "
                        "zero glyphs for one or both text_reveal lines. "} +
            "font_path='" + font.font_path + "'");
    }
    const auto& shape1 = *shape1_opt;
    const auto& shape2 = *shape2_opt;

    f32 max_w = std::max(shape1.width(), shape2.width());
    f32 ref_x = -max_w * 0.5f;

    // build_text_reveal_line production defaults (verbatim from TextRevealDescriptor).
    const f32 stagger  = 2.0f;
    const f32 duration = 8.0f;

    auto emit = [&](const TextLineSpec& line, f32 y_offset, f32 start_delay,
                    const char* prefix, const ShapedGlyphLine& pre_shaped) {
        TextRevealDescriptor d{
            .text = line.text, .font_size = line.font_size, .font_spec = font,
            .tracking = spec.tracking, .ref_offset_x = ref_x,
            .base_pos = {0.0f, spec.base_y + y_offset, 0.0f},
            .start_delay = start_delay, .duration = duration, .stagger = stagger,
            .slide_up = spec.slide_up, .pin_to_center = true,
            .color = spec.text_color, .add_shadow = true, .shadow_color = spec.shadow_color,
            .glow_intensity = spec.glow_intensity,
            .layer_prefix = prefix
        };
        // P0-2 fix(perf/text): pass already-shaped ShapedGlyphLine so the
        // reveal layer emitter doesn't re-shape the same text (single-shape
        // contract: shape1, shape2 are computed once above to derive ref_x
        // and per-line widths; build_text_reveal_line 3-arg uses them as-is).
        build_text_reveal_line(s, d, pre_shaped);
    };

    // line 1 (y_offset = -line_spacing/2, starts at frame 0)
    emit(spec.first,  -spec.line_spacing * 0.5f, 0.0f,             "ch_0", shape1);
    // line 2 (y_offset = +line_spacing/2, starts at second_delay)
    emit(spec.second,  spec.line_spacing * 0.5f, spec.second_delay, "ch_1", shape2);

    // Per TypewriterBlockResult field formulas:
    //   - second_line_right_edge = ref_x + shape2.width() (width includes tracking)
    //   - second_line_reveal_end = Frame{second_delay + (n_glyphs-1)*2 + 8}
    //     where 2.0f = stagger + 8.0f = duration (production defaults).
    const std::size_t n_glyphs_2 = shape2.layout().size();
    // 0-glyph defensive path (shape already fail-loud-checked above; this
    // is a no-op safety net for downstream n_glyphs_2 introspection).
    const f32 reveal_end_f = (n_glyphs_2 > 0)
        ? spec.second_delay + static_cast<f32>(n_glyphs_2 - 1) * stagger + duration
        : spec.second_delay + duration;

    return TypewriterBlockResult{
        .second_line_right_edge = ref_x + shape2.width(),
        .second_line_reveal_end = Frame{static_cast<Frame>(reveal_end_f)}
    };
}

} // namespace chronon3d::content::text_reveal
