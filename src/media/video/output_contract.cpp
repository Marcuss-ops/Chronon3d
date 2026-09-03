#include <chronon3d/media/video/output_contract.hpp>

#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

#include <cmath>
#include <filesystem>
#include <string>
#include <string_view>

#if defined(CHRONON3D_ENABLE_NATIVE_FFMPEG)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/pixdesc.h>
#include <libavutil/rational.h>
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

std::string ffmpeg_error_string(int error_code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    if (av_strerror(error_code, buffer, sizeof(buffer)) < 0) {
        return "FFmpeg error " + std::to_string(error_code);
    }
    return buffer;
}

double rate_to_double(AVRational rate) {
    if (rate.num <= 0 || rate.den <= 0) {
        return 0.0;
    }
    return av_q2d(rate);
}

double stream_duration_seconds(const AVStream& stream) {
    if (stream.duration == AV_NOPTS_VALUE || stream.duration <= 0 ||
        stream.time_base.num <= 0 || stream.time_base.den <= 0) {
        return 0.0;
    }
    return static_cast<double>(stream.duration) * av_q2d(stream.time_base);
}

#endif

} // namespace

Result<OutputContract, std::string> resolve_output_contract(
    std::string_view profile_id) {
    if (profile_id == "youtube_overlay_v1") {
        OutputContract contract;
        contract.width = 1920;
        contract.height = 1080;
        contract.fps = chronon3d::FrameRate{24, 1};
        contract.video_codec = "h264";
        contract.pixel_format = "yuv420p";
        contract.audio_required = true;
        contract.audio_streams = 1;
        contract.frame_count = -1;
        return contract;
    }
    return std::string("unknown output profile: ") + std::string(profile_id);
}

