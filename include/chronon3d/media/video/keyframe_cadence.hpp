#pragma once

// Keyframe cadence — the ONE authority for the GOP every native encoder path
// configures.
//
// WHY THIS FILE EXISTS
// --------------------
// The assembly-ready output contract (VELOX_ASSEMBLY_READY_V1) fixes
// `keyframe_interval: 48` on its 24 fps timeline, i.e. a keyframe every 2
// SECONDS. Both native encoder paths used to derive `gop_size` from the frame
// rate ALONE — `llround(fps_num / fps_den)` — which emits one keyframe per
// second (24 at 24 fps). The clip.render output-facts gateway then rejected
// every production render with
//
//     rendered output violates the output contract: keyframe interval 24 != 48
//
// The defect stayed invisible while the certified-facts boundary was a legacy
// flat summary (the dimension was unset, so the gate skipped it); it surfaced
// the moment a rebuilt server started decoding the complete `output_facts`
// object. Two independent hardcoded "2"s would have reproduced exactly that
// class of drift, so the cadence lives here once and both paths call it.
//
// CONTRACT
// --------
// The GOP is a TIME cadence (2 seconds), never a frame count: at 24 fps it is
// 48 frames, at 30 fps 60, at 25 fps 50. Deriving from the frame rate keeps
// the contract value for the certified rate without hardcoding 48, and stays
// correct for any other rate the timeline may carry.
//
// The producer-side regression guard is
// `tests/video/test_keyframe_cadence.cpp` (the helper, unit tier) plus the
// emitted-stream assertion in `tests/video/test_native_av_sink_lifecycle.cpp`
// (decodes the muxed output and counts real keyframe spacing). A gateway test
// alone is NOT sufficient: it cannot fail while the producer's binary is stale.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace chronon3d::media::video {

/// Keyframe cadence, in seconds. Matches VELOX_ASSEMBLY_READY_V1's
/// `keyframe_interval: 48` on a 24 fps timeline (48 / 24 = 2 s).
inline constexpr double kKeyframeIntervalSeconds = 2.0;

/// Number of frames between keyframes for a given frame rate.
///
/// Returns at least 1 so a degenerate or absurd rate can never disable
/// keyframes entirely (a stream with no keyframe is undecodable from any seek
/// point). Non-positive inputs are the caller's validation error and fall back
/// to a single-frame GOP rather than propagating a zero.
[[nodiscard]] inline int keyframe_interval_frames(int frame_rate_num,
                                                  int frame_rate_den) noexcept {
    if (frame_rate_num <= 0 || frame_rate_den <= 0) {
        return 1;
    }
    const double fps = static_cast<double>(frame_rate_num) /
                       static_cast<double>(frame_rate_den);
    return std::max(1, static_cast<int>(std::llround(kKeyframeIntervalSeconds * fps)));
}

} // namespace chronon3d::media::video
