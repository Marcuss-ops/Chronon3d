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
        case SourceTextTransition::CrossfadeLayouts:
            // No transition-text blending for these modes (Hold/Cut are
            // discrete; CrossfadeLayouts blends per-glyph in the
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

std::optional<std::string> select_crossfade_target_text(
    const ActiveTextState& state
) {
    if (state.crossfade_from == nullptr) {
        return std::nullopt;
    }
    return state.crossfade_from->utf8;
}

bool is_in_crossfade_gap(const ActiveTextState& state) {
    return state.transition == SourceTextTransition::CrossfadeLayouts
        && state.crossfade_from != nullptr
        && state.mix > 0.0f
        && state.mix < 1.0f;
}

}  // namespace chronon3d::text::driver
