#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_definition.hpp — F2.A canonical text authoring DTO
//
// TextDefinition is the SINGLE canonical representation for text authoring.
// Every text API (layer.text(), text_run(), centered_text(), presets)
// produces a TextDefinition. No duplicated representations for font size,
// position, anchor, alignment, box, or overflow.
//
// LIFECYCLE:
//
//   F2.A                    — reuse TextContent from builder_params.hpp +
//                              fill in TextStyle + TextFrame with real fields
//                              mapped from TextSpec
//   Phase A.3                  — fill in TextAnimation (animators +
//                              selectors + run-control + Frame envelope).
//                              from_text_run_spec now routes the 6 spec-only
//                              fields into TextAnimation (replaces prior
//                              (void)silence).
//                              LOSSY REVERSE: from_text_definition returns
//                              TextSpec only — direction/language/script/
//                              animators/selectors are NOT carried back.
//   F2.D                         — to_text_run_spec closes the adapter gap.
//                              The 6 spec-only fields above are now carried
//                              back to a TextRunSpec from a TextDefinition.
//                              ADDITIONAL LOSSY DROP (per-run Frame envelope):
//                              TextAnimation.start_delay + .duration are
//                              NOT representable in TextRunSpec and are
//                              silently dropped during to_text_run_spec.
//                              Roundtrip TextDefinition → TextRunSpec →
//                              TextDefinition therefore re-initialises the
//                              Frame envelope to Frame{0}.  This is the
//                              documented, tested behaviour.
//   F3.D                         — LayerBuilder overloads
//                              (`text(name, TextDefinition)`
//                              in commands/shape_commands.cpp +
//                              `text_run(name, TextDefinition)`
//                              in commands/layer_builder_compile.cpp)
//                              now route via to_text_run_spec() instead of
//                              the F2.C lossy `from_text_definition()`
//                              path.  This makes the 17 helper-site
//                              call sites (`centered_text()` /
//                              `glow_text()` / `typewriter_text()` /
//                              presets) end-to-end lossless: animation
//                              fields populated in TextDefinition reach
//                              TextRunSpec + materialize_text_run_shape()
//                              instead of being silently dropped.
//                              F3.D also ADDs a forward-point overload
//                              `text(name, TextRunSpec)` — the symmetric
//                              counterpart of the existing
//                              `text_run(name, TextRunSpec)` — letting
//                              callers fully migrated to TextRunSpec
//                              authoring use the short-form
//                              `layer.text("id", run_spec).commit()`.
//                              Frame envelope drop (start_delay + duration)
//                              is identical to F2.D contract.
//   Phase B (implemented)   — to_text_document(const TextDefinition&) lowers
//                            this DTO into the canonical TextDocument pipeline
//                            model consumed by compile_text_layout()
//
// DO NOT USE TextDefinition IN THE RUNTIME PIPELINE.
// The runtime pipeline consumes TextDocument (text_document.hpp), not this
// authoring DTO.  TextDefinition is purely for authoring / deserialization.
//
// Per AGENTS.md "Non duplicare registry, resolver, sampler, cache, service
// locator o checklist": TextDefinition complements (does not duplicate)
// TextDocument.  TextDocument is the canonical pipeline model; this DTO is
// the authoring surface that lowers into it.
//
// ANTI-DUPLICATION: TextDefinition is now the SOLE canonical authoring DTO.
// All text APIs (TextSpec, centered_text(), glow_text(), presets) produce
// TextDefinition.  No duplicate representations for font size, position,
// anchor, alignment, box, overflow, color, or stroke.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/math/glm_types.hpp>         // Vec2, Vec3
#include <chronon3d/math/color.hpp>              // Color
#include <chronon3d/text/font_engine.hpp>         // FontSpec
#include <chronon3d/text/paragraph_style.hpp>     // ParagraphStyle, TextAnchor, TextAlign,
                                                  // VerticalAlign, TextWrap, TextOverflow,
                                                  // TextCenteringMode
