// SPDX-License-Identifier: MIT
//
// M1.5#2 — internal helper: text_state_sampler.
// Selects the target text + transition-text decision from an
// `ActiveTextState` sample.  Pure projection — no shape mutation, no cache
// access, no HarfBuzz.  Called by both `apply_active_state_to_text_run_shape`
// and `prewarm_text_run_layout_for_frame` via the orchestrator at
// `src/text/text_run_driver.cpp`.
//
// Internal-only — NOT in include/chronon3d/.

#pragma once

#include <chronon3d/text/animated_text_document.hpp>

#include <string>

namespace chronon3d::text::driver {

/// Honours `target_text` selection per `SourceTextTransition`:
///   - Hold / Cut / DissolveLayouts → `state.active->utf8` (the
///     transition_text is empty for these modes / mixed per-glyph in the
///     compositor, not in the layout).
///   - Scramble / Morph / Typewriter → `state.transition_text` when
///     `transition_text_authoritative` is set.  Empty is a valid target for
///     typewriter frame 0; it must not fall back to `state.active->utf8`.
///     When the flag is false (Hold/Cut/DissolveLayouts), returns
///     `state.active->utf8`.
/// Returns empty string when `state.active == nullptr`.  An empty string is
/// a valid layout target when the transition owns the text; callers must
/// rebuild the zero-glyph layout instead of treating it as "no work".
[[nodiscard]] std::string select_target_text(const ActiveTextState& state);

/// True iff the per-frame transition expects to shape crossfade slots:
/// `state.transition == DissolveLayouts` AND `state.dissolve_from != nullptr`
/// AND `0 < state.mix < 1`.
[[nodiscard]] bool is_in_dissolve_gap(const ActiveTextState& state);

}  // namespace chronon3d::text::driver
