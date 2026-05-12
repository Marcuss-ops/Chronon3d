#pragma once

#include <chronon3d/core/frame_context.hpp>

namespace chronon3d {

struct SequenceContext {
    FrameContext parent;
    Frame local_frame{0};

    [[nodiscard]] Frame frame() const {
        return local_frame;
    }
};

inline bool in_sequence(const FrameContext& ctx, Frame from, Frame duration) {
    return ctx.frame >= from && ctx.frame < from + duration;
}

inline SequenceContext sequence_context(const FrameContext& ctx, Frame from) {
    return SequenceContext{
        .parent = ctx,
        .local_frame = ctx.frame - from
    };
}

} // namespace chronon3d
