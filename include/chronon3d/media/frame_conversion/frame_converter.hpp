#pragma once

// ---------------------------------------------------------------------------
// frame_converter.hpp — YUV / RGB frame conversion contract.
//
// PR1 refactor: replaces the previous ad-hoc int color_matrix + bool
// used_simd API with a typed, capability-aware contract.  Behavior is
// preserved for BT.601 / BT.709 callers; BT.2020 callers that previously
// silently fell back to BT.709 now correctly route to swscale and report
// FrameConversionBackend::Swscale.
// ---------------------------------------------------------------------------

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/color/output_transform.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>

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

/// Color matrix / chromaticity set used for YUV encoding.
enum class YuvMatrix {
    BT709,   ///< HDTV / modern SDR — default.
    BT601,   ///< SDTV / legacy.
    BT2020,  ///< UHDTV / HDR stage 1.
};

/// Color range of the YUV output samples.
enum class ColorRange {
    Limited,  ///< Y ∈ [16, 235], Cb/Cr ∈ [16, 240] — broadcast.
    Full,     ///< Y ∈ [0, 255], Cb/Cr ∈ [0, 255] — JPEG / ffmpeg "-pix_fmt yuvj*".
};

/// Backend that actually produced the converted framebuffer.
enum class FrameConversionBackend {
    HighwayDirect,   ///< PR4B: deprecated — removed from production.  Kept for API compat.
    Swscale,         ///< FFmpeg libswscale path (canonical YUV/RGB24/BT.2020 backend).
    Packed,          ///< Direct float→uint8 path for RGBA8.
    Unavailable,     ///< No backend could satisfy the request.
};

/// Categorical reason for a conversion failure.  `None` means success.
enum class ConversionError {
    None,
    OddDims,            ///< width or height is odd (4:2:0 requires even).
    NullPointer,        ///< A required plane pointer is null for the target format.
    UnsupportedMatrix,  ///< Backend cannot encode the requested YuvMatrix.
    UnsupportedRange,   ///< Backend cannot encode the requested ColorRange.
    UnsupportedFormat,  ///< Target EncoderPixelFormat is not handled.
    BackendError,       ///< Backend runtime failure (e.g., swscale returned 0).
};

/// Aggregated YUV plane pointers and per-plane byte strides.  Iteration
/// helpers and the dispatcher use this struct to remove the duplicated
/// pointer/stride plumbing that previously lived in three different TUs.
struct FramePlanes {
    uint8_t* y{nullptr};
    uint8_t* u{nullptr};     // YUV420P only.
    uint8_t* v{nullptr};     // YUV420P only.
    uint8_t* uv{nullptr};    // NV12 only — interleaved UV pairs.

    int stride_y{0};          // Bytes per row of the Y plane.
    int stride_u{0};          // Bytes per row of the U plane (YUV420P).
    int stride_v{0};          // Bytes per row of the V plane (YUV420P).
    int stride_uv{0};         // Bytes per row of the UV plane (NV12).
};

/// Single frame conversion request.  All dimensions, plane pointers,
/// matrix, range and gamma selection must be explicit — no implicit
/// defaults that contradict caller intent.
struct ConvertFrameRequest {
    const Framebuffer& src;
    FramePlanes planes;

    int width{0};
    int height{0};
    EncoderPixelFormat format{EncoderPixelFormat::YUV420P};

    YuvMatrix matrix{YuvMatrix::BT709};
    ColorRange range{ColorRange::Limited};
    bool apply_gamma{true};
};

/// Categorical result for a conversion attempt.
struct ConvertFrameResult {
    bool success{false};
    FrameConversionBackend backend{FrameConversionBackend::Unavailable};
    ConversionError error{ConversionError::None};
    uint64_t conversion_ns{0};
};

// ── Capability advertisement ──────────────────────────────────────────────

/// Per-build static capabilities of the available backends.
struct FrameConversionCapabilities {
    bool highway_direct{false};
    bool swscale{false};

    /// Bitmask of matrices supported by HighwayDirect (BT.2020 is intentionally excluded).
    uint8_t highway_direct_matrices{0};
};

/// Returns the capability snapshot for the current build (compile-time
/// switches resolved at first call).  Computed once and cached.
const FrameConversionCapabilities& frame_conversion_capabilities();

/// Pure selector that computes which backend SHOULD handle a request.
/// No fallback chain: returns `Unavailable` if no backend fits.
FrameConversionBackend select_backend(const ConvertFrameRequest& req);

// ── Validation + plane layout helpers ────────────────────────────────────

/// Validates dimensions, plane pointers, format/stride coherence and
/// matrix/range support for the global capabilities.  Returns None on
/// success, otherwise the matching ConversionError.
ConversionError validate_conversion_request(const ConvertFrameRequest& req);

/// Resolves the Y/U/V/UV planes from a contiguous packed buffer that
/// contains the full framebuffer layout for the requested format.  Returns
/// std::nullopt on size-mismatch / unsupported format.
std::optional<FramePlanes> resolve_frame_planes(
    uint8_t* packed_buffer, std::size_t packed_size,
    int width, int height, EncoderPixelFormat format);

/// Human-readable error string (for logs).
const char* conversion_error_to_string(ConversionError err);

// ── Dispatcher ───────────────────────────────────────────────────────────

ConvertFrameResult convert_frame(const ConvertFrameRequest& req);

/// Convenience overload accepting the unpacked plane layout.  Equivalent to
/// building a ConvertFrameRequest explicitly.  Use this when callers have
/// their own buffer pool instead of the contiguous packed layout.
ConvertFrameResult convert_frame_tight(
    const Framebuffer& src, FramePlanes planes,
    int width, int height, EncoderPixelFormat format,
    YuvMatrix matrix, ColorRange range,
    bool apply_gamma = true);

} // namespace chronon3d::video