#include <chronon3d/text/text_material.hpp>       // TextMaterial
#include <chronon3d/text/text_placement.hpp>       // TextPlacement, TextPlacementKind
#include <chronon3d/scene/model/shape/shape.hpp>  // TextPaint, TextShadow
#include <chronon3d/scene/builders/builder_params.hpp>  // TextContent (canonical), TextSpec, TextRunSpec

// Phase A.3 — TextAnimation fields (TextAnimatorSpec, GlyphSelectorSpec,
// TextDirection, Frame).  Included directly (not via the compat shim) for
// minimal per-TU include surface.
#include <cstdint>                                     // std::uint32_t (script tag)
#include <chronon3d/text/animation/text_animator_spec.hpp>  // TextAnimatorSpec
#include <chronon3d/text/glyph_selector_spec.hpp>            // GlyphSelectorSpec
#include <chronon3d/text/text_direction.hpp>                 // TextDirection
#include <chronon3d/core/types/frame.hpp>                    // Frame (start_delay, duration)

#include <cstddef>   // std::size_t
#include <optional>
#include <string>
#include <vector>

namespace chronon3d {

// Forward declaration — TextDocument is the runtime pipeline model.
class TextDocument;

// ═══════════════════════════════════════════════════════════════════════════
// TextSpanOverride — per-range style overrides (authoring-level)
// ═══════════════════════════════════════════════════════════════════════════
//
// TextContent (value + pre_shaped) is the canonical struct from
// builder_params.hpp — we reuse it directly, NO duplication.
// SpanOverride is a new authoring-only type that does not exist in
// builder_params.hpp; it lives here and is lowered to TextStyleSpan
// by the Phase B compiler.

struct TextSpanOverride {
    std::size_t byte_start{0};
    std::size_t byte_end{0};
    std::optional<FontSpec>  font;
    std::optional<Color>     color;
    std::optional<f32>       font_size;
};

// ═══════════════════════════════════════════════════════════════════════════
// TextDefStyle — font, size, color, stroke, material (TextDefinition-scoped)
// ═══════════════════════════════════════════════════════════════════════════
//
// Named TextDefStyle (not TextStyle) to avoid collision with the existing
// chronon3d::TextStyle in shape.hpp (which is the registry-resolved style
// used by the authoring facade's .style(id, registry) path).
// TextDefStyle is the TextDefinition-internal representation that maps
// from TextSpec::font + TextSpec::appearance fields.

struct TextDefStyle {
    /// Font specification (path, family, weight, size).
    /// Maps to TextSpec::font.
    FontSpec font{};

    /// Fill color (maps to TextSpec::appearance.color).
    Color color{1.0f, 1.0f, 1.0f, 1.0f};

    /// Paint settings (stroke enabled, stroke color, stroke width).
    /// Maps to TextSpec::appearance.paint.
    TextPaint paint{};

    /// Per-layer shadow stack (maps to TextSpec::appearance.shadows).
    std::vector<TextShadow> shadows{};

    /// Premium material settings (gradient, bevel, neon, etc.).
    /// Maps to TextSpec::appearance.material.
    TextMaterial material{};

    /// Box background style (maps to TextSpec::appearance.box_style).
    TextBoxStyle box_style{};
};

// ═══════════════════════════════════════════════════════════════════════════
// TextFrame — layout box, placement, alignment
// ═══════════════════════════════════════════════════════════════════════════

struct TextFrame {
    /// Layout box size in canvas pixels (maps to TextSpec::layout.box).
    Vec2 size{900.0f, 160.0f};

    /// Canonical placement semantics + 2D pin offset (the F3 lossless
    /// round-trip channel; replaces the redundant Vec3 position +
    /// TextPlacementKind + Vec2 offset triple). Depth (TextSpec's old
    /// position.z) is intentionally not preserved — the render pipeline
    /// operates on 2D pin coordinates via resolve_text_placement().
    TextPlacement placement{TextPlacementKind::Absolute};

    /// Anchor point for positioning (maps to TextSpec::layout.anchor).
    TextAnchor anchor{TextAnchor::Center};

