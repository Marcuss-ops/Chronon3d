#pragma once
#ifndef CHRONON3D_CLI_CANONICAL_FRAME_RATE_HPP
#define CHRONON3D_CLI_CANONICAL_FRAME_RATE_HPP

// ---------------------------------------------------------------------------
// canonical_frame_rate.hpp
//
// Single canonicalisation rule for the {fps, fps_num, fps_den} frame-rate
// triple. OutputOptions and FfmpegPipeOptions both carry the same three raw
// fields (with the same defaults) and both used to inline the same "which
// field wins" heuristic; the duplicated copies could drift (one of them
// disagreed on the denominator in the legacy branch). Everything now funnels
// through canonical_frame_rate() so the rule lives in exactly one place.
//
// Legacy rule (kept byte-for-byte for behaviour parity — see the parity test
// in tests/cli/test_ffmpeg_export_options.cpp):
//   * the rational fields start at the 30/1 sentinel, so a legacy integer
//     `fps` different from 30 is treated as an explicit author intent and
//     overrides the untouched 30/1 default (denominator collapses to 1);
//   * otherwise fps_num/fps_den are authoritative.
// ---------------------------------------------------------------------------

namespace chronon3d::cli {

struct CanonicalFrameRate {
    int num{30};
    int den{1};
};

[[nodiscard]] constexpr CanonicalFrameRate canonical_frame_rate(
    int fps, int fps_num, int fps_den) noexcept {
    if (fps_num == 30 && fps_den == 1 && fps != 30) {
        return {fps, 1};
    }
    return {fps_num, fps_den};
}

} // namespace chronon3d::cli

#endif // CHRONON3D_CLI_CANONICAL_FRAME_RATE_HPP
