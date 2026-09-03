#include <chronon3d/media/video/output_contract.hpp>

#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/media/video/media_probe.hpp>

#include <cmath>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>

namespace chronon3d::media::video {
namespace {

[[nodiscard]] double rational_to_double(Rational value) {
    if (value.numerator <= 0 || value.denominator <= 0) {
        return 0.0;
    }
    return static_cast<double>(value.numerator) /
           static_cast<double>(value.denominator);
}

[[nodiscard]] std::int64_t derive_frame_count_exact(
    RationalTime duration,
    Rational frame_rate) {
    if (duration.ticks() <= 0 || duration.time_base.numerator <= 0 ||
        duration.time_base.denominator <= 0 || frame_rate.numerator <= 0 ||
        frame_rate.denominator <= 0) {
        return 0;
    }

    // frames = duration_ticks * duration_time_base * frames_per_second.
    // Round to the nearest whole frame, matching the verifier's historical
    // llround policy without converting the authoritative media time to double.
    const __int128 numerator =
        static_cast<__int128>(duration.ticks()) *
        static_cast<__int128>(duration.time_base.numerator) *
        static_cast<__int128>(frame_rate.numerator);
    const __int128 denominator =
        static_cast<__int128>(duration.time_base.denominator) *
        static_cast<__int128>(frame_rate.denominator);
    if (numerator <= 0 || denominator <= 0) {
        return 0;
    }

    const __int128 rounded = (numerator + denominator / 2) / denominator;
    if (rounded > static_cast<__int128>(std::numeric_limits<std::int64_t>::max())) {
        return 0;
    }
    return static_cast<std::int64_t>(rounded);
}

[[nodiscard]] std::string probe_failure(const MediaProbeError& error) {
    switch (error.code) {
        case MediaProbeErrorCode::BackendUnavailable:
            return error.message;
        case MediaProbeErrorCode::OpenInput:
            return "libavformat rejected artifact: " + error.message;
        case MediaProbeErrorCode::StreamInfo:
            return "libavformat could not read stream info: " + error.message;
    }
    return "libavformat probe failed: " + error.message;
}

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

    // All container/stream discovery goes through the canonical probe. The
    // verifier only interprets those facts against the output contract.
    const auto probe_t0 = profiling::now();
    auto probe = probe_media(artifact);
    result.ffprobe_ms = profiling::elapsed_ms(probe_t0);
    if (!probe) {
        const auto& error = probe.error();
        result.ffprobe_missing =
            error.code == MediaProbeErrorCode::BackendUnavailable;
        result.failure = probe_failure(error);
        return result;
    }

    const auto& media = probe.value();
    std::size_t video_streams = 0;
    std::size_t audio_streams = 0;
    const MediaStreamProbe* video_stream = nullptr;

    for (const auto& stream : media.streams) {
        if (stream.kind == MediaStreamKind::Video) {
            ++video_streams;
            if (video_stream == nullptr) {
                video_stream = &stream;
            }
        } else if (stream.kind == MediaStreamKind::Audio) {
            ++audio_streams;
        }
    }
    result.audio_streams = audio_streams;

    if (video_stream != nullptr) {
        result.width = video_stream->width;
        result.height = video_stream->height;
        result.video_codec = video_stream->codec;
        result.pixel_format = video_stream->pixel_format;
        result.fps = rational_to_double(video_stream->frame_rate);
        result.frame_count = video_stream->frame_count;
    }

    // The verifier describes the video artifact, so prefer the exact video
    // stream duration. Container duration is only a deterministic fallback for
    // muxers that omit per-stream duration (for example some fragmented files).
    const std::optional<RationalTime> video_duration =
        video_stream != nullptr && video_stream->duration.has_value()
            ? video_stream->duration
            : media.duration;

    if (video_duration.has_value()) {
        result.duration_seconds = video_duration->seconds();
    }

    if (result.frame_count <= 0 && video_duration.has_value() &&
        video_stream != nullptr) {
        result.frame_count = derive_frame_count_exact(
            *video_duration, video_stream->frame_rate);
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
}

} // namespace chronon3d::media::video