    /// Horizontal text alignment (maps to TextSpec::layout.align).
    TextAlign align{TextAlign::Center};

    /// Vertical alignment within the box (maps to TextSpec::layout.vertical_align).
    VerticalAlign vertical_align{VerticalAlign::Middle};

    /// Text wrapping mode (maps to TextSpec::layout.wrap).
    TextWrap wrap{TextWrap::Word};

    /// Overflow handling (maps to TextSpec::layout.overflow).
    TextOverflow overflow{TextOverflow::Clip};

    /// Centering mode (maps to TextSpec::layout.centering_mode).
    TextCenteringMode centering_mode{TextCenteringMode::LayoutBox};

    /// Line height multiplier (maps to TextSpec::layout.line_height).
    f32 line_height{1.2f};

    /// Per-cluster tracking in pixels (maps to TextSpec::layout.tracking).
    f32 tracking{0.0f};

    /// Auto-fit: shrink font to fit box (maps to TextSpec::layout.auto_fit).
    bool auto_fit{false};

    /// Minimum font size for auto-fit (maps to TextSpec::layout.min_font_size).
    f32 min_font_size{12.0f};

    /// Maximum font size for auto-fit (maps to TextSpec::layout.max_font_size).
    f32 max_font_size{160.0f};

    /// Maximum number of lines (0 = unlimited) (maps to TextSpec::layout.max_lines).
    i32 max_lines{0};

    /// Append ellipsis on overflow (maps to TextSpec::layout.ellipsis).
    bool ellipsis{false};
};

// ═══════════════════════════════════════════════════════════════════════════
// TextAnimation — runtime animation contracts (Phase A.3)
// ═══════════════════════════════════════════════════════════════════════════
//
// TextAnimation holds the per-text-run animation contract.  The fields mirror
// the top-level editor surface carried by `TextRunSpec` (builder_params.hpp),
// lifted here so the canonical `TextDefinition` DTO carries the full authoring
// state without leaking through the adapter boundary.
//
//   animators    — per-property animator specs (opacity, typewriter, etc.)
//                  Managed by evaluate_animator_stack() at runtime.
//   selectors    — glyph-targeting predicates (matches GlyphSelectorSpec enum
//                  surface in shape.hpp).
//   direction    — HarfBuzz shaping direction (default = Auto → engine
//                  auto-detects via hb_buffer_guess_segment_properties()).
//   language     — BCP-47 tag (empty = engine auto-detect).
//   script       — OpenType script tag (HB_SCRIPT_*).
//                  0 = auto, 0x4C61746E (Latin), 0x41726162 (Arabic), etc.
//   cache_layout — hint: cache the compiled layout for repeated playback.
//
//   start_delay  — GLOBAL envelope start frame;
//                  Frame{0} (default) = animations start immediately.
//                  Animator-internal properties[].frame are unaffected.
//   duration     — GLOBAL envelope length frame;
//                  Frame{0} (default) = use per-animator timeline length.
//
// Sibling naming: `TextAnimation` — verified no collision with existing types
// in shape.hpp or builder_params.hpp (TextAnimationSpec doesn't exist;
// animation-related types live under chronon3d::text::animation).

struct TextAnimation {
    std::vector<TextAnimatorSpec>  animators{};
    std::vector<GlyphSelectorSpec> selectors{};

    TextDirection direction{TextDirection::Auto};
    std::string   language;             // BCP-47 (empty = auto)
    std::uint32_t script{0u};           // OpenType tag (0 = auto)
    bool          cache_layout{true};

