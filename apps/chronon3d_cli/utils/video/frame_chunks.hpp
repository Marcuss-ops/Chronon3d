#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <algorithm>
#include <vector>

namespace chronon3d::cli {

struct FrameChunk {
    Frame start;
    Frame end;
};

inline std::vector<FrameChunk> split_frame_range(Frame start, Frame end, int chunks) {
    std::vector<FrameChunk> out;

    const Frame total = end - start;
    if (total <= 0) {
        return out;
    }

    if (chunks < 1) {
        return out;
    }

    chunks = std::min<int>(chunks, static_cast<int>(total));

    const Frame base = total / chunks;
    const Frame rem = total % chunks;

    Frame cursor = start;

    for (int i = 0; i < chunks; ++i) {
        const Frame len = base + (i < rem ? 1 : 0);
        out.push_back(FrameChunk{
            .start = cursor,
            .end = cursor + len
        });
        cursor += len;
    }

    return out;
}

} // namespace chronon3d::cli
