// ---------------------------------------------------------------------------
// video_config_validator.cpp — Centralised VideoSinkConfig validation.
//
// Single point of truth for all configuration constraints.  Every sink
// implementation calls validate_video_sink_config() at the top of open()
// so that invalid configurations are rejected promptly and uniformly.
// ---------------------------------------------------------------------------

#include <chronon3d/media/video/video_config.hpp>

#include <cstdint>
#include <string>

namespace chronon3d::media::video {

// ============================================================================
//  Local helpers
// ============================================================================

namespace {

/// Check that the submitted frame format is compatible with the stream geometry.
/// YUV420P and NV12 require even width and height.
[[nodiscard]] const char* check_yuv_dimensions(
    PixelFormat fmt, int w, int h) noexcept
{
    if ((fmt == PixelFormat::YUV420P || fmt == PixelFormat::NV12) &&
        (w % 2 != 0 || h % 2 != 0)) {
        return "YUV420P and NV12 require even width and height";
    }
    return nullptr;  // no error
}

/// Check codec / container compatibility.
[[nodiscard]] const char* check_codec_container(
    VideoCodec codec, VideoContainer container) noexcept
{
    // WebM only supports VP9 and AV1.
    if (container == VideoContainer::WebM) {
        if (codec == VideoCodec::H264) {
            return "WebM container does not support H.264 codec";
        }
        if (codec == VideoCodec::H265) {
            return "WebM container does not support H.265/HEVC codec";
        }
        if (codec == VideoCodec::Uncompressed) {
            return "WebM container does not support uncompressed/raw codec";
        }
        // VP9 and AV1 are fine in WebM.
        // Auto → will resolve to VP9 typically, acceptable.
        return nullptr;
    }

    // Raw container requires Uncompressed codec.
    if (container == VideoContainer::Raw) {
        if (codec != VideoCodec::Uncompressed) {
            return "Raw container requires Uncompressed codec; "
                   "use VideoCodec::Uncompressed";
        }
        return nullptr;
    }

    // MP4 with VP9 or AV1 — technically possible but uncommon;
    // many decoders don't expect these in MP4.  Warn via soft reject.
    if (container == VideoContainer::Mp4) {
        if (codec == VideoCodec::VP9) {
            return "MP4 container with VP9 codec is unusual and may not "
                   "be playable everywhere; use MKV or WebM instead";
        }
        if (codec == VideoCodec::AV1) {
            return "MP4 container with AV1 codec requires a recent player; "
                   "consider MKV instead";
        }
    }

    // MKV + Uncompressed: not supported by FfmpegPipeSink pipe.
    if (codec == VideoCodec::Uncompressed && container != VideoContainer::Raw) {
        return "Uncompressed codec requires Raw container";
    }

    return nullptr;
}

} // anonymous namespace

// ============================================================================
//  validate_video_sink_config() — main entry point
// ============================================================================

ValidationResult validate_video_sink_config(
    const VideoSinkConfig& config) noexcept
{
    // ── Stream validation ──────────────────────────────────────────────

    const auto& stream = config.stream;

    if (stream.width <= 0) {
        return {false, "stream.width must be > 0"};
    }
    if (stream.height <= 0) {
        return {false, "stream.height must be > 0"};
    }
    if (stream.width > kMaxFrameDimension) {
        return {false, "stream.width exceeds max dimension (16384)"};
    }
    if (stream.height > kMaxFrameDimension) {
        return {false, "stream.height exceeds max dimension (16384)"};
    }
    // Overflow guard: width*height must not exceed max pixel count.
    {
        const int64_t pixels = static_cast<int64_t>(stream.width)
                             * static_cast<int64_t>(stream.height);
        if (pixels > kMaxPixelCount) {
            return {false, "width*height exceeds max pixel count (268M)"};
        }
    }

    if (stream.frame_rate_num <= 0) {
        return {false, "stream.frame_rate_num must be > 0"};
    }
    if (stream.frame_rate_den <= 0) {
        return {false, "stream.frame_rate_den must be > 0"};
    }

    if (const char* err = check_yuv_dimensions(
            stream.submitted_format, stream.width, stream.height)) {
        return {false, err};
    }

    // ── Encoder validation ─────────────────────────────────────────────

    const auto& enc = config.encoder;

    if (enc.crf < -1 || enc.crf > 51) {
        return {false, "encoder.crf must be in [-1, 51] (-1 = codec default)"};
    }

    if (enc.bitrate < 0) {
        return {false, "encoder.bitrate must be >= 0 (0 = let encoder choose)"};
    }

    // Resolve Auto codec before validating — the validator must check the
    // actual codec that will be used, not the unresolved placeholder.
    const auto resolved_codec = resolve_auto_codec(
        enc.codec, config.output.container);
    if (const char* err = check_codec_container(
            resolved_codec, config.output.container)) {
        return {false, err};
    }

    // ── Output validation ──────────────────────────────────────────────

    const auto& output = config.output;

    if (output.output_path.empty()) {
        return {false, "output.output_path must not be empty"};
    }

    // ── Transport validation ───────────────────────────────────────────

    const auto& transport = config.transport;

    // The current implementations are synchronous only.
    if (transport.asynchronous) {
        return {false, "transport.asynchronous=true is not yet implemented; "
                       "set transport.asynchronous=false for synchronous mode"};
    }

    // io_uring is not wired in the current sinks — warn and fall back.
    // (We don't reject because the adapter already handles fallback.)
    if (transport.write_timeout.count() < 0) {
        return {false, "transport.write_timeout must be >= 0ms"};
    }

    // ── All checks passed ──────────────────────────────────────────────

    return {true, {}};
}

} // namespace chronon3d::media::video
