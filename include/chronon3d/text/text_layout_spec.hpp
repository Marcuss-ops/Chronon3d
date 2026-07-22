#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_layout_spec.hpp — Canonical text layout specification
//
// Moved from <chronon3d/scene/builders/params/text_params.hpp> to the text
// module.  These are pre-layout (reflow) properties: box/anchor/align/wrap/
// overflow/line_height/tracking/auto_fit/min/max_font_size/max_lines/ellipsis/
// paragraph/centering_mode/vertical_align.  The runtime animated variants
// (TextAnimator::*) override these via PostLayout on a per-glyph basis.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/scene/model/shape/shape.hpp>  // TextAnchor, TextAlign, VerticalAlign, TextWrap, TextOverflow, TextCenteringMode
#include <chronon3d/text/paragraph_style.hpp>      // ParagraphStyle

#include <cstdint>
#include <string>

namespace chronon3d {

struct TextLayoutSpec {
    // Per-field phase tags (TICKET-TEXT-PROPERTY-PHASES — PreLayout = reflow): box/anchor/align/wrap/overflow/line_height/tracking/auto_fit/min/max_font_size/max_lines/ellipsis/paragraph/centering_mode/vertical_align. The runtime animated variants (TextAnimator::*) override these via PostLayout on a per-glyph basis.
    Vec2 box{900.0f, 160.0f};
    TextAnchor anchor{TextAnchor::Center};
    TextCenteringMode centering_mode{TextCenteringMode::LayoutBox};
    TextAlign align{TextAlign::Center};
    VerticalAlign vertical_align{VerticalAlign::Middle};
    TextWrap wrap{TextWrap::Word};
    TextOverflow overflow{TextOverflow::Clip};
    f32 line_height{1.2f};
    f32 tracking{0.0f};
    bool auto_fit{false};
    f32 min_font_size{12.0f};
    f32 max_font_size{160.0f};
    i32 max_lines{0};
    bool ellipsis{false};
    ParagraphStyle paragraph{};

    /// OpenType / HarfBuzz shaping features (e.g. "kern=1,liga=1,calt=0").
    /// Empty default = use the font's natural feature set (HarfBuzz
    /// applies its implicit defaults: kern=1, liga=1, calt=1, ...).
    std::string features{};
};

} // namespace chronon3d
