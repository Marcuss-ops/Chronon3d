#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_direction.hpp — Lightweight TextDirection enum
//
// Extracted from font_engine.hpp so headers like paragraph_style.hpp can
// reference TextDirection without dragging in the full FontEngine,
// GlyphPosition, PlacedGlyphRun, etc.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/types/types.hpp>

namespace chronon3d {

/// Shaping direction for non-Latin / complex-script text.
/// When Auto, the shaping engine detects RTL from the first
/// strongly-directional character (Arabic, Hebrew, etc.).
enum class TextDirection : u8 {
    Auto,
    LTR,
    RTL,
};

} // namespace chronon3d
