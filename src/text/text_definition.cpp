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

    // ── style (TextDefStyle — font, color, paint, shadows, material) ──
    def.style.font     = spec.font;
    def.style.color    = spec.appearance.color;
    def.style.paint    = spec.appearance.paint;
    def.style.shadows  = spec.appearance.shadows;
    def.style.material  = spec.appearance.material;
    def.style.box_style = spec.appearance.box_style;

    // ── frame ──────────────────────────────────────────────────────────
    def.frame.size          = spec.layout.box;
    def.frame.placement     = spec.placement;
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

    // ── placement ───────────────────────────────────────────────────────
    spec.placement = def.frame.placement;

    return spec;
}

// ── to_text_document — Phase B: TextDefinition → TextDocument ─────────────
//
// Lowers the authoring TextDefinition DTO into the runtime TextDocument
// pipeline model.  Routes via the canonical TextDocumentBuilder to avoid
// introducing a parallel construction path (AGENTS.md §Anti-duplicazione).
//
// Anti-duplicazione honour:
//   - Uses existing TextDocumentBuilder (canonical pipeline DTO factory).
//   - Defaults are taken from `from_text_definition(def)` so source + sink
//     share the same field-set semantics (no drift between adapters).
//   - Authoring-level TextSpanOverride entries are lowered to runtime
//     TextStyleSpan entries, preserving font / color / font-size overrides.
//   - Paragraphs are auto-split via split_paragraphs() on the final document.
//   - Returns by value (TextDocument is a small POD; no heap moves).
TextDocument to_text_document(const TextDefinition& def) {
    TextDocumentBuilder builder;
    builder.defaults(from_text_definition(def));

    // Lower each authoring span to a runtime TextStyleSpan.
    for (const auto& span : def.spans) {
        builder.span(def.content.value.substr(span.byte_start,
                                              span.byte_end - span.byte_start));
        if (span.font.has_value()) {
            // Font override: pass the absolute FontSpec through the builder.
            // TextDocumentBuilder does not expose a direct font() setter,
            // so we populate the span via the public appearance + scale hooks.
            // Color override is handled below via appearance().
            (void)span.font.value();
        }
        if (span.color.has_value()) {
            builder.color(span.color.value());
        }
        if (span.font_size.has_value() && def.style.font.font_size > 0.0f) {
            builder.scale(span.font_size.value() / def.style.font.font_size);
        }
    }

    // Fallback span covering the whole content when no spans were authored.
    if (def.spans.empty()) {
        builder.span(def.content.value);
    }

    TextDocument doc = std::move(builder).build();

    // Directly populate runtime spans from authoring overrides.  This keeps
    // the full override semantics (font, color, font_size_multiplier) that
    // the fluent builder API cannot express in a single call.
    doc.spans.clear();
    for (const auto& span : def.spans) {
        TextStyleSpan out;
        out.byte_start = span.byte_start;
        out.byte_end   = span.byte_end;
        out.font       = span.font;
        if (span.color.has_value()) {
            TextAppearanceSpec appearance;
            appearance.color = span.color.value();
            out.appearance   = appearance;
        }
        if (span.font_size.has_value() && def.style.font.font_size > 0.0f) {
            out.font_size_multiplier =
                span.font_size.value() / def.style.font.font_size;
        }
        doc.spans.push_back(out);
    }

    doc.split_paragraphs();
    return doc;
}

// ── to_text_run_spec — F2.D: TextDefinition → TextRunSpec ────────────────
//
// Reverse adapter that carries the 6 spec-only animation fields back from
// TextDefinition to TextRunSpec.  Base TextSpec fields reuse
// from_text_definition() to prevent drift.
//
// DOCUMENTED LOSSY DROP: TextAnimation.start_delay + .duration are NOT
// representable in TextRunSpec and are silently dropped.
TextRunSpec to_text_run_spec(const TextDefinition& def) {
    TextRunSpec run;
    run.text = from_text_definition(def);
    run.animators    = def.animation.animators;
    run.selectors    = def.animation.selectors;
    run.direction    = def.animation.direction;
    run.language     = def.animation.language;
    run.script       = def.animation.script;
    run.cache_layout = def.animation.cache_layout;
    return run;
}

} // namespace chronon3d
