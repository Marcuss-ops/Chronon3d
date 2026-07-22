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
///   - Scramble / Morph             → `state.transition_text` (filled
///     by `AnimatedTextDocument::sample_at` for these modes); falls back
///     to `state.active->utf8` when transition_text is empty (e.g. mix
///     pre/post-conditions clip the transition).
/// Returns empty string when `state.active == nullptr` (caller treats
/// this as a no-op opportunity).
[[nodiscard]] std::string select_target_text(const ActiveTextState& state);

/// True iff the per-frame transition expects to shape crossfade slots:
/// `state.transition == DissolveLayouts` AND `state.dissolve_from != nullptr`
/// AND `0 < state.mix < 1`.
[[nodiscard]] bool is_in_dissolve_gap(const ActiveTextState& state);

}  // namespace chronon3d::text::driver
