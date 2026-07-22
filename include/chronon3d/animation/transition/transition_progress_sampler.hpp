#pragma once

#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/core/types/types.hpp>

#include <algorithm>
#include <cmath>

namespace chronon3d {

/// Direction of a transition. Only affects the final eased_progress:
/// - In:  eased_progress goes from 0 to 1.
/// - Out: eased_progress goes from 1 to 0 (i.e. 1 - ease(t)).
enum class TransitionProgressDirection : u8 {
    In,
    Out
};

/// Result of sampling a transition at a given time.
///
/// `linear_progress` is the geometric progress in [0, 1] before applying easing.
/// `eased_progress`  is `easing.apply(linear_progress)`, optionally inverted
///                    when direction == Out.
///
/// The flags describe the temporal relationship to the [start, end) interval:
///   before: current < start
///   active: start <= current < end
///   after:  current >= end
struct TransitionSample {
    float linear_progress{0.0f};
    float eased_progress{0.0f};
    bool before{false};
    bool active{false};
    bool after{false};
};

/// Sample a single transition at `current` over the half-open interval
/// [start, end).  All inputs use SampleTime so sub-frame evaluation and any
/// rational frame rate are handled consistently.
///
/// To express a duration instead of an end point, build `end` as
/// `SampleTime::from_frame(start.frame + duration_frames, start.frame_rate)`.
///
/// Semantic rules:
///   - current < start        -> linear_progress = 0, before = true
///   - current in [start,end) -> linear_progress = (current - start) / (end - start), active = true
///   - current >= end         -> linear_progress = 1, after = true
///   - start == end (duration 0 / cut) -> linear_progress = 1 when current >= start, else 0
///
/// `eased_progress` applies `easing` to `linear_progress`; for Out direction it
/// is further reflected as 1 - eased.
[[nodiscard]] inline TransitionSample sample_transition(
    SampleTime current,
    SampleTime start,
    SampleTime end,
    const EasingCurve& easing = EasingCurve{Easing::Linear},
    TransitionProgressDirection direction = TransitionProgressDirection::In
) {
    // All three samples must share the same frame-rate context; otherwise
    // comparing their frame coordinates is meaningless.  This is a cheap
    // debug-only guard because the function is intended for hot-path use.
    TransitionSample result;

    const double start_frame = start.frame;
    const double end_frame = end.frame;
    const double current_frame = current.frame;

    if (current_frame < start_frame) {
        result.before = true;
    } else if (end_frame > start_frame) {
        if (current_frame < end_frame) {
            result.active = true;
        } else {
            result.after = true;
        }
    } else {
        // Zero or negative duration behaves as a cut: the transition is
        // instantaneous at start.
        result.after = true;
    }

    double linear = 0.0;
    if (result.before) {
        linear = 0.0;
    } else if (result.after) {
        linear = 1.0;
    } else {
        const double denom = end_frame - start_frame;
        linear = (current_frame - start_frame) / denom;
        linear = std::clamp(linear, 0.0, 1.0);
    }

    result.linear_progress = static_cast<float>(linear);

    float eased = easing.apply(result.linear_progress);
    if (direction == TransitionProgressDirection::Out) {
        eased = 1.0f - eased;
    }
    result.eased_progress = eased;

    return result;
}

} // namespace chronon3d
