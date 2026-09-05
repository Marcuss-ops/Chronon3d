#pragma once

// ---------------------------------------------------------------------------
// video_frame.hpp — Lightweight frame view type for the VideoSink pipeline.
//
// VideoFrameView is a non-owning view of a single video frame's pixel data.
// Pixel taxonomy is owned by runtime::FrameFormat; this media boundary only
// exposes the canonical runtime::PixelFormat under its historical local name.
// ---------------------------------------------------------------------------

#include <chronon3d/runtime/frame_format.hpp>
#include <cstddef>
#include <cstdint>

namespace chronon3d::media::video {

/// Maximum frame dimension in any single axis (P2-B memory budget).
inline constexpr int kMaxFrameDimension = 16384;

/// Maximum total pixel count (width × height) to prevent unbounded memory.
inline constexpr int64_t kMaxPixelCount = 268435456LL;

/// Source-compatible spelling for the one canonical pixel taxonomy.
using PixelFormat = runtime::PixelFormat;

/// Returns the total number of bytes required for a single frame of the
/// given format and dimensions, assuming tight (no-padding) row strides.
/// Returns 0 for unsupported formats or non-positive dimensions.
[[nodiscard]] inline uint64_t frame_buffer_size(PixelFormat fmt, int width, int height) noexcept {
    if (width <= 0 || height <= 0) return 0;
    if (width > kMaxFrameDimension || height > kMaxFrameDimension) return 0;

    switch (fmt) {
        case PixelFormat::RGBA8:
        case PixelFormat::YUV420P:
        case PixelFormat::NV12:
        case PixelFormat::RGB24:
            return static_cast<uint64_t>(runtime::tight_surface_bytes(
                fmt, static_cast<std::uint32_t>(width),
                static_cast<std::uint32_t>(height)));
        default:
            return 0;
    }
}

/// Non-owning view of a single video frame's pixel data.
///
/// The caller guarantees that the `data` pointer remains valid for the
/// duration of the `VideoSink::submit()` call. After submit() returns the sink
/// must NOT access the data pointer.
struct VideoFrameView {
    const void* data{nullptr};
    std::size_t stride_bytes{0};
    int width{0};
    int height{0};
    PixelFormat pixel_format{PixelFormat::RGBA8};
    std::int64_t pts{0};

    /// Reconstruct the complete boundary semantics in the canonical format
    /// type. Legacy VideoFrameView does not carry independent color metadata;
    /// callers requiring non-default color interpretation must use the richer
    /// conversion/output contracts rather than inventing another enum set.
    [[nodiscard]] constexpr runtime::FrameFormat frame_format() const noexcept {
        return runtime::make_frame_format(pixel_format);
    }
};

// ==========================================================================
// Frame validation helpers (shared contract between RawVideoSink and
// NativeAvSink).
// ==========================================================================

[[nodiscard]] inline bool validate_planar_frame(int width, int height,
                                                  std::size_t y_stride,
                                                  std::size_t u_stride,
                                                  std::size_t v_stride) noexcept {
    if (width <= 0 || height <= 0) return false;
    if (width % 2 != 0 || height % 2 != 0) return false;
    const std::size_t y_row  = static_cast<std::size_t>(width);
    const std::size_t uv_row = static_cast<std::size_t>(width / 2);
    const std::size_t ys = (y_stride > 0) ? y_stride : y_row;
    const std::size_t us = (u_stride > 0) ? u_stride : uv_row;
    const std::size_t vs = (v_stride > 0) ? v_stride : uv_row;
    return ys >= y_row && us >= uv_row && vs >= uv_row;
}

[[nodiscard]] inline bool validate_biplanar_frame(int width, int height,
                                                    std::size_t y_stride,
                                                    std::size_t uv_stride) noexcept {
    if (width <= 0 || height <= 0) return false;
    if (width % 2 != 0 || height % 2 != 0) return false;
    const std::size_t y_row  = static_cast<std::size_t>(width);
    const std::size_t uv_row = static_cast<std::size_t>(width);
    const std::size_t ys  = (y_stride > 0)  ? y_stride  : y_row;
    const std::size_t uvs = (uv_stride > 0) ? uv_stride : uv_row;
    return ys >= y_row && uvs >= uv_row;
}

[[nodiscard]] inline bool validate_packed_stride(PixelFormat fmt, int width,
                                                   std::size_t stride_bytes) noexcept {
    if (width <= 0) return false;
    if (fmt == PixelFormat::YUV420P || fmt == PixelFormat::NV12) {
        return stride_bytes == 0;
    }
    if (stride_bytes == 0) return true;
    const uint64_t tight_row = frame_buffer_size(fmt, width, 1);
    return tight_row != 0 && stride_bytes >= tight_row;
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
