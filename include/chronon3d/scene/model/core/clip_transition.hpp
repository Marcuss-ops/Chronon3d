#pragma once

#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/core/transition.hpp>
#include <string>

namespace chronon3d {

/// Kinds of transitions between two full-frame clips/sources.
enum class ClipTransitionKind : std::uint8_t {
    Cut,
    Dissolve,
    Push,
    Slide,
    Wipe,
    Iris,
    Zoom,
    Flash,
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
/// Transition-specific fields:
///   - direction: used by Push, Slide and Wipe (default Right).
///   - center:    normalised centre for Iris and Zoom ([0,1], default 0.5,0.5).
///   - feather:   edge softness for Wipe and Iris (relative to smaller
///                screen dimension, default 0.1).
///   - flash_color: tint used by Flash (default white).
///   - zoom_scale:  maximum extra scale applied by Zoom (default 2.0).
struct ClipTransitionSpec {
    ClipTransitionKind kind{ClipTransitionKind::Cut};
    Easing easing{Easing::Linear};
    ClipTransitionFitPolicy fit{ClipTransitionFitPolicy::ScaleToFit};
    TransitionDirection direction{TransitionDirection::Right};
    Vec2 center{0.5f, 0.5f};
    float feather = 0.1f;
    Color flash_color = Color::white();
    float zoom_scale = 2.0f;
};

/// Scene-level relationship that wires two named layers through a
/// ClipTransitionNode during graph compilation.
struct SceneClipTransition {
    std::string layer_a;
    std::string layer_b;
    ClipTransitionSpec spec;
    Frame from{0};
    Frame duration{0};
};

} // namespace chronon3d
