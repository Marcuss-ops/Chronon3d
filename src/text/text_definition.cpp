// SPDX-License-Identifier: MIT

// ═══════════════════════════════════════════════════════════════════════════
// text_definition.cpp — canonical TextDefinition lowering
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/text/text_document_builder.hpp>
// TextDocument still owns the runtime defaults representation; this file is
// the single lowering boundary until that runtime representation is folded
// into TextDefinition directly.

namespace chronon3d {

// Helper: build the runtime document defaults from the authoring DTO.
[[nodiscard]] static TextSpec defaults_from_definition(const TextDefinition& def) {
    TextSpec spec;

    spec.content.value      = def.content.value;
    spec.content.pre_shaped = def.content.pre_shaped;
    spec.spans              = def.spans;
    spec.font               = def.style.font;

    spec.layout.box            = def.frame.size;
    spec.layout.anchor         = def.frame.anchor;
    spec.layout.align          = def.frame.align;
    spec.layout.vertical_align = def.frame.vertical_align;
    spec.layout.wrap           = def.frame.wrap;
    spec.layout.overflow       = def.frame.overflow;
    spec.layout.centering_mode = def.frame.centering_mode;
    spec.layout.line_height    = def.frame.line_height;
    spec.layout.tracking       = def.frame.tracking;
    spec.layout.auto_fit       = def.frame.auto_fit;
    spec.layout.min_font_size  = def.frame.min_font_size;
    spec.layout.max_font_size  = def.frame.max_font_size;
    spec.layout.max_lines      = def.frame.max_lines;
    spec.layout.ellipsis       = def.frame.ellipsis;
    spec.layout.paragraph      = def.paragraph;

    spec.appearance.color     = def.style.color;
    spec.appearance.paint     = def.style.paint;
    spec.appearance.shadows   = def.style.shadows;
    spec.appearance.material  = def.style.material;
    spec.appearance.box_style = def.style.box_style;

    spec.placement = {TextPlacementKind::Absolute, {def.frame.placement.offset.x, def.frame.placement.offset.y}};

    return spec;
}

// ── to_text_document — TICKET-SIMPLICITY-TEXTDEFINITION §3 ───────────
//
// Lowers the authoring TextDefinition DTO into the runtime TextDocument
// pipeline model.  Routes via the canonical TextDocumentBuilder to avoid
// introducing a parallel construction path (AGENTS.md §Anti-duplicazione).
TextDocument to_text_document(const TextDefinition& def) {
    TextDocument doc;

    doc.utf8     = def.content.value;
    doc.defaults = defaults_from_definition(def);

    for (const auto& over : def.spans) {
        append_span_override(doc, over, doc.defaults.font);
    }

    if (!doc.utf8.empty()) {
        doc.split_paragraphs();
    }
    return doc;
}

} // namespace chronon3d
