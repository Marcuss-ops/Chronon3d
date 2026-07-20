#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_span_override.hpp — Authoring-level per-range text style overrides
// ═══════════════════════════════════════════════════════════════════════════
//
// TextSpanOverride lives at the AUTHORING layer (TextDefinition / TextSpec).
// It is intentionally separate from the runtime TextStyleSpan (text_document.hpp)
// and the richer TextSpan (text_span.hpp) to keep the authoring surface
// lightweight and avoid pulling runtime resolver types into SDK headers.
//
// This header is included by both text_definition.hpp and builder_params.hpp.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/types/types.hpp>   // f32
#include <chronon3d/math/color.hpp>          // Color
#include <chronon3d/text/font_engine.hpp>    // FontSpec
#include <chronon3d/text/text_span.hpp>      // TextStrokeStyle

#include <cstddef>
#include <optional>
#include <string>

namespace chronon3d {

/// Per-range style override stored in the authoring DTO (TextDefinition)
/// and in the runtime spec (TextSpec).  Fields are std::optional: empty
/// means "inherit from the surrounding default / span".
struct TextSpanOverride {
    std::size_t byte_start{0};
    std::size_t byte_end{0};

    std::optional<FontSpec>  font;
    std::optional<Color>     color;
    std::optional<f32>       font_size;            // absolute px
    std::optional<f32>       font_size_multiplier; // relative to default
    std::optional<f32>       tracking;
    std::optional<f32>       baseline_shift;
    std::optional<std::string> semantic_id;
    std::optional<TextStrokeStyle> stroke;
};

} // namespace chronon3d
