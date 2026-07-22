#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_shaping_options.hpp — Canonical per-document shaping options
//
// Holds the HarfBuzz/OpenType shaping parameters that influence glyph
// selection and positioning.  Previously these fields were scattered
// across TextRunSpec (direction/language/script) and TextLayoutSpec
// (features).  This struct is the single canonical location for the
// X1 canonization step.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_direction.hpp>

#include <cstdint>
#include <string>

namespace chronon3d {

struct TextShapingOptions {
    TextDirection direction{TextDirection::Auto};
    std::string language;
    std::uint32_t script{0u};
    std::string open_type_features;
};

} // namespace chronon3d
