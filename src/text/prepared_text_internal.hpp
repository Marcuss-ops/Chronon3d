#pragma once

#include <chronon3d/text/prepared_text.hpp>

namespace chronon3d::text_internal {

/// Return a self-consistent canonical payload for compiler consumption.
/// The sibling canonical fields are authoritative; document-derived defaults,
/// spans, and paragraph ranges are rebuilt at this boundary so caller edits
/// cannot leave a stale TextDocument behind.
[[nodiscard]] inline PreparedText normalize_prepared_text(const PreparedText& input) {
    PreparedText out = input;
    auto& doc = out.document;

    if (doc.utf8.empty() && !out.spans.empty()) {
        doc.utf8 = doc.defaults.content.value;
    }
    if (doc.utf8.empty()) {
        return out;
    }

    doc.defaults.content.value = doc.utf8;
    doc.defaults.placement = out.frame.placement;
    doc.defaults.font = out.style.font;
    doc.defaults.layout.box = out.frame.size;
    doc.defaults.layout.anchor = out.frame.anchor;
    doc.defaults.layout.align = out.frame.align;
    doc.defaults.layout.vertical_align = out.frame.vertical_align;
    doc.defaults.layout.wrap = out.frame.wrap;
    doc.defaults.layout.overflow = out.frame.overflow;
    doc.defaults.layout.centering_mode = out.frame.centering_mode;
    doc.defaults.layout.line_height = out.frame.line_height;
    doc.defaults.layout.tracking = out.frame.tracking;
    doc.defaults.layout.auto_fit = out.frame.auto_fit;
    doc.defaults.layout.min_font_size = out.frame.min_font_size;
    doc.defaults.layout.max_font_size = out.frame.max_font_size;
    doc.defaults.layout.max_lines = out.frame.max_lines;
    doc.defaults.layout.ellipsis = out.frame.ellipsis;
    doc.defaults.layout.paragraph = out.frame.paragraph;
    doc.defaults.appearance.color = out.style.color;
    doc.defaults.appearance.paint = out.style.paint;
    doc.defaults.appearance.shadows = out.style.shadows;
    doc.defaults.appearance.material = out.style.material;
    doc.defaults.appearance.box_style = out.style.box_style;

    doc.spans.clear();
    for (const auto& span : out.spans) {
        append_span_override(doc, span, out.style.font);
    }

    doc.paragraphs.clear();
    doc.split_paragraphs(out.frame.paragraph);
    return out;
}

} // namespace chronon3d::text_internal
