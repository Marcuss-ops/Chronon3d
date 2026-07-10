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
#include <chronon3d/text/text_document.hpp>   // TextDocument — Phase B lowering target
#include <utility>                           // std::move — span lowering
// builder_params.hpp is already pulled in transitively via text_definition.hpp
// (which includes it for the canonical TextContent + TextSpec + TextRunSpec).

namespace chronon3d {

TextDefinition from_text_spec(const TextSpec& spec) {
    TextDefinition def;

    // ── content (canonical TextContent from builder_params.hpp) ───────
    def.content = spec.content;

    // ── style (TextDefStyle — font, color, paint, shadows, material) ──
    def.style.font     = spec.font;
    def.style.color    = spec.appearance.color;
    def.style.paint    = spec.appearance.paint;
    def.style.shadows  = spec.appearance.shadows;
    def.style.material  = spec.appearance.material;
    def.style.box_style = spec.appearance.box_style;

    // ── frame ──────────────────────────────────────────────────────────
    def.frame.size          = spec.layout.box;
    def.frame.position      = spec.position;
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

    // ── animation (Phase A.3 placeholder — stored in TextAnimation) ────
    // Forward-point: when TextAnimation is filled in Phase A.3,
    // spec.animators and spec.selectors will be mapped here.
    // All TextRunSpec-only fields are intentionally not yet mapped.
    (void)spec.animators;
    (void)spec.selectors;
    (void)spec.direction;
    (void)spec.language;
    (void)spec.script;
    (void)spec.cache_layout;

    return def;
}

TextSpec from_text_definition(const TextDefinition& def) {
    TextSpec spec;

    // ── content ────────────────────────────────────────────────────────
    spec.content.value      = def.content.value;
    spec.content.pre_shaped = def.content.pre_shaped;

    // ── font ───────────────────────────────────────────────────────────
    spec.font = def.style.font;

    // ── layout ─────────────────────────────────────────────────────────
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

    // ── appearance ─────────────────────────────────────────────────────
    spec.appearance.color     = def.style.color;
    spec.appearance.paint     = def.style.paint;
    spec.appearance.shadows   = def.style.shadows;
    spec.appearance.material  = def.style.material;
    spec.appearance.box_style = def.style.box_style;

    // ── position ───────────────────────────────────────────────────────
    spec.position = def.frame.position;

    return spec;
}

// ═══════════════════════════════════════════════════════════════════════════
// Phase B — to_text_document(): lower TextDefinition → TextDocument
//
// This is the convergence point: all authoring APIs (centered_text(),
// glow_text(), typewriter_text(), text_run()) produce TextDefinition,
// which lowers into a TextDocument for compile_text_layout().
//
// Maps:
//   content.value       → doc.utf8
//   style+frame+paragraph → doc.defaults (via from_text_definition)
//   spans[]             → doc.spans[] (TextSpanOverride → TextStyleSpan)
//   paragraph           → doc.split_paragraphs()
// ═══════════════════════════════════════════════════════════════════════════

TextDocument to_text_document(const TextDefinition& def) {
    TextDocument doc;

    // ── Source text ────────────────────────────────────────────────────
    doc.utf8 = def.content.value;

    // ── Defaults (font, layout, appearance, position) ──────────────────
    // Reuse the reverse adapter to produce the full TextSpec, then assign
    // it as the document's defaults.  This guarantees that every field
    // in TextDefinition (style, frame, paragraph) is faithfully mapped
    // to the pipeline-visible TextSpec without duplication.
    doc.defaults = from_text_definition(def);

    // ── Span overrides (TextSpanOverride → TextStyleSpan) ──────────────
    //
    // TextSpanOverride is the authoring-level type (text_definition.hpp);
    // TextStyleSpan is the runtime-pipeline type (text_document.hpp).
    // Both express optional overrides on a byte range.  Map field-by-field.
    doc.spans.reserve(def.spans.size());
    for (const auto& src : def.spans) {
        TextStyleSpan dst;
        dst.byte_start = src.byte_start;
        dst.byte_end   = src.byte_end;

        // Font override → optional<FontSpec>
        if (src.font.has_value()) {
            dst.font = src.font;
        }

        // Color override → optional<TextAppearanceSpec>
        // TextSpanOverride stores color standalone; TextStyleSpan wraps it
        // in an optional<TextAppearanceSpec> (only color is set, other
        // appearance fields inherit from defaults).
        if (src.color.has_value()) {
            TextAppearanceSpec appearance;
            appearance.color = *src.color;
            dst.appearance = appearance;
        }

        // Font size override → optional<f32> font_size_multiplier
        // TextSpanOverride stores absolute font_size; TextStyleSpan uses
        // a multiplier relative to the paragraph default.  When the
        // default font size is non-zero, compute the ratio; otherwise
        // pass 1.0 (the resolver will handle it).
        //
        // IMPORTANT: when src.font already carries a font_size > 0, the
        // absolute size is conveyed via the FontSpec override.  We skip
        // the multiplier in that case to avoid double-application (the
        // resolver would otherwise multiply font->font_size * multiplier).
        if (src.font_size.has_value() && *src.font_size > 0.0f) {
            const bool font_override_has_size =
                src.font.has_value() && src.font->font_size > 0.0f;
            if (!font_override_has_size) {
                const f32 default_size = def.style.font.font_size;
                if (default_size > 0.0f) {
                    dst.font_size_multiplier = *src.font_size / default_size;
                } else {
                    // No default to ratio against — store as 1.0 and let
                    // the resolver use the absolute value from dst.font.
                    dst.font_size_multiplier = 1.0f;
                }
            }
        }

        doc.spans.push_back(std::move(dst));
    }

    // ── Paragraph splitting ────────────────────────────────────────────
    // Auto-split on hard breaks (\n, U+2029).  Uses the paragraph style
    // from the TextDefinition as the default paragraph style.
    if (!doc.utf8.empty()) {
        doc.split_paragraphs(def.paragraph);
    }

    return doc;
}

} // namespace chronon3d
