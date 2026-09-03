#pragma once

#include <cstdint>
#include <limits>
#include <string>

namespace chronon3d::media {

/// Stable failure taxonomy for media decode. These reasons describe source /
/// decoder failures only; terminal GPU device loss remains a separate runtime
/// failure domain and must never be represented here.
enum class DecodeFailureReason : std::uint8_t {
    None = 0,
    OpenInput,
    StreamInfo,
    NoVideoStream,
    DecoderUnavailable,
    BeforeStart,
    PresentationGap,
    SeekFailure,
    CorruptPacket,
    DecoderSubmitFailure,
    DecoderReceiveFailure,
    UnexpectedEof,
    UnsupportedFormatChange,
    NativeSurfaceUnavailable,
};

inline constexpr std::int64_t kNoDecodeTimestamp =
    std::numeric_limits<std::int64_t>::min();

struct DecodeDiagnostic {
    DecodeFailureReason reason{DecodeFailureReason::None};
    int ffmpeg_error{0};
    std::int64_t pts{kNoDecodeTimestamp};
    std::int64_t dts{kNoDecodeTimestamp};
    std::uint64_t source_order{0};
    std::string message{};

    [[nodiscard]] bool failed() const noexcept {
        return reason != DecodeFailureReason::None;
    }
};

} // namespace chronon3d::media
