#pragma once

// ---------------------------------------------------------------------------
// video_config.hpp — Configuration structs for the VideoSink pipeline.
//
// Four separate configuration levels that are composed into a single
// VideoSinkConfig.  Each level can be constructed independently from
// CLI arguments, a JSON config file, or embedded defaults.
//
// Layout (all defaults are valid for a basic H.264 MP4 export):
//   VideoSinkConfig
//     ├── stream     (geometry, frame rate, input pixel format)
//     ├── encoder    (codec, quality, preset, output pixel format)
//     ├── transport  (queue depth, async, io_uring)
//     └── output     (file path, container)
// ---------------------------------------------------------------------------

#include <chronon3d/media/video/video_frame.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace chronon3d::media::video {

// ==========================================================================
//  Frame-level stream configuration
// ==========================================================================

/// Describes the geometry and timing of the video stream.
struct VideoStreamConfig {
    /// Frame width in pixels (must be > 0, even for YUV420P/NV12).
    int width{0};

    /// Frame height in pixels (must be > 0, even for YUV420P/NV12).
    int height{0};

    /// Frame rate as a rational: numerator / denominator.
    /// Default 30/1 = 30 fps.
    int frame_rate_num{30};
    int frame_rate_den{1};

    /// Pixel format of the frames supplied to VideoSink::submit().
    /// This is the format the caller provides; the sink validates
    /// all submitted frames against this contract.
    PixelFormat submitted_format{PixelFormat::RGBA8};
};

// ==========================================================================
//  Encoder quality / codec configuration
// ==========================================================================

/// Video codec identifier.
enum class VideoCodec : uint8_t {
    /// Let the sink choose the best codec for the container.
    Auto = 0,

    /// H.264 / AVC (most compatible, good quality/size).
    H264 = 1,

    /// H.265 / HEVC (better compression, slower, less compatible).
    H265 = 2,

    /// VP9 (open, good quality, slower encoding).
    VP9 = 3,

    /// AV1 (best compression, very slow encoding, limited compat).
    AV1 = 4,

    /// Uncompressed / raw (no encoding — used with RawVideoSink).
    Uncompressed = 5,
};

/// Video container format.
enum class VideoContainer : uint8_t {
    /// MP4 (most compatible, H.264/HEVC).
    Mp4 = 0,

    /// Matroska (more flexible codec support).
    Mkv = 1,

    /// WebM (VP9/AV1 only).
    WebM = 2,

    /// Raw file (no container — used with RawVideoSink).
    Raw = 3,
};

/// Encoder quality / codec settings.
/// Selected at planning time and read-only during encoding.
struct VideoEncoderConfig {
    /// Target codec.  Auto lets the implementation choose.
    VideoCodec codec{VideoCodec::Auto};

    /// Encoder preset string (e.g. "superfast", "medium", "slow").
    /// Empty = implementation default.
    std::string preset;

    /// Encoder tune parameter (e.g. "zerolatency", "film", "grain").
    std::optional<std::string> tune;

    /// Target bitrate in bits/second.  0 = let encoder choose (CRF/VBR).
    std::int64_t bitrate{0};

    /// CRF / quality value (0–51, lower = better quality).
    /// -1 = use codec default.
    int crf{-1};

    /// Pixel format for the encoded output stream (e.g. YUV420P).
    /// This is the pixel format written into the final container, distinct
    /// from the submitted frame format (VideoStreamConfig::submitted_format).
    PixelFormat encoded_pixel_format{PixelFormat::YUV420P};

    /// Apply sRGB gamma curve during conversion (default true).
    bool apply_gamma{true};
};

// ==========================================================================
//  Transport / I/O configuration (pipe-specific)
// ==========================================================================

/// Controls the internal transport between the producer and the encoder.
struct PipeTransportConfig {
    /// Maximum number of frames buffered in the internal queue.
    /// Higher values smooth out render-time spikes but use more memory.
    std::size_t queue_capacity{4};

    /// When true, frame submission returns immediately after enqueuing.
    /// When false, submit() blocks until the frame is fully written.
    bool asynchronous{true};

    /// Use Linux io_uring for asynchronous I/O (experimental).
    /// Ignored on non-Linux platforms or when io_uring is unavailable.
    bool use_io_uring{false};

    /// Maximum time to wait for a write to complete (per-frame).
    /// 0 = no timeout (block indefinitely).
    std::chrono::milliseconds write_timeout{30000};
};

// ==========================================================================
//  Output destination configuration
// ==========================================================================

/// Describes the output file and container.
struct VideoOutputConfig {
    /// Output file path (e.g. "/tmp/output.mp4").
    std::filesystem::path output_path;

    /// Container format.  Mp4 is the default.
    VideoContainer container{VideoContainer::Mp4};

    /// Overwrite existing file without prompting.
    bool overwrite{true};
};

// ==========================================================================
//  Complete sink configuration
// ==========================================================================

/// Complete configuration for a VideoSink.
/// Composed of the four sub-configs above plus a human-readable label.
struct VideoSinkConfig {
    /// Stream geometry and frame rate.
    VideoStreamConfig stream;

    /// Encoder codec, quality, and output format.
    VideoEncoderConfig encoder;

    /// Transport/queue behaviour (pipe-specific).
    PipeTransportConfig transport;

    /// Output file destination.
    VideoOutputConfig output;

    /// Optional human-readable label for diagnostics.
    std::string label;
};

} // namespace chronon3d::media::video
