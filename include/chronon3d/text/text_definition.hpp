#pragma once

// text_definition.hpp — F2.A canonical text authoring DTO.
// (Lifecycle / Phase A.3 / F2.D / F3.D / Phase B narrative archived to ADR-019 §A.1.)

#include <chronon3d/math/glm_types.hpp>         // Vec2, Vec3
#include <chronon3d/math/color.hpp>              // Color
#include <chronon3d/text/font_engine.hpp>         // FontSpec
#include <chronon3d/text/paragraph_style.hpp>     // ParagraphStyle, TextAnchor, TextAlign,
                                                  // VerticalAlign, TextWrap, TextOverflow,
                                                  // TextCenteringMode
#include <chronon3d/text/text_material.hpp>       // TextMaterial
#include <chronon3d/text/text_span_override.hpp>  // TextSpanOverride
#include <chronon3d/scene/model/shape/shape.hpp>  // TextPaint, TextShadow
#include <chronon3d/text/text_content.hpp>           // TextContent (canonical)
#include <chronon3d/text/text_shaping_options.hpp>   // TextShapingOptions
#include <chronon3d/text/text_placement.hpp>          // TextPlacement, TextPlacementKind

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
// <chronon3d/text/text_content.hpp> — we reuse it directly, NO duplication.
// SpanOverride is a new authoring-only type that does not exist in
// builder_params.hpp; it lives here and is lowered to TextStyleSpan
// by the Phase B compiler.// TextSpanOverride is defined in <chronon3d/text/text_span_override.hpp>
// and reused here by value.

// ═══════════════════════════════════════════════════════════════════════════
// TextDefStyle — font, size, color, stroke, material (TextDefinition-scoped)
// ═══════════════════════════════════════════════════════════════════════════
//
// Named TextDefStyle (not TextStyle) to avoid collision with the existing
// chronon3d::TextStyle in shape.hpp (which is the registry-resolved style
// used by the authoring facade's .style(id, registry) path).
// TextDefStyle is the TextDefinition-internal representation that maps
// from TextDefaults::font + TextDefaults::appearance fields.

struct TextDefStyle {
    // Per-field phase tags (TICKET-TEXT-PROPERTY-PHASES):
    //   .font        → PreLayout (reflow; font size feeds HarfBuzz shaping metrics)
    //   .color       → PostLayout (visual-only)
    //   .paint       → PostLayout (visual-only)
    //   .shadows     → PostLayout (visual-only)
    //   .material    → PostLayout (visual-only)
    //   .box_style   → PostLayout (visual-only)

    /// Font specification (path, family, weight, size).
    /// Maps to TextDefaults::font.
    FontSpec font{};

    /// Fill color (maps to TextDefaults::appearance.color).
    Color color{1.0f, 1.0f, 1.0f, 1.0f};

    /// Paint settings (stroke enabled, stroke color, stroke width).
    /// Maps to TextDefaults::appearance.paint.
    TextPaint paint{};

    /// Per-layer shadow stack (maps to TextDefaults::appearance.shadows).
    std::vector<TextShadow> shadows{};

    /// Premium material settings (gradient, bevel, neon, etc.).
    /// Maps to TextDefaults::appearance.material.
    TextMaterial material{};

    /// Box background style (maps to TextDefaults::appearance.box_style).
    TextBoxStyle box_style{};
};

// ═══════════════════════════════════════════════════════════════════════════
// TextFrame — layout box, placement, alignment
// ═══════════════════════════════════════════════════════════════════════════

struct TextFrame {
    // Per-field phase tags (TICKET-TEXT-PROPERTY-PHASES): all (.size/.placement/.anchor/.align/.vertical_align/.wrap/.overflow/.centering_mode/.line_height/.tracking/.auto_fit/.min_font_size/.max_font_size/.max_lines/.ellipsis) are PreLayout (reflow). Animated overrides (TextAnimator::position/scale/rotation/skew/anchor/tracking/opacity/blur/fill_color/stroke_color/stroke_width/baseline_shift) run PostLayout per-glyph.

    /// Layout box size in canvas pixels (maps to TextDefaults::layout.box).
    Vec2 size{900.0f, 160.0f};

    /// Canonical placement semantics + 2D pin offset (the F3 lossless
    /// round-trip channel; replaces the redundant Vec3 position +
    /// TextPlacementKind + Vec2 offset triple). Depth (TextDefaults's old
    /// position.z) is intentionally not preserved — the render pipeline
    /// operates on 2D pin coordinates via resolve_text_placement().
    TextPlacement placement{TextPlacementKind::Absolute};

