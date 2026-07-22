// SPDX-License-Identifier: MIT

// ═══════════════════════════════════════════════════════════════════════════
// prepared_text.cpp — X2 canonical text lowering
//
// Implements prepare_text(const TextDefinition&) -> PreparedText.
// This is the single canonical lowering from the authoring DTO to the
// runtime payload consumed by compile_text_layout().
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/prepared_text.hpp>
#include <chronon3d/text/text_document_builder.hpp>

namespace chronon3d {

PreparedText prepare_text(const TextDefinition& def) {
    PreparedText pt;

    // 1. Lower the document model (content, spans, paragraphs).
    //    to_text_document() is kept as an internal helper; it still uses
    //    TextSpec defaults inside TextDocument for the runtime span resolver.
    pt.document = to_text_document(def);

    // 2. Carry the canonical style/frame/shaping/animation sub-structs
    //    directly, so the compiler never needs to round-trip through
    //    TextSpec/TextRunSpec.
    pt.style   = def.style;
    pt.frame   = def.frame;
    pt.shaping = TextShapingOptions{
        .direction = def.animation.direction,
        .language  = def.animation.language,
        .script    = def.animation.script,
        .open_type_features = {},  // populated below from legacy layout features
    };
    pt.animation = def.animation;

    // 3. OpenType shaping features are not yet part of TextDefinition;
    //    they will be carried by TextShapingOptions once TextDefinition
    //    owns a shaping field (X3).  For now the canonical compiler
    //    receives an empty feature string through PreparedText.
    pt.shaping.open_type_features.clear();

    return pt;
}

} // namespace chronon3d
