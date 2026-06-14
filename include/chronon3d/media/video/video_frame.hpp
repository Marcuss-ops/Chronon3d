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

// ==========================================================================
//  Frame validation helpers (shared contract between RawVideoSink and
//  FfmpegPipeSink).
// ==========================================================================

/// Validate that a planar YUV420P frame view has valid dimensions and strides.
/// Returns true if all checks pass.
[[nodiscard]] inline bool validate_planar_frame(int width, int height,
                                                  std::size_t y_stride,
                                                  std::size_t u_stride,
                                                  std::size_t v_stride) noexcept {
    if (width <= 0 || height <= 0) return false;
    if (width % 2 != 0 || height % 2 != 0) return false;  // YUV420P requires even dims
    const std::size_t y_row  = static_cast<std::size_t>(width);
    const std::size_t uv_row = static_cast<std::size_t>(width / 2);
    const std::size_t ys = (y_stride > 0) ? y_stride : y_row;
    const std::size_t us = (u_stride > 0) ? u_stride : uv_row;
    const std::size_t vs = (v_stride > 0) ? v_stride : uv_row;
    return ys >= y_row && us >= uv_row && vs >= uv_row;
}

/// Validate that a biplanar NV12 frame view has valid dimensions and strides.
/// Returns true if all checks pass.
[[nodiscard]] inline bool validate_biplanar_frame(int width, int height,
                                                    std::size_t y_stride,
                                                    std::size_t uv_stride) noexcept {
    if (width <= 0 || height <= 0) return false;
    if (width % 2 != 0 || height % 2 != 0) return false;  // NV12 requires even dims
    const std::size_t y_row  = static_cast<std::size_t>(width);
    const std::size_t uv_row = static_cast<std::size_t>(width);
    const std::size_t ys  = (y_stride > 0)  ? y_stride  : y_row;
    const std::size_t uvs = (uv_stride > 0) ? uv_stride : uv_row;
    return ys >= y_row && uvs >= uv_row;
}

/// Validate that a packed frame stride is at least the tight row bytes.
/// For YUV planar formats, only stride==0 (tight packing) is allowed in
/// packed mode — callers with padded planar data must use the separate
/// planar/biplanar submit overloads.
[[nodiscard]] inline bool validate_packed_stride(PixelFormat fmt, int width,
                                                   std::size_t stride_bytes) noexcept {
    if (width <= 0) return false;
    // Planar formats in packed mode: only tight packing is supported.
    if (fmt == PixelFormat::YUV420P || fmt == PixelFormat::NV12) {
        return stride_bytes == 0;
    }
    if (stride_bytes == 0) return true;  // tight packing is always valid
    const uint64_t tight_row = frame_buffer_size(fmt, width, 1);
    return stride_bytes >= tight_row;
}

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
