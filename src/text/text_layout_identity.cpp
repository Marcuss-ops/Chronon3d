// ═══════════════════════════════════════════════════════════════════════════
// src/text/text_layout_identity.cpp — M1.5#4
//
//   Split from the original text-run implementation. Contains:
//     - font_layout_identity_of(const TextRunLayout&)
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_run.hpp>
#include <chronon3d/text/font_engine.hpp>  // P0-2 — font_layout_identity_of(FontSpec, ...)

namespace chronon3d {

FontLayoutIdentity font_layout_identity_of(const TextRunLayout& layout) noexcept {
    FontLayoutIdentity identity = font_layout_identity_of(
        layout.font, layout.font_size, layout.features,
        layout.variation_axes);
    // A document-level TextRunLayout may contain independently varied
    // paragraphs. Preserve each span's coordinates in the fast-path identity;
    // the legacy layout field alone represents only the first paragraph.
    for (const auto& span : layout.font_spans) {
        if (span.variation_axes == layout.variation_axes) continue;
        identity.variation_axes += "|" + std::to_string(span.glyph_begin) + ":" +
            std::to_string(span.glyph_end) + "=" + span.variation_axes;
    }
    return identity;
}

} // namespace chronon3d
