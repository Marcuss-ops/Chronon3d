#pragma once

#include <chronon3d/timeline/composition.hpp>

namespace chronon3d {

/**
 * SequenceContext represents a temporal slice of the timeline.
 * It provides a local frame counter that starts from 0 at the 'from' point.
 */
struct SequenceContext {
    FrameContext parent;
    Frame frame{0};     // Local frame (0 to duration-1)
    Frame from{0};      // Start frame in global timeline
    Frame duration{0};  // Duration of the sequence
    bool active{false}; // True if the global frame is within [from, from + duration)

    [[nodiscard]] f32 progress() const {
        if (duration == 0) return 0.0f;
        return std::clamp(static_cast<f32>(frame) / static_cast<f32>(duration), 0.0f, 1.0f);
    }

    // Like progress(), but holds at 0 before the sequence and at 1 after it ends.
    // Use this when an animation should stay at its final state once the window closes.
    [[nodiscard]] f32 held_progress() const {
        if (!active && parent.frame >= from + duration) return 1.0f;
        return progress();
    }

    [[nodiscard]] f32 seconds() const {
        return static_cast<f32>(frame) * (static_cast<f32>(parent.frame_rate.denominator) / static_cast<f32>(parent.frame_rate.numerator));
    }
};

/**
 * Creates a SequenceContext from the current FrameContext.
 */
inline SequenceContext sequence(const FrameContext& ctx, Frame from, Frame duration) {
    bool active = ctx.frame >= from && ctx.frame < from + duration;
    Frame local_frame = active ? Frame{ctx.frame - from} : Frame{0};

    return SequenceContext{
        .parent = ctx,
        .frame = local_frame,
        .from = from,
        .duration = duration,
        .active = active
    };
}

} // namespace chronon3d
