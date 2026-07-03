// SPDX-License-Identifier: MIT
//
// M1.5#2 — implementation for text_font_state.  See header for contract.

#include "text_font_state.hpp"

#include <chronon3d/scene/builders/builder_params.hpp>  // TextSpec

namespace chronon3d::text::driver {

namespace {

// Shape-preserving copy with active document overrides applied
// field-by-field.  P0-2: every non-empty / non-default field wins;
// the old `font_path.empty()` gate that suppressed family/weight/style
// overrides when the path matched is removed.
//
// `defaults` is a pointer to the active document's TextSpec defaults
// (TextDocument::defaults.font), forwarded from
// compute_effective_font.  Nullable for shapes with no active doc.
FontSpec apply_overrides_h(
    FontSpec basis,
    const chronon3d::TextSpec* defaults
) {
    if (defaults == nullptr) {
        return basis;
    }
    // `defaults` is a TextSpec* — its `.font` member is the FontSpec
    // we copy into.  `basis` IS the FontSpec, so direct field access.
    if (!defaults->font.font_path.empty()) {
        basis.font_path = defaults->font.font_path;
    }
    if (!defaults->font.font_family.empty()) {
        basis.font_family = defaults->font.font_family;
    }
    if (defaults->font.font_weight != 0) {
        basis.font_weight = defaults->font.font_weight;
    }
    if (!defaults->font.font_style.empty()) {
        basis.font_style = defaults->font.font_style;
    }
    if (defaults->font.font_size > 0.0f) {
        basis.font_size = defaults->font.font_size;
    }
    return basis;
}

}  // namespace

FontSpec compute_effective_font(
    const ActiveTextState& state,
    const TextRunLayout* prev_layout
) {
    FontSpec basis;
    if (prev_layout != nullptr) {
        basis = prev_layout->font;
    } else if (state.active != nullptr) {
        // First-frame shape: seed from the active document's defaults.
        basis = state.active->defaults.font;
    }
    // Forward active-doc overrides only when an active document is bound.
    const chronon3d::TextSpec* override_src =
        (state.active != nullptr) ? &state.active->defaults : nullptr;
    return apply_overrides_h(basis, override_src);
}

FontSpec compute_effective_font_for_prewarm(
    const ActiveTextState& state,
    const TextRunLayout* prev_layout
) {
    // Same as the apply path — canonical cache key requires identical
    // font projection.  Kept as a separate function to make the
    // prewarm lock contract obvious to readers + future audits.
    return compute_effective_font(state, prev_layout);
}

}  // namespace chronon3d::text::driver
