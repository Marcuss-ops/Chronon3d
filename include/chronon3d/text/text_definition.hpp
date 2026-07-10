#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_definition.hpp — Phase A.1 canonical text authoring DTO
//
// TextDefinition is the Phase A.1 authoring payload (Data Transfer Object)
// for the text compilation pipeline.  It mirrors the external `.chronon`
// file schema and is the SINGLE source of truth for text authoring.
//
// LIFECYCLE:
//
//   Phase A.1 (this file)  — create the DTO + 5 placeholder struct types
//   Phase A.2              — fill in TextContent + TextStyle + TextFrame
//   Phase A.3              — fill in TextEffects + TextAnimation
//   Phase B                — TextDocumentBuilder::build(const TextDefinition&)
//                            lowers this DTO into the canonical TextDocument
//                            pipeline model (see text_document.hpp)
//
// DO NOT USE TextDefinition IN THE RUNTIME PIPELINE.
// The runtime pipeline consumes TextDocument (text_document.hpp), not this
// authoring DTO.  TextDefinition is purely for authoring / deserialization.
//
// Per AGENTS.md "Non duplicare registry, resolver, sampler, cache, service
// locator o checklist": TextDefinition complements (does not duplicate)
// TextDocument.  TextDocument is the canonical pipeline model; this DTO is
// the authoring surface that lowers into it.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/paragraph_style.hpp>

namespace chronon3d {

// ── Phase A.1 placeholder types (filled in by A.2/A.3) ───────────────────
//
// These 5 types are empty placeholders.  They are introduced now so that
// TextDefinition can be defined with the exact 6-field schema required by
// the text V1 cleanup plan.  Phase A.2 will populate TextContent, TextStyle,
// TextFrame; Phase A.3 will populate TextEffects, TextAnimation.

/// Phase A.2: actual content type (raw text + metadata).
struct TextContent {};

/// Phase A.2: actual style type (font, size, appearance).
struct TextStyle {};

/// Phase A.2: actual frame type (layout box: position + size).
struct TextFrame {};

/// Phase A.3: actual effects type (glow, shadow, outline, etc.).
struct TextEffects {};

/// Phase A.3: actual animation type (typewriter, reveal, stagger, etc.).
struct TextAnimation {};

// ── TextDefinition — Phase A.1 canonical authoring DTO ──────────────────

struct TextDefinition {
    TextContent     content;     ///< what characters (Phase A.2)
    TextStyle       style;       ///< font, size, appearance (Phase A.2)
    TextFrame       frame;       ///< layout box (Phase A.2)
    ParagraphStyle  paragraph;   ///< paragraph-level style (reused from paragraph_style.hpp)
    TextEffects     effects;     ///< glow, shadow, etc. (Phase A.3)
    TextAnimation   animation;   ///< typewriter, reveal, etc. (Phase A.3)
};

} // namespace chronon3d
