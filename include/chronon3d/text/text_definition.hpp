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
//   F2.A (this commit)    — reuse TextContent from builder_params.hpp +
//                            fill in TextStyle + TextFrame with real fields
//                            mapped from TextSpec
//   Phase A.3 / F3.B/C     — fill in TextEffects + TextAnimation
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
#include <chronon3d/scene/model/shape/shape.hpp>  // TextPaint, TextShadow
#include <chronon3d/scene/builders/builder_params.hpp>  // TextContent (canonical), TextSpec, TextRunSpec

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

    /// Position in canvas space (maps to TextSpec::position).
    Vec3 position{};

    /// Separate offset from placement (maps to .place(...).offset(...)).
    Vec2 offset{};

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
// TextEffects — glow, shadow, bevel, blur (Phase A.3 placeholder)
// ═══════════════════════════════════════════════════════════════════════════
//
// Filled in by Phase A.3 / F3.B/C.  Currently empty — all effect settings
// live in TextStyle (paint, shadows, material) until the Phase A.3
// split separates compositor effects from paint settings.

struct TextEffects {};

// ═══════════════════════════════════════════════════════════════════════════
// TextAnimation — selectors, properties, timing (Phase A.3 placeholder)
// ═══════════════════════════════════════════════════════════════════════════
//
// Filled in by Phase A.3.  Will contain animators, selectors, and timing
// information.  Currently animators live in TextRunSpec (builder_params.hpp).

struct TextAnimation {};

// ═══════════════════════════════════════════════════════════════════════════
// TextDefinition — F2.A canonical authoring DTO
// ═══════════════════════════════════════════════════════════════════════════

struct TextDefinition {
    TextContent              content;     ///< text + pre-shaped glyphs (canonical from builder_params.hpp)
    std::vector<TextSpanOverride> spans;  ///< per-range style overrides (authoring-level)
    TextDefStyle             style;       ///< font, size, color, stroke, material
    TextFrame                frame;       ///< layout box, position, alignment
    ParagraphStyle           paragraph;   ///< paragraph-level typography (reused)
    TextEffects              effects;     ///< glow, shadow, etc. (Phase A.3)
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
//   def.frame.size    = spec.layout.box;
//   def.frame.position = spec.position;
//   // ... etc.

/// Convert a TextSpec to the canonical TextDefinition.
/// Full implementation in src/text/text_definition.cpp.
[[nodiscard]] TextDefinition from_text_spec(const TextSpec& spec);

/// Convert a TextRunSpec to the canonical TextDefinition.
/// Includes animators (maps to TextAnimation placeholder — Phase A.3).
/// Full implementation in src/text/text_definition.cpp.
[[nodiscard]] TextDefinition from_text_run_spec(const TextRunSpec& spec);

/// Reverse adapter: convert the canonical TextDefinition back to TextSpec.
/// Used by LayerBuilder::text(name, TextDefinition) and by helper functions
/// that need to bridge from the canonical DTO to the builder's TextSpec.
/// Full implementation in src/text/text_definition.cpp (F2.C adapter phase).
[[nodiscard]] TextSpec from_text_definition(const TextDefinition& def);

} // namespace chronon3d
