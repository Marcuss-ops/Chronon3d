// ═══════════════════════════════════════════════════════════════════════════
// src/text/text_layout_identity.cpp — M1.5#4
//
//   Extracted from src/text/text_run.cpp.  Contains:
//     - font_layout_identity_of(const TextRunLayout&)
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_run.hpp>
#include <chronon3d/text/font_engine.hpp>  // P0-2 — font_layout_identity_of(FontSpec, ...)

namespace chronon3d {

FontLayoutIdentity font_layout_identity_of(const TextRunLayout& layout) noexcept {
    return font_layout_identity_of(
        layout.font, layout.font_size, layout.features,
        layout.variation_axes);  // M1.5#5 — plumbed from TextRunLayout
}

} // namespace chronon3d
