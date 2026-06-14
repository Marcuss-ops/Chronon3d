#include "video_sink_encoders.hpp"
#include "ffmpeg_pipe_encoder.hpp"
#include <chronon3d/media/frame_conversion/frame_converter.hpp>

namespace chronon3d::cli {

// ── Shared conversion helper ────────────────────────────────────────────────
// Returns the number of bytes written to dst (0 on failure).

namespace {

size_t convert_to_selected_format(
    const Framebuffer& fb,
    uint8_t* dst,
    int width,
    int height,
    PipePixelFormat input_format,
    const color::OutputTransformOptions& color_transform)
{
    switch (input_format) {
        case PipePixelFormat::YUV420P: {
            const size_t y_size = static_cast<size_t>(width) * height;
            const size_t uv_size = y_size / 4u;
            const auto res = video::convert_frame_tight(
                fb,
                dst,                  // Y plane
                dst + y_size,         // U plane
                dst + y_size + uv_size, // V plane
                nullptr,              // UV interleaved (not used)
                width, height,
                video::EncoderPixelFormat::YUV420P,
                color_transform.apply_gamma);
            return res.success ? y_size + uv_size * 2 : 0;
        }
        case PipePixelFormat::NV12: {
            const size_t y_size = static_cast<size_t>(width) * height;
            const auto res = video::convert_frame_tight(
                fb,
                dst,                  // Y plane
                nullptr,              // U (not used for NV12)
                nullptr,              // V (not used for NV12)
                dst + y_size,         // UV interleaved
                width, height,
                video::EncoderPixelFormat::NV12,
                color_transform.apply_gamma);
            return res.success ? y_size + y_size / 2u : 0;
        }
        case PipePixelFormat::RGBA:
        default: {
            // video::convert_frame_tight with RGB24 outputs 3 bytes/pixel (no alpha).
            // The buffer is sized for 4 bytes/pixel (RGBA) for API compatibility,
            // but we track the actual converted byte count.
            const auto res = video::convert_frame_tight(
                fb,
                dst,                  // packed RGB output
                nullptr, nullptr, nullptr,
                width, height,
                video::EncoderPixelFormat::RGB24,
                color_transform.apply_gamma);
            return res.success ? static_cast<size_t>(width) * height * 3u : 0;
        }
    }
}

} // anonymous namespace

// ── NullConvertEncoder ─────────────────────────────────────────────────────

bool NullConvertEncoder::open(const FfmpegPipeOptions& options) {
    options_ = options;
    frames_written_ = 0;

    const size_t bytes =
        options.input_format == PipePixelFormat::RGBA
            ? static_cast<size_t>(options.width) * options.height * 4u
            : static_cast<size_t>(options.width) * options.height * 3u / 2u;
    buffer_.resize(bytes);
    return true;
}

bool NullConvertEncoder::write_frame(const Framebuffer& fb) {
    const auto t0 = profiling::now();

    const size_t converted_bytes = convert_to_selected_format(
        fb, buffer_.data(),
        options_.width, options_.height,
        options_.input_format,
        options_.color_transform);

    const auto t1 = profiling::now();
    const uint64_t ms = static_cast<uint64_t>(profiling::duration_ms(t0, t1));

    if (counters_) {
        counters_->video_conversion_ms.fetch_add(ms, std::memory_order_relaxed);
        counters_->frame_conversion_copy_ms.fetch_add(ms, std::memory_order_relaxed);
        counters_->video_frames_submitted.fetch_add(1, std::memory_order_relaxed);
        counters_->video_frames_converted.fetch_add(1, std::memory_order_relaxed);
    }

    if (converted_bytes == 0) return false;

    ++frames_written_;
    return true;
}

} // namespace chronon3d::cli
