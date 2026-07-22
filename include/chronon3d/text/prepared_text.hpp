#pragma once

// ══════════════════════════════════════════════════════════════════════════
// prepared_text.hpp — Canonical lowered text payload for the runtime compiler
//
// PreparedText is the unique result of `prepare_text(const TextDefinition&)`.
// It bundles the runtime models that the text compiler consumes:
//   - TextDocument   (content + spans + paragraphs)
//   - TextDefStyle   (font, color, paint, shadows, material, box style)
//   - TextFrame      (layout box, placement, anchor, alignment, wrap, overflow)
//   - TextShapingOptions (direction, language, script, OpenType features)
//   - TextAnimation  (animators, selectors, timing)
//
// X2 canonization step: the compiler no longer reverse-adapts the canonical
// TextDefinition back into TextSpec/TextRunSpec/TextLayoutSpec; it reads
// the canonical sub-structs directly from PreparedText.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_document.hpp>
#include <chronon3d/text/text_definition.hpp>   // TextDefStyle, TextFrame, TextAnimation
#include <chronon3d/text/text_shaping_options.hpp>

namespace chronon3d {

struct PreparedText {
    TextDocument document;
    TextDefStyle style;
    TextFrame frame;
    TextShapingOptions shaping;
    TextAnimation animation;
};

/// Lower a canonical TextDefinition into the runtime PreparedText payload.
[[nodiscard]] PreparedText prepare_text(const TextDefinition& def);

} // namespace chronon3d