    /// Anchor point for positioning (maps to TextDefaults::layout.anchor).
    TextAnchor anchor{TextAnchor::Center};

    /// Horizontal text alignment (maps to TextDefaults::layout.align).
    TextAlign align{TextAlign::Center};

    /// Vertical alignment within the box (maps to TextDefaults::layout.vertical_align).
    VerticalAlign vertical_align{VerticalAlign::Middle};

    /// Text wrapping mode (maps to TextDefaults::layout.wrap).
    TextWrap wrap{TextWrap::Word};

    /// Overflow handling (maps to TextDefaults::layout.overflow).
    TextOverflow overflow{TextOverflow::Clip};

    /// Centering mode (maps to TextDefaults::layout.centering_mode).
    TextCenteringMode centering_mode{TextCenteringMode::LayoutBox};

    /// Line height multiplier (maps to TextDefaults::layout.line_height).
    f32 line_height{1.2f};

    /// Per-cluster tracking in pixels (maps to TextDefaults::layout.tracking).
    f32 tracking{0.0f};

    /// Auto-fit: shrink font to fit box (maps to TextDefaults::layout.auto_fit).
    bool auto_fit{false};

    /// Minimum font size for auto-fit (maps to TextDefaults::layout.min_font_size).
    f32 min_font_size{12.0f};

    /// Maximum font size for auto-fit (maps to TextDefaults::layout.max_font_size).
    f32 max_font_size{160.0f};

    /// Maximum number of lines (0 = unlimited) (maps to TextDefaults::layout.max_lines).
    i32 max_lines{0};

    /// Append ellipsis on overflow (maps to TextDefaults::layout.ellipsis).
    bool ellipsis{false};
};

// ═══════════════════════════════════════════════════════════════════════════
// TextAnimation — runtime animation contracts (Phase A.3)
// ═══════════════════════════════════════════════════════════════════════════
//
// TextAnimation — runtime animation contract (Phase A.3). Sibling naming + field semantics archived in ADR-019 §A.1.

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
    // Per-field phase tags (TICKET-TEXT-PROPERTY-PHASES):
    //   .content     → PreShaping substrate (the value field is the input to PreShaping evaluators like CharacterOffset)
    //   .spans[]     → mixed; per-span font is PreLayout, per-span color is PostLayout
    //   .style       → font=PreLayout (reflow), color/paint/shadows/material/box_style=PostLayout (see TextDefStyle)
    //   .frame       → PreLayout (reflow; per-field table above in TextFrame)
    //   .paragraph   → PreLayout (reflow)
    //   .animation   → PostLayout (visual-only; runtime animator stack applied per-frame per-glyph after layout)

    TextContent              content;     ///< text + pre-shaped glyphs (canonical from <chronon3d/text/text_content.hpp>)
    std::vector<TextSpanOverride> spans;  ///< per-range style overrides (authoring-level)
    TextDefStyle             style;       ///< font, size, color, stroke, material
    TextFrame                frame;       ///< layout box, position, alignment
    ParagraphStyle           paragraph;   ///< paragraph-level typography (reused)
    TextAnimation            animation;   ///< typewriter, reveal, etc. (Phase A.3)
};

// ═══════════════════════════════════════════════════════════════════════════
// ═══════════════════════════════════════════════════════════════════════════
//
/// Phase B — lower the canonical TextDefinition into a TextDocument
/// (the runtime pipeline model consumed by compile_text_layout()).
///
/// Maps:
///   - content.value → doc.utf8
///   - style + frame + paragraph → doc.defaults
///   - spans (TextSpanOverride) → doc.spans (TextStyleSpan)
///   - paragraph → split_paragraphs()
///
/// Callers that need a TextLayoutSpec for compile_text_layout() should lower
/// through `to_text_document()` and the canonical prepared-text pipeline.
///
/// Usage:
///   TextDefinition def = make_text_definition(opts);
///   PreparedText prepared = prepare_text(def).value();
///   TextLayoutRequest req{&prepared.document, &prepared.frame, prepared.style.font};
///   auto result = compile_text_layout(req, services);
[[nodiscard]] TextDocument to_text_document(const TextDefinition& def);

} // namespace chronon3d
