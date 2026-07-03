// SPDX-License-Identifier: MIT
//
// M1.5#2 — internal helper: text_font_state.
// Projects `ActiveTextState` + previous `TextRunLayout*` into a concrete
// `FontSpec` for `build_text_run`.  Carries the P0-2 fix (font overrides
// no longer gated by `font_path.empty()`) forwards verbatim, so
// FontLayoutIdentity comparison covers family/weight/style/size.
//
// Internal-only — NOT in include/chronon3d/.

#pragma once

#include <chronon3d/text/animated_text_document.hpp>  // ActiveTextState
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_run.hpp>

namespace chronon3d::text::driver {

/// Compute the effective FontSpec for the next-frame layout:
/// starting from the previous layout's font (when present) so
/// non-overridden fields (e.g. weight inherit from prior state),
/// then apply the active document's `defaults.font` overrides field
/// by field (P0-2: every non-empty/non-default field wins).
///
/// `prev_layout` may be nullptr for first-frame shapes; in that case
/// the projection starts from `state.active->defaults.font` directly.
[[nodiscard]] FontSpec compute_effective_font(
    const ActiveTextState& state,
    const TextRunLayout* prev_layout
);

/// Compute the effective FontSpec for the prewarm path.  Mirrors
/// `compute_effective_font` EXACTLY (same field-by-field override
/// semantics) but parameterized on a `ShapePtr` + `Frame` so the
/// anticipator can sample the AnimatedTextDocument independently.
[[nodiscard]] FontSpec compute_effective_font_for_prewarm(
    const ActiveTextState& state,
    const TextRunLayout* prev_layout
);

}  // namespace chronon3d::text::driver
