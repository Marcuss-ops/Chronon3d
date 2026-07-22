// SPDX-License-Identifier: MIT
//
// M1.5#2 — implementation for text_state_sampler.  See header for contract.

#include "text_state_sampler.hpp"

namespace chronon3d::text::driver {

std::string select_target_text(const ActiveTextState& state) {
    if (state.active == nullptr) {
        return {};
    }
    switch (state.transition) {
        case SourceTextTransition::Hold:
        case SourceTextTransition::Cut:
        case SourceTextTransition::DissolveLayouts:
            // No transition-text blending for these modes (Hold/Cut are
            // discrete; DissolveLayouts blends per-glyph in the
            // compositor).
            return state.active->utf8;

        case SourceTextTransition::Scramble:
        case SourceTextTransition::Morph:
            // transition_text holds the per-frame generated string
            // produced by AnimatedTextDocument::sample_at.  Empty only
            // when pre/post-conditions clip the transition (e.g. mix=1
            // already); falls through to active->utf8.
            return state.transition_text.empty()
                ? state.active->utf8
                : state.transition_text;
    }
    return {};  // unreachable in well-formed enum usage
}

bool is_in_dissolve_gap(const ActiveTextState& state) {
    return state.transition == SourceTextTransition::DissolveLayouts
        && state.dissolve_from != nullptr
        && state.mix > 0.0f
        && state.mix < 1.0f;
}

}  // namespace chronon3d::text::driver
