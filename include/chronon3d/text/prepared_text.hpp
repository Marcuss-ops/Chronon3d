#pragma once

// Canonical lowered text payload for the runtime compiler.
//
// PreparedText owns one compiled representation: TextDocument, TextDefStyle,
// TextFrame, TextShapingOptions, and TextAnimation. `spans` is only the
// authoring-side override input used to rebuild TextDocument::spans at the
// normalization boundary; it is not a second compiled text transport.

#include <chronon3d/text/text_document.hpp>
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/text/text_shaping_options.hpp>

namespace chronon3d {

struct PreparedText {
    TextDocument document;
    TextDefStyle style;
    TextFrame frame;
    TextShapingOptions shaping;
    TextAnimation animation;
    std::vector<TextSpanOverride> spans;
};

/// Lower a canonical TextDefinition into the unified compiled text payload.
[[nodiscard]] PreparedText prepare_text(const TextDefinition& def);

} // namespace chronon3d
