#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_appearance_spec.hpp — Canonical text appearance specification
//
// Moved from <chronon3d/scene/builders/params/text_params.hpp> to the text
// module.  Visual-only properties (color/paint/shadows/material/box_style)
// are post-layout; animations targeting these properties do not invalidate
// the shaping/layout cache.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/math/color.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>  // TextPaint, TextShadow, TextBoxStyle

#include <vector>

namespace chronon3d {

struct TextAppearanceSpec {
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    TextPaint paint{};
    std::vector<TextShadow> shadows{};
    TextMaterial material{};
    TextBoxStyle box_style{};
};

} // namespace chronon3d
