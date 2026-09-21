#pragma once

#include <cstdint>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace chronon3d::media::detail {

/// Validate the stream contract shared by two encoded segments.
[[nodiscard]] bool compatible_codec_parameters(
    const AVCodecParameters* left,
    const AVCodecParameters* right,
    std::string& reason);

/// Return a non-negative stream duration, treating FFmpeg's unknown sentinel
/// as zero so callers can use timestamp-derived duration as a fallback.
[[nodiscard]] std::int64_t stream_duration(const AVStream* stream) noexcept;

/// Move a timestamp from a segment-local origin onto the assembled timeline.
[[nodiscard]] std::int64_t normalize_timestamp(
    std::int64_t value,
    std::int64_t first,
    std::int64_t offset) noexcept;

} // namespace chronon3d::media::detail
