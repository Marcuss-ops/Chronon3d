#pragma once

#include <chronon3d/core/frame_context.hpp>

namespace chronon3d {

struct SequenceContext {
    FrameContext parent;
    Frame frame{0};
};

inline bool in_sequence(const FrameContext& ctx, Frame from, Frame duration) {
    return ctx.frame >= from && ctx.frame < from + duration;
}

inline SequenceContext sequence_context(const FrameContext& ctx, Frame from) {
    return SequenceContext{
        .parent = ctx,
        .frame = ctx.frame - from
    };
}

} // namespace chronon3d
