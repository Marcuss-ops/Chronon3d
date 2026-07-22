// SPDX-License-Identifier: MIT

// ═══════════════════════════════════════════════════════════════════════════
// text_definition.cpp — F2.A adapter implementations
//
// from_text_spec() and from_text_run_spec() adapt the existing TextSpec /
// TextRunSpec (builder_params.hpp) into the canonical TextDefinition DTO.
//
// These adapters bridge the F2.A (canonical struct) and F2.C (full adapter
// phase) milestones.  During F2.C, text()/text_run()/centered_text() will
// call these adapters to produce a single TextDefinition.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/text/text_document_builder.hpp>
// builder_params.hpp is already pulled in transitively via text_definition.hpp
// (which includes it for the canonical TextContent + TextSpec + TextRunSpec).

namespace chronon3d {

TextDefinition from_text_spec(const TextSpec& spec) {
    TextDefinition def;

    // ── content (canonical TextContent from builder_params.hpp) ───────
    def.content = spec.content;

    // ── spans (authoring-level overrides) ─────────────────────────────
    def.spans = spec.spans;

    // ── style (TextDefStyle — font, color, paint, shadows, material) ──
    def.style.font     = spec.font;
    def.style.color    = spec.appearance.color;
    def.style.paint    = spec.appearance.paint;
    def.style.shadows  = spec.appearance.shadows;
    def.style.material  = spec.appearance.material;
    def.style.box_style = spec.appearance.box_style;

    // ── frame ──────────────────────────────────────────────────────────
    def.frame.size          = spec.layout.box;
    // TICKET-TEXT-LEGACY-POSITION-ROT (sub-area ii, read direction): legacy
    // `.position` Vec3 was the post-anchor resolved coords; faithfully map to
    // `{TextPlacementKind::Absolute, {Vec2}}` which is byte-equivalent (Z=0).
    def.frame.placement     = {TextPlacementKind::Absolute, {spec.placement.offset.x, spec.placement.offset.y}};
    def.frame.anchor        = spec.layout.anchor;
    def.frame.align         = spec.layout.align;
    def.frame.vertical_align = spec.layout.vertical_align;
    def.frame.wrap          = spec.layout.wrap;
    def.frame.overflow      = spec.layout.overflow;
    def.frame.centering_mode = spec.layout.centering_mode;
    def.frame.line_height   = spec.layout.line_height;
    def.frame.tracking      = spec.layout.tracking;
    def.frame.auto_fit      = spec.layout.auto_fit;
    def.frame.min_font_size = spec.layout.min_font_size;
    def.frame.max_font_size = spec.layout.max_font_size;
    def.frame.max_lines     = spec.layout.max_lines;
    def.frame.ellipsis      = spec.layout.ellipsis;

    // ── paragraph ──────────────────────────────────────────────────────
    def.paragraph = spec.layout.paragraph;

    return def;
}

TextDefinition from_text_run_spec(const TextRunSpec& spec) {
    TextDefinition def = from_text_spec(spec.text);

    // ── animation (Phase A.3 — stored in TextAnimation) ─────────────────
    def.animation.direction    = spec.direction;
    def.animation.language     = spec.language;
    def.animation.script       = spec.script;
    def.animation.cache_layout = spec.cache_layout;
    def.animation.animators    = spec.animators;
    def.animation.selectors    = spec.selectors;

    return def;
}

// Helper: build the legacy TextSpec defaults that TextDocument still needs
// until TextDocument::defaults migrates to TextDefStyle (X3).
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