OutputVerificationResult verify_output_contract(
    const std::filesystem::path& artifact,
    const OutputContract& contract) {
    OutputVerificationResult result;

    std::error_code ec;
    if (!std::filesystem::is_regular_file(artifact, ec) ||
        std::filesystem::file_size(artifact, ec) == 0) {
        result.failure = "artifact missing or empty: " + artifact.string();
        return result;
    }

#if !defined(CHRONON3D_ENABLE_NATIVE_FFMPEG)
    // Keep lean/pipe-only builds free of a new mandatory libav dependency.
    // Verification fails closed instead of falling back to an ffprobe process.
    result.ffprobe_missing = true;
    result.failure =
        "libavformat verification unavailable: build with "
        "CHRONON3D_ENABLE_NATIVE_FFMPEG=ON";
    return result;
#else
    // Probe the final container in-process. This intentionally replaces the
    // historical ffprobe subprocess + temporary JSON file path.
    const auto probe_t0 = profiling::now();
    FormatContextGuard format;
    const std::string artifact_path = artifact.string();
    int ffmpeg_result =
        avformat_open_input(&format.context, artifact_path.c_str(), nullptr, nullptr);
    if (ffmpeg_result < 0) {
        result.ffprobe_ms = profiling::elapsed_ms(probe_t0);
        result.failure = "libavformat rejected artifact: " +
                         ffmpeg_error_string(ffmpeg_result);
        return result;
    }

    ffmpeg_result = avformat_find_stream_info(format.context, nullptr);
    result.ffprobe_ms = profiling::elapsed_ms(probe_t0);
    if (ffmpeg_result < 0) {
        result.failure = "libavformat could not read stream info: " +
                         ffmpeg_error_string(ffmpeg_result);
        return result;
    }

    std::size_t video_streams = 0;
    std::size_t audio_streams = 0;
    AVStream* video_stream = nullptr;
    for (unsigned int index = 0; index < format.context->nb_streams; ++index) {
        AVStream* stream = format.context->streams[index];
        if (stream == nullptr || stream->codecpar == nullptr) {
            continue;
        }
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            ++video_streams;
            if (video_stream == nullptr) {
                video_stream = stream;
            }
        } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            ++audio_streams;
        }
    }
    result.audio_streams = audio_streams;

    if (video_stream != nullptr) {
        const AVCodecParameters* video_params = video_stream->codecpar;
        result.width = video_params->width;
        result.height = video_params->height;

        const char* codec_name = avcodec_get_name(video_params->codec_id);
        if (codec_name != nullptr) {
            result.video_codec = codec_name;
        }

        if (video_params->format >= 0) {
            const char* pixel_format = av_get_pix_fmt_name(
                static_cast<AVPixelFormat>(video_params->format));
            if (pixel_format != nullptr) {
                result.pixel_format = pixel_format;
            }
        }

        result.fps = rate_to_double(video_stream->r_frame_rate);
        if (result.fps <= 0.0) {
            result.fps = rate_to_double(video_stream->avg_frame_rate);
        }

        result.duration_seconds = stream_duration_seconds(*video_stream);
        if (video_stream->nb_frames > 0) {
            result.frame_count = video_stream->nb_frames;
        }
    }

    // Some muxers leave per-stream duration unset while providing a valid
    // container duration. Preserve the verifier's conservative fallback.
    if (result.duration_seconds <= 0.0 &&
        format.context->duration != AV_NOPTS_VALUE &&
        format.context->duration > 0) {
        result.duration_seconds =
            static_cast<double>(format.context->duration) /
            static_cast<double>(AV_TIME_BASE);
    }

    if (result.frame_count <= 0 &&
        result.duration_seconds > 0.0 && result.fps > 0.0) {
        // H.264 MP4 commonly omits nb_frames; derive from duration × fps.
        result.frame_count = static_cast<std::int64_t>(
            std::llround(result.duration_seconds * result.fps));
    }

    // ── Structural verdict (`passed`) ─────────────────────────────────────
    if (video_streams != 1) {
        result.failure = "expected 1 video stream, found " +
                         std::to_string(video_streams);
        return result;
    }
    if (result.width != contract.width || result.height != contract.height) {
        result.failure = "resolution mismatch: got " +
                         std::to_string(result.width) + "x" +
                         std::to_string(result.height) + ", expected " +
                         std::to_string(contract.width) + "x" +
                         std::to_string(contract.height);
        return result;
    }
    const double expected_fps = contract.fps.fps();
    if (result.fps <= 0.0 ||
        std::abs(result.fps - expected_fps) > 0.05) {
        result.failure = "frame rate mismatch: got " +
                         std::to_string(result.fps) + ", expected " +
                         std::to_string(expected_fps);
        return result;
    }
    if (result.duration_seconds <= 0.0) {
        result.failure = "duration is not positive";
        return result;
    }

    // ── Media contract (`copy_eligible`) ──────────────────────────────────
    result.passed = true;

    // Compute the SHA-256 digest for every decodable artifact (the durable
    // content fingerprint used for reporting and copy eligibility). It runs
    // before the media-contract verdict so a contract mismatch still reports
    // the digest instead of an empty placeholder.
    const auto sha256_t0 = profiling::now();
    const auto digest = chronon3d::assets::sha256_file(artifact);
    result.sha256_ms = profiling::elapsed_ms(sha256_t0);
    if (!digest) {
        result.passed = false;
        result.failure = "cannot compute SHA-256 for artifact";
        return result;
    }
    result.sha256 = digest->hex();

    bool contract_ok = true;
    if (result.video_codec != contract.video_codec) {
        contract_ok = false;
        result.failure = "codec mismatch: got " + result.video_codec +
                         ", expected " + contract.video_codec;
    } else if (result.pixel_format != contract.pixel_format) {
        contract_ok = false;
        result.failure = "pixel format mismatch: got " + result.pixel_format +
                         ", expected " + contract.pixel_format;
    } else if (contract.audio_required &&
               audio_streams != contract.audio_streams) {
        contract_ok = false;
        result.failure = "audio stream mismatch: got " +
                         std::to_string(audio_streams) + ", expected " +
                         std::to_string(contract.audio_streams);
    } else if (contract.frame_count > 0 &&
               std::abs(result.frame_count - contract.frame_count) > 1) {
        contract_ok = false;
        result.failure = "frame count mismatch: got " +
                         std::to_string(result.frame_count) + ", expected " +
                         std::to_string(contract.frame_count);
    } else if (!contract.expected_sha256.empty() &&
               result.sha256 != contract.expected_sha256) {
        contract_ok = false;
        result.failure = "SHA-256 mismatch: got " + result.sha256 +
                         ", expected " + contract.expected_sha256;
    }

    if (!contract_ok) {
        return result;  // passed=true, copy_eligible=false, sha256 populated
    }

    result.copy_eligible = true;
    return result;
#endif
}

} // namespace chronon3d::media::video
