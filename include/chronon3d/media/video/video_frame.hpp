#pragma once

// ---------------------------------------------------------------------------
// video_frame.hpp — Lightweight frame view type for the VideoSink pipeline.
//
// VideoFrameView is a non-owning view of a single video frame's pixel data.
// It does NOT depend on Framebuffer, RenderGraph, or any rendering types.
// Any component that produces or consumes raw pixel data (renderer, decoder,
// converter, sink) can use this as the interchange type.
//
// Design:
//  - Lightweight: 5 pointers + 5 ints = ~56 bytes on x64.
//  - No ownership: the caller guarantees the data pointer remains valid
//    until submit() returns.
//  - Format-agnostic: PixelFormat enum covers all common encoder formats.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>

namespace chronon3d::media::video {

/// Pixel format of a VideoFrameView buffer.
/// This is a superset of the encoder-only formats in EncoderPixelFormat
/// and adds the native RGBA8 format used by the renderer output path.
enum class PixelFormat : uint8_t {
    /// 8-bit RGBA, 4 bytes/pixel, interleaved (native renderer output).
    RGBA8 = 0,

    /// Planar 4:2:0 Y′CbCr, 3 planes: Y (w×h) + U (w/2×h/2) + V (w/2×h/2).
    YUV420P = 1,

    /// Biplanar 4:2:0 Y′CbCr, 2 planes: Y (w×h) + interleaved UV (w×h/2).
    NV12 = 2,

    /// Packed 8-bit RGB, 3 bytes/pixel, no alpha channel.
    RGB24 = 3,
};

/// Returns the total number of bytes required for a single frame of the
/// given format and dimensions, assuming tight (no-padding) row strides.
/// Returns 0 for unknown formats or non-positive dimensions.
[[nodiscard]] inline uint64_t frame_buffer_size(PixelFormat fmt, int width, int height) noexcept {
    if (width <= 0 || height <= 0) return 0;
    const uint64_t w = static_cast<uint64_t>(width);
    const uint64_t h = static_cast<uint64_t>(height);

    switch (fmt) {
        case PixelFormat::RGBA8:  return w * h * 4;
        case PixelFormat::YUV420P: return w * h + (w / 2) * (h / 2) * 2;
        case PixelFormat::NV12:    return w * h + (w / 2) * (h / 2) * 2;
        case PixelFormat::RGB24:   return w * h * 3;
    }
    return 0;
}

/// Non-owning view of a single video frame's pixel data.
///
/// The caller guarantees that the `data` pointer remains valid for the
/// duration of the `VideoSink::submit()` call.  After submit() returns
/// the sink must NOT access the data pointer.
///
/// All dimensions are in pixels.  Stride is in bytes per row.
/// A stride of 0 means tight packing (width × bytes_per_pixel).
struct VideoFrameView {
    /// Pointer to the first byte of the first pixel row.
    /// For planar formats (YUV420P), this points to the Y plane start.
    /// The caller must separately provide U/V or UV plane pointers
    /// (or use the multi-plane overload).
    const void* data{nullptr};

    /// Row stride in bytes.  0 = tight (width × pixel_size).
    std::size_t stride_bytes{0};

    /// Frame width in pixels.
    int width{0};

    /// Frame height in pixels.
    int height{0};

    /// Pixel format of the data buffer.
    PixelFormat pixel_format{};

    /// Presentation timestamp in stream timebase units (e.g. frame number).
    std::int64_t pts{0};
};

/// Extended frame view for planar YUV420P data.
struct PlanarVideoFrameView {
    const void* y_data{nullptr};
    const void* u_data{nullptr};
    const void* v_data{nullptr};

    std::size_t y_stride{0};
    std::size_t u_stride{0};
    std::size_t v_stride{0};

    int width{0};
    int height{0};
    std::int64_t pts{0};
};

/// Extended frame view for biplanar NV12 data.
struct BiplanarVideoFrameView {
    const void* y_data{nullptr};
    const void* uv_data{nullptr};

    std::size_t y_stride{0};
    std::size_t uv_stride{0};

    int width{0};
    int height{0};
    std::int64_t pts{0};
};

} // namespace chronon3d::media::video
