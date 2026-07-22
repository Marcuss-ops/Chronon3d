// SPDX-License-Identifier: MIT

// ═══════════════════════════════════════════════════════════════════════════
// text_spec_adapter.cpp — Legacy reverse adapters (compat-only)
//
// X2 canonization moved the reverse adapters out of the canonical text module.
// These functions are kept only for backward compatibility in tests and
// external consumers; production code should use PreparedText and the
// canonical compile_text_layout overload.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/compat/text_spec_adapter.hpp>

namespace chronon3d::compat {

TextSpec from_text_definition(const TextDefinition& def) {
    TextSpec spec;

    spec.content.value      = def.content.value;
    spec.content.pre_shaped = def.content.pre_shaped;
    spec.spans              = def.spans;
    spec.font               = def.style.font;

    spec.layout.box            = def.frame.size;
    spec.layout.anchor         = def.frame.anchor;
    spec.layout.align          = def.frame.align;
    spec.layout.vertical_align = def.frame.vertical_align;
    spec.layout.wrap           = def.frame.wrap;
    spec.layout.overflow       = def.frame.overflow;
    spec.layout.centering_mode = def.frame.centering_mode;
    spec.layout.line_height    = def.frame.line_height;
    spec.layout.tracking       = def.frame.tracking;
    spec.layout.auto_fit       = def.frame.auto_fit;
    spec.layout.min_font_size  = def.frame.min_font_size;
    spec.layout.max_font_size  = def.frame.max_font_size;
    spec.layout.max_lines      = def.frame.max_lines;
    spec.layout.ellipsis       = def.frame.ellipsis;
    spec.layout.paragraph      = def.paragraph;

    spec.appearance.color     = def.style.color;
    spec.appearance.paint     = def.style.paint;
    spec.appearance.shadows   = def.style.shadows;
    spec.appearance.material  = def.style.material;
    spec.appearance.box_style = def.style.box_style;

    spec.placement = {TextPlacementKind::Absolute, {def.frame.placement.offset.x, def.frame.placement.offset.y}};

    return spec;
}

TextRunSpec to_text_run_spec(const TextDefinition& def) {
    TextRunSpec run;
    run.text = from_text_definition(def);

    run.direction    = def.animation.direction;
    run.language     = def.animation.language;
    run.script         = def.animation.script;
    run.cache_layout = def.animation.cache_layout;
    run.animators    = def.animation.animators;
    run.selectors    = def.animation.selectors;

    return run;
}

} // namespace chronon3d::compat
