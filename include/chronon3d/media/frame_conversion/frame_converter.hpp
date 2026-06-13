#pragma once

// ---------------------------------------------------------------------------
// frame_converter.hpp — Unified RGBA → encoder pixel format conversion API.
//
// Design goals:
//  - Single entry point: convert_frame(ConvertFrameRequest) → ConvertFrameResult
//  - Format-agnostic caller: the encoder passes a request and gets timing/flags
//  - Dispatcher is decoupled from the encoder, so SIMD backends can be swapped
//    (scalar, Highway AVX2, Highway AVX-512, NEON) without touching the encoder.
//
// Supported formats:
//  - YUV420P  (planar, 3 separate planes: Y, U, V)
//  - NV12     (biplanar: Y plane + interleaved UV plane)
//  - RGB24    (packed, no alpha, drop A channel)
//  - RGBA8    (packed, 4 bytes/pixel, preserves alpha channel)
// ---------------------------------------------------------------------------

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/color/output_transform.hpp>
#include <cstddef>
#include <cstdint>

namespace chronon3d::video {

/// Target pixel format for the hardware or software encoder.
enum class EncoderPixelFormat {
    /// Planar 4:2:0 Y′CbCr — 3 planes: Y (w×h) + U (w/2×h/2) + V (w/2×h/2).
    YUV420P,

    /// Biplanar 4:2:0 Y′CbCr — Y plane + interleaved UV (w×h/2).
    NV12,

    /// Packed 8-bit RGB, 3 bytes/pixel, no alpha — used with libx264rgb.
    RGB24,

    /// Packed 8-bit RGBA, 4 bytes/pixel — native renderer output format.
    RGBA8,
};

/// Full description of a single frame conversion request.
/// All pointers must remain valid for the duration of convert_frame().
struct ConvertFrameRequest {
    const Framebuffer& src;

    // ── Output planes ──────────────────────────────────────────────────
    // YUV420P: dst_y, dst_u, dst_v must be non-null; dst_uv ignored.
    // NV12   : dst_y, dst_uv must be non-null; dst_u, dst_v ignored.
    // RGB24  : dst_y used as the packed RGB output; others ignored.
    uint8_t* dst_y{nullptr};
    uint8_t* dst_u{nullptr};    // YUV420P only
    uint8_t* dst_v{nullptr};    // YUV420P only
    uint8_t* dst_uv{nullptr};   // NV12 only

    // Strides in bytes (width in bytes per row).
    // Set to width (Y) / (width/2) (U/V) if there is no padding.
    int dst_stride_y{0};
    int dst_stride_uv{0};   // NV12 UV stride = width (interleaved pairs)
    int dst_stride_u{0};    // YUV420P U stride = width/2
    int dst_stride_v{0};    // YUV420P V stride = width/2
    int color_matrix{0};    // 0=BT709, 1=BT601, 2=BT2020 (future use)

    int width{0};
    int height{0};
    EncoderPixelFormat format{EncoderPixelFormat::YUV420P};

    /// Apply sRGB gamma curve to linear-light source pixels.
    /// Set false only when the Framebuffer already carries gamma-encoded data.
    bool apply_gamma{true};
};

/// Metrics returned by a single convert_frame() call.
struct ConvertFrameResult {
    bool  success{false};
    bool  used_simd{true};      ///< False only if scalar fallback was taken
    uint64_t conversion_ns{0};  ///< Wall-clock nanoseconds spent inside the kernel
};

/// Dispatch a frame conversion request to the appropriate SIMD kernel.
/// Thread-safe: all state is passed through the request struct.
/// Throws std::invalid_argument for unknown format or null required pointers.
ConvertFrameResult convert_frame(const ConvertFrameRequest& req);

/// Convenience overload: compute tight strides from width, fill them automatically.
/// dst_y / dst_u / dst_v / dst_uv must still be caller-provided.
ConvertFrameResult convert_frame_tight(
    const Framebuffer& src,
    uint8_t* dst_y,
    uint8_t* dst_u,
    uint8_t* dst_v,
    uint8_t* dst_uv,
    int width, int height,
    EncoderPixelFormat format,
    bool apply_gamma = true);

} // namespace chronon3d::video
