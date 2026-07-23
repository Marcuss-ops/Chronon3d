#pragma once

#include <chronon3d/animation/easing/easing.hpp>

namespace chronon3d {

/// Kinds of transitions between two full-frame clips/sources.
enum class ClipTransitionKind : std::uint8_t {
    Cut,
    Dissolve,
};

/// Policy for handling input clips/sources with different resolutions.
enum class ClipTransitionFitPolicy : std::uint8_t {
    /// Scale the inputs to cover the output canvas (default).
    ScaleToFit,
    /// Reject mismatched dimensions with a structured error.
    ErrorOnMismatch,
};

/// Typed descriptor for a transition between two clips/sources (A -> B).
/// Unlike LayerTransitionSpec, this operates on two full inputs rather
/// than a single layer reveal.  The temporal window is supplied by the
/// graph builder via ClipTransitionNode's constructor (from + duration in
/// frames), so this spec only carries the transition kind and easing.
///
/// Color/alpha policy:
///   - Inputs and output are expected to be in premultiplied linear RGBA.
///   - Cut: output is the selected input, scaled to the output canvas.
///   - Dissolve: output = A*(1-p) + B*p per-channel in premultiplied space.
///   Callers with straight-alpha inputs must premultiply before wiring
///   the inputs into this node.
struct ClipTransitionSpec {
    ClipTransitionKind kind{ClipTransitionKind::Cut};
    Easing easing{Easing::Linear};
    ClipTransitionFitPolicy fit{ClipTransitionFitPolicy::ScaleToFit};
};

} // namespace chronon3d
