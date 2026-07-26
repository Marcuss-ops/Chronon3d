#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_defaults.hpp — Runtime defaults for a TextDocument.
//
// TextDefaults contains the fallback style/layout substrate used by the
// document compiler. Authoring code should prefer TextDefinition.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/font_engine.hpp>            // FontSpec
#include <chronon3d/text/text_appearance_spec.hpp>   // TextAppearanceSpec
#include <chronon3d/text/text_content.hpp>             // TextContent
#include <chronon3d/text/text_layout_spec.hpp>         // TextLayoutSpec
#include <chronon3d/text/text_placement.hpp>           // TextPlacement
#include <chronon3d/text/text_span_override.hpp>        // TextSpanOverride

#include <vector>

namespace chronon3d {

struct TextDefaults {
    // Per-field phase tags (TICKET-TEXT-PROPERTY-PHASES):
    //   .content     → PreShaping substrate (value/pre_shaped are inputs to the PreShaping evaluator)
    //   .placement   → PreLayout (reflow; affects paragraph composition)
    //   .font        → PreLayout (reflow; font size feeds HarfBuzz shaping metrics)
    //   .layout      → PreLayout (reflow; see TextLayoutSpec table above)
    //   .appearance  → PostLayout (visual-only; see TextAppearanceSpec table above)
    //   .spans[]     → mixed phase; per-span font is PreLayout, per-span color is PostLayout.
    TextContent content;
    TextPlacement placement{};
    FontSpec font;
    TextLayoutSpec layout;
    TextAppearanceSpec appearance;

    /// Per-range style overrides for rich text.  Each override covers
    /// the UTF-8 byte range [byte_start, byte_end) inside content.value.
    /// Empty vector means "single-style text".
    std::vector<TextSpanOverride> spans{};
};

} // namespace chronon3d