    Frame         start_delay{};        // Frame{0} = immediate
    Frame         duration{};           // Frame{0} = per-animator
};

// ═══════════════════════════════════════════════════════════════════════════
// TextDefinition — F2.A canonical authoring DTO
// ═══════════════════════════════════════════════════════════════════════════

struct TextDefinition {
    TextContent              content;     ///< text + pre-shaped glyphs (canonical from builder_params.hpp)
    std::vector<TextSpanOverride> spans;  ///< per-range style overrides (authoring-level)
    TextDefStyle             style;       ///< font, size, color, stroke, material
    TextFrame                frame;       ///< layout box, position, alignment
    ParagraphStyle           paragraph;   ///< paragraph-level typography (reused)
    TextAnimation            animation;   ///< typewriter, reveal, etc. (Phase A.3)
};

// ═══════════════════════════════════════════════════════════════════════════
// Adapter functions — TextSpec / TextRunSpec → TextDefinition
// ═══════════════════════════════════════════════════════════════════════════
//
// Forward-point: full adapter implementations land in F2.C (text()/
// text_run() become adapters).  For now, callers can populate TextDefinition
// directly from TextSpec fields (all fields are public with matching names).
//
//   TextDefinition def;
//   def.content.value = spec.content.value;
//   def.style.font    = spec.font;
//   def.style.color   = spec.appearance.color;
//   def.frame.size      = spec.layout.box;
//   def.frame.placement = TextPlacement{TextPlacementKind::Absolute,
//                                       Vec2{spec.position.x, spec.position.y}};
//   // ... etc.

/// Convert a TextSpec to the canonical TextDefinition.
/// Full implementation in src/text/text_definition.cpp.
[[nodiscard]] TextDefinition from_text_spec(const TextSpec& spec);

/// Convert a TextRunSpec to the canonical TextDefinition.
/// Includes animators (maps to TextAnimation placeholder — Phase A.3).
/// Full implementation in src/text/text_definition.cpp.
[[nodiscard]] TextDefinition from_text_run_spec(const TextRunSpec& spec);

/// F2.C — Reverse adapter: convert the canonical TextDefinition back to
/// TextSpec (the editor / authoring spec).
[[nodiscard]] TextSpec from_text_definition(const TextDefinition& def);

/// F2.D — Reverse adapter: convert the canonical TextDefinition back to
/// TextRunSpec (the editor / authoring spec that carries animators, selectors,
/// run-control fields, and cache_layout — the fields NOT present in TextSpec).
///
/// LOSSY DROP (documented): TextRunSpec does NOT represent the Frame envelope
/// (TextAnimation.start_delay + .duration); these fields are silently dropped
/// during the conversion.  Roundtrip TextDefinition → TextRunSpec →
/// TextDefinition therefore yields Frame{0} for both envelope fields — this
/// is the canonical, tested behaviour (see test_text_definition.cpp group 19).
///
/// Base fields (style, frame, paragraph) reuse from_text_definition() so the
/// two reverse adapters (`from_text_definition` for TextSpec and
/// `to_text_run_spec` for TextRunSpec) cannot drift out of sync — per Phase B
/// risk register "Duplicating the Base TextSpec Conversion".
///
/// Parallel naming pattern to `to_text_document` (Phase B) — both lowerers
/// consume TextDefinition into a downstream model.  Name chosen over the
/// user-suggested `from_run_spec_definition` for symmetry with
/// `to_text_document`.
[[nodiscard]] TextRunSpec to_text_run_spec(const TextDefinition& def);

/// Phase B — lower the canonical TextDefinition into a TextDocument
/// (the runtime pipeline model consumed by compile_text_layout()).
///
/// Maps:
///   - content.value → doc.utf8
///   - style + frame + paragraph → doc.defaults (via from_text_definition)
///   - spans (TextSpanOverride) → doc.spans (TextStyleSpan)
///   - paragraph → split_paragraphs()
///
/// Callers that need a TextLayoutSpec for compile_text_layout() should also
/// call from_text_definition() to obtain the matching TextSpec.layout.
///
/// Usage:
///   TextDefinition def = centered_text(opts);
///   TextDocument doc = to_text_document(def);
///   TextSpec spec = from_text_definition(def);
///   TextLayoutRequest req{&doc, &spec.layout, spec.font};
///   auto result = compile_text_layout(req, services);
[[nodiscard]] TextDocument to_text_document(const TextDefinition& def);

} // namespace chronon3d
