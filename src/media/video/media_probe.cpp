#include <chronon3d/media/video/media_probe.hpp>

#include <optional>
#include <string>
#include <utility>

#if defined(CHRONON3D_ENABLE_NATIVE_FFMPEG)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
}
#endif

namespace chronon3d::media::video {
namespace {

#if defined(CHRONON3D_ENABLE_NATIVE_FFMPEG)

struct FormatContextGuard {
    AVFormatContext* context{nullptr};

    ~FormatContextGuard() {
        if (context != nullptr) {
            avformat_close_input(&context);
        }
    }
};

[[nodiscard]] std::string ffmpeg_error_string(int error_code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    if (av_strerror(error_code, buffer, sizeof(buffer)) < 0) {
        return "FFmpeg error " + std::to_string(error_code);
    }
    return buffer;
}

[[nodiscard]] constexpr bool valid_av_rational(AVRational value) noexcept {
    return value.num > 0 && value.den > 0;
}

[[nodiscard]] constexpr Rational to_rational(AVRational value) noexcept {
    if (!valid_av_rational(value)) {
        return Rational{0, 1};
    }
    return Rational{value.num, value.den};
}

[[nodiscard]] std::optional<RationalTime> stream_duration(const AVStream& stream) {
    if (stream.duration == AV_NOPTS_VALUE || stream.duration <= 0 ||
        !valid_av_rational(stream.time_base)) {
        return std::nullopt;
    }
    return RationalTime{stream.duration, to_rational(stream.time_base)};
}

[[nodiscard]] bool duration_less(RationalTime lhs, RationalTime rhs) noexcept {
    // Durations accepted by this translation unit are positive and have
    // positive time-base components. Cross-multiplication stays comfortably
    // inside signed 128-bit range for FFmpeg's i64 timestamps + i32 rationals.
    const __int128 lhs_scaled = static_cast<__int128>(lhs.ticks()) *
                                static_cast<__int128>(lhs.time_base.numerator) *
                                static_cast<__int128>(rhs.time_base.denominator);
    const __int128 rhs_scaled = static_cast<__int128>(rhs.ticks()) *
                                static_cast<__int128>(rhs.time_base.numerator) *
                                static_cast<__int128>(lhs.time_base.denominator);
    return lhs_scaled < rhs_scaled;
}

#endif

} // namespace

Result<MediaProbeInfo, MediaProbeError> probe_media(
    const std::filesystem::path& input) {
#if !defined(CHRONON3D_ENABLE_NATIVE_FFMPEG)
    (void)input;
    return MediaProbeError{
        .code = MediaProbeErrorCode::BackendUnavailable,
        .native_code = 0,
        .message = "libavformat probe unavailable: build with CHRONON3D_ENABLE_NATIVE_FFMPEG=ON",
    };
#else
    FormatContextGuard format;
    const std::string input_path = input.string();
    int ffmpeg_result =
        avformat_open_input(&format.context, input_path.c_str(), nullptr, nullptr);
    if (ffmpeg_result < 0) {
        return MediaProbeError{
            .code = MediaProbeErrorCode::OpenInput,
            .native_code = ffmpeg_result,
            .message = ffmpeg_error_string(ffmpeg_result),
        };
    }

    ffmpeg_result = avformat_find_stream_info(format.context, nullptr);
    if (ffmpeg_result < 0) {
        return MediaProbeError{
            .code = MediaProbeErrorCode::StreamInfo,
            .native_code = ffmpeg_result,
            .message = ffmpeg_error_string(ffmpeg_result),
        };
    }

    MediaProbeInfo info;
    if (format.context->iformat != nullptr && format.context->iformat->name != nullptr) {
        info.format_name = format.context->iformat->name;
    }

    // Container duration is authoritative when libavformat has one. Keep it as
    // exact AV_TIME_BASE ticks; never round through double seconds here.
    if (format.context->duration != AV_NOPTS_VALUE && format.context->duration > 0) {
        info.duration = RationalTime{
            format.context->duration,
            Rational{1, AV_TIME_BASE},
        };
    }

    info.streams.reserve(format.context->nb_streams);
    std::optional<RationalTime> longest_stream_duration;

    for (unsigned int index = 0; index < format.context->nb_streams; ++index) {
        const AVStream* stream = format.context->streams[index];
        if (stream == nullptr || stream->codecpar == nullptr) {
            continue;
        }

        const AVCodecParameters* params = stream->codecpar;
        MediaStreamProbe observed;
        observed.index = static_cast<int>(index);
        observed.time_base = to_rational(stream->time_base);
        observed.duration = stream_duration(*stream);
        observed.frame_count = stream->nb_frames > 0 ? stream->nb_frames : 0;

        const char* codec_name = avcodec_get_name(params->codec_id);
        if (codec_name != nullptr) {
            observed.codec = codec_name;
        }

        if (observed.duration.has_value() &&
            (!longest_stream_duration.has_value() ||
             duration_less(*longest_stream_duration, *observed.duration))) {
            longest_stream_duration = observed.duration;
        }

        switch (params->codec_type) {
            case AVMEDIA_TYPE_VIDEO: {
                observed.kind = MediaStreamKind::Video;
                observed.width = params->width;
                observed.height = params->height;
                if (params->format >= 0) {
                    const char* pixel_format = av_get_pix_fmt_name(
                        static_cast<AVPixelFormat>(params->format));
                    if (pixel_format != nullptr) {
                        observed.pixel_format = pixel_format;
                    }
                }

                AVRational rate = stream->r_frame_rate;
                if (!valid_av_rational(rate)) {
                    rate = stream->avg_frame_rate;
                }
                observed.frame_rate = to_rational(rate);
                break;
            }
            case AVMEDIA_TYPE_AUDIO: {
                observed.kind = MediaStreamKind::Audio;
                observed.sample_rate = params->sample_rate;
                observed.channels = params->ch_layout.nb_channels;
                if (params->format >= 0) {
                    const char* sample_format = av_get_sample_fmt_name(
                        static_cast<AVSampleFormat>(params->format));
                    if (sample_format != nullptr) {
                        observed.sample_format = sample_format;
                    }
                }
                break;
            }
            default:
                observed.kind = MediaStreamKind::Other;
                break;
        }

        info.streams.push_back(std::move(observed));
    }

    // Deterministic fallback for muxers that omit container duration: use the
    // longest valid stream duration. If no trustworthy duration exists, keep
    // the optional empty rather than fabricating zero/NaN metadata.
    if (!info.duration.has_value()) {
        info.duration = longest_stream_duration;
    }

    return info;
#endif
}

} // namespace chronon3d::media::video
