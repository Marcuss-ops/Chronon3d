#pragma once

// ─── Chronon3D — TextSpan POD foundation (FASE 4, Blocco B) ───────────────
//
// Per-span style/motion/identity overrides that complement (do NOT replace)
// the document/paragraph-level `TextSpec` shape canonised by
// `docs/MIGRATION_TEXT_SPEC.md` (TICKET-002 🟢 Done).
//
// Hierarchy:    TextDocument → Paragraph → TextSpan[] → per-span overrides
// Inheritance:  merge at RENDER time, NOT at construction (rendering layer)
// Cross-ref:    `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md` Fase 4 §Piano operativo
// Guard:        DO NOT wire into `TextLayoutEngine` in this PR (Fase 4 step 2 only).
//
// Naming convention:
//   * whole-text shape / arg-shaped  → `…Spec` suffix   (TextSpec, TextLayoutSpec,
//                                                      FontSpec — per MIGRATION_TEXT_SPEC.md §1)
//   * span-shaped overrides         → `…Style` suffix  (TextSpanStyle, TextStrokeStyle,
//                                                      TextHighlightStyle — this file)

#include <chronon3d/core/types/types.hpp>           // u32, f32, u64
#include <chronon3d/math/color.hpp>                // Color (RGBA POD)
#include <chronon3d/text/glyph_selector_spec.hpp>  // GlyphSelectorSpec (TICKET-046 Phase 1a)
#include <chronon3d/text/text_material.hpp>         // TextMaterial

#include <optional>
#include <string>
#include <vector>

namespace chronon3d {

// ─── Extension points (Fase 4 step 2 — promotion to dedicated headers) ────
//
// Minimal viable PODs that satisfy the `std::optional<T>` complete-type
// requirement of `TextSpanStyle`. Will be promoted to:
//   * `include/chronon3d/text/text_stroke.hpp`
//   * `include/chronon3d/text/text_highlight_box.hpp`
// in Fase 4 step 2 with full implementations (dash patterns, multi-pass
// material layers, padding semantics, etc.).
struct TextStrokeStyle {
    f32                          width_em{0.0f};    ///< 0 = no stroke; >0 = EM width
    std::optional<Color>         color;             ///< empty = fall back to fg color at render
    std::optional<f32>           dash_period_em{0.0f}; ///< 0 = solid; >0 = dash period in EM
};

struct TextHighlightStyle {
    std::optional<Color>         background;        ///< empty = no highlight
    std::optional<f32>           padding_em{0.5f};  ///< default 0.5 EM
    std::optional<f32>           radius_em{0.0f};   ///< 0 = square corners
};

// ─── Identity ────────────────────────────────────────────────────────────

/// Semantic anchor for analytics, audio sync, subtitle highlights, template
/// reuse. `icon` is either an emoji codepoint or a sprite-id string resolved
/// at render time (see Fase 4 step 2 docs when icon enumeration lands).
struct TextSpanIdentity {
    std::string                  semantic_id;       ///< stable id, e.g. "keyword-1"
    std::optional<std::string>   icon;              ///< emoji cp | unicode escape | sprite-id
};

// ─── Style (per-span POD overrides) ──────────────────────────────────────

/// `std::optional<T>` semantically: empty = fall back to TextSpec default
/// at render; filled = override; merge logic lives in rendering layer.
struct TextSpanStyle {
    std::optional<Color>             color;
    std::optional<u32>               font_weight;       ///< 100..900 standard axis
    std::optional<f32>               font_size;         ///< multiplier vs paragraph default
    std::optional<f32>               tracking;          ///< letter-spacing in EM
    std::optional<f32>               line_height;       ///< multiplier, e.g. 1.10f
    std::optional<TextStrokeStyle>   stroke;            ///< per-span stroke override
    std::optional<TextMaterial>      material;          ///< per-span material override
    std::optional<TextHighlightStyle> highlight;        ///< per-span background box override
};

// ─── Motion ──────────────────────────────────────────────────────────────

// Determinism caveat:
// `GlyphSelectorSpec::random_seed` defaults to 0. Under SelectorOrder::Random,
// callers MUST set a per-span-disambiguating seed (e.g., hash of semantic_id).
// Default seed=0 across multiple spans collapses to identical permutations,
// breaking per-span independence and determinism guarantees (AGENTS.md).

/// Per-span selectors that reuse `GlyphSelectorSpec` (Fase 3).
struct TextSpanMotion {
    std::vector<GlyphSelectorSpec>   selectors;
};

// ─── TextSpan (THE POD) ──────────────────────────────────────────────────

/// A contiguous text segment within a Paragraph or directly under an
/// unstructured TextDocument. Strict inheritance: overrides apply ONLY
/// to fields set in `style`; unset fields fall back to TextSpec defaults
/// at render time (post Fase 4 step 2).
struct TextSpan {
    std::string                       text;            ///< UTF-8 text content
    std::optional<TextSpanStyle>      style;
    std::optional<TextSpanMotion>     motion;
    std::optional<TextSpanIdentity>   identity;
};

} // namespace chronon3d
