#pragma once

#include <chronon3d/core/types/result.hpp>
#include <chronon3d/core/types/time.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d::media::video {

/// Coarse stream classification exposed by the canonical media probe without
/// leaking FFmpeg types across the public API boundary.
enum class MediaStreamKind {
    Video,
    Audio,
    Other,
};

/// Canonical, backend-independent facts discovered for one container stream.
/// All timestamps remain exact RationalTime values; floating-point seconds are
/// deliberately left to presentation/reporting boundaries.
struct MediaStreamProbe {
    int index{-1};
    MediaStreamKind kind{MediaStreamKind::Other};
    std::string codec;
    Rational time_base{0, 1};
    std::optional<RationalTime> duration;

    // Video metadata.
    int width{0};
    int height{0};
    std::string pixel_format;
    Rational frame_rate{0, 1};
    std::int64_t frame_count{0};

    // Audio metadata.
    int sample_rate{0};
    int channels{0};
    std::string sample_format;
};

/// Result of probing one media container. `duration` uses the container
/// duration when available, otherwise the longest valid stream duration. It is
/// absent when neither source provides a trustworthy duration.
struct MediaProbeInfo {
    std::string format_name;
    std::optional<RationalTime> duration;
    std::vector<MediaStreamProbe> streams;
};

enum class MediaProbeErrorCode {
    BackendUnavailable,
    OpenInput,
    StreamInfo,
};

struct MediaProbeError {
    MediaProbeErrorCode code{MediaProbeErrorCode::OpenInput};
    int native_code{0};
    std::string message;
};

/// Inspect an input/container through the canonical in-process libavformat
/// backend. Lean builds without native FFmpeg support fail closed with
/// BackendUnavailable; no subprocess/ffprobe fallback is used.
[[nodiscard]] Result<MediaProbeInfo, MediaProbeError> probe_media(
    const std::filesystem::path& input);

} // namespace chronon3d::media::video
