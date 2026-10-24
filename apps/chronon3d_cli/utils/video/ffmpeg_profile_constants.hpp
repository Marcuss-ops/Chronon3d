#pragma once

#include <libavcodec/avcodec.h>

namespace chronon3d::cli::ffmpeg_profile {

#if defined(AV_PROFILE_H264_BASELINE)
inline constexpr int h264_baseline = AV_PROFILE_H264_BASELINE;
inline constexpr int h264_main = AV_PROFILE_H264_MAIN;
inline constexpr int h264_high = AV_PROFILE_H264_HIGH;
inline constexpr int h264_high_10 = AV_PROFILE_H264_HIGH_10;
#else
inline constexpr int h264_baseline = FF_PROFILE_H264_BASELINE;
inline constexpr int h264_main = FF_PROFILE_H264_MAIN;
inline constexpr int h264_high = FF_PROFILE_H264_HIGH;
inline constexpr int h264_high_10 = FF_PROFILE_H264_HIGH_10;
#endif

} // namespace chronon3d::cli::ffmpeg_profile
