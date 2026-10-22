#pragma once

// ---------------------------------------------------------------------------
// frame_converter.hpp — YUV / RGB frame conversion contract.
//
// Pixel/color taxonomy is owned exclusively by runtime::FrameFormat. The
// historical EncoderPixelFormat/YuvMatrix/ColorRange names below are aliases
// to that canonical authority so media conversion cannot invent a second set
// of enum values.
// ---------------------------------------------------------------------------

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/color/output_transform.hpp>
#include <chronon3d/runtime/frame_format.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace chronon3d::video {

// Source-compatible names for existing media call sites. These are aliases,
// not independent enum definitions; runtime::FrameFormat owns the semantics.
using EncoderPixelFormat = runtime::PixelFormat;
using YuvMatrix = runtime::ColorMatrix;
using ColorRange = runtime::ColorRange;

/// Backend that actually produced the converted framebuffer.
enum class FrameConversionBackend {
    HighwayDirect,   ///< PR4B: deprecated — removed from production. Kept for API compat.
    Swscale,         ///< FFmpeg libswscale path (canonical YUV/RGB24/BT.2020 backend).
    Packed,          ///< Direct float→uint8 path for RGBA8.
    Unavailable,     ///< No backend could satisfy the request.
};

/// Categorical reason for a conversion failure. `None` means success.
enum class ConversionError {
    None,
    OddDims,            ///< width or height is odd (4:2:0 requires even).
    NullPointer,        ///< Source pixels or a required target plane are null.
    UnsupportedMatrix,  ///< Backend cannot encode the requested ColorMatrix.
    UnsupportedRange,   ///< Backend cannot encode the requested ColorRange.
    UnsupportedFormat,  ///< Target PixelFormat is not handled.
    BackendError,       ///< Backend runtime failure (e.g., swscale returned 0).
    InvalidSource,      ///< Source framebuffer does not cover the requested extent.
};

/// Aggregated YUV plane pointers and per-plane byte strides. Iteration
/// helpers and the dispatcher use this struct to remove duplicated
/// pointer/stride plumbing.
struct FramePlanes {
    uint8_t* y{nullptr};
    uint8_t* u{nullptr};     // YUV420P only.
    uint8_t* v{nullptr};     // YUV420P only.
    uint8_t* uv{nullptr};    // NV12 only — interleaved UV pairs.

    int stride_y{0};
    int stride_u{0};
    int stride_v{0};
    int stride_uv{0};
};

/// Single frame conversion request. Format/matrix/range use the canonical
/// runtime taxonomy. The output boundary is responsible for constructing the
/// corresponding FrameFormat; conversion does not define a parallel format.
struct ConvertFrameRequest {
    const Framebuffer& src;
    FramePlanes planes;

    int width{0};
    int height{0};
    EncoderPixelFormat format{EncoderPixelFormat::YUV420P};

    YuvMatrix matrix{YuvMatrix::BT709};
    ColorRange range{ColorRange::Limited};
    bool apply_gamma{true};
    // The source RGB values already include the requested display transform;
    // keep this marker so PQ/HLG conversion only performs YUV matrix/range work.
    bool color_transform_applied{false};
    // Select BT.709 OETF when apply_gamma is enabled; otherwise the default
    // remains sRGB for existing image/web encoder call sites.
    bool rec709_oetf{false};
    color::ColorSpace output_space{color::ColorSpace::SRGB};
    float reference_white_nits{100.0f};

};

/// Categorical result for a conversion attempt.
struct ConvertFrameResult {
    bool success{false};
    FrameConversionBackend backend{FrameConversionBackend::Unavailable};
    ConversionError error{ConversionError::None};
    uint64_t conversion_ns{0};
};

// Conversion compatibility boundary. Keep the result ABI aggregate stable,
// but centralize all failure/success result construction.
[[nodiscard]] constexpr ConvertFrameResult make_convert_frame_result(
    bool success,
    FrameConversionBackend backend,
    ConversionError error = ConversionError::None,
    uint64_t conversion_ns = 0) noexcept {
    return ConvertFrameResult{success, backend, error, conversion_ns};
}

// ── Capability advertisement ──────────────────────────────────────────────

/// Per-build static capabilities of the available backends.
struct FrameConversionCapabilities {
    bool highway_direct{false};
    bool swscale{false};

    /// Bitmask of matrices supported by HighwayDirect (BT.2020 is intentionally excluded).
    uint8_t highway_direct_matrices{0};
};

/// Returns the capability snapshot for the current build (compile-time
/// switches resolved at first call). Computed once and cached.
const FrameConversionCapabilities& frame_conversion_capabilities();

/// Pure selector that computes which backend SHOULD handle a request.
/// No fallback chain: returns `Unavailable` if no backend fits.
FrameConversionBackend select_backend(const ConvertFrameRequest& req);

// ── Validation + plane layout helpers ────────────────────────────────────

/// Validates dimensions, plane pointers, format/stride coherence and
/// matrix/range support for the global capabilities. Returns None on success.
ConversionError validate_conversion_request(const ConvertFrameRequest& req);



ConvertFrameResult convert_frame(const ConvertFrameRequest& req);

/// Convenience overload accepting the unpacked plane layout.
ConvertFrameResult convert_frame_tight(
    const Framebuffer& src, FramePlanes planes,
    int width, int height, EncoderPixelFormat format,
    YuvMatrix matrix, ColorRange range,
    bool apply_gamma = true,
    bool rec709_oetf = false,
    color::ColorSpace output_space = color::ColorSpace::SRGB,
    float reference_white_nits = 100.0f);

// ── Direct NV12 Overlay Compositor ──────────────────────────────────────

/// Request for direct compositing of an RGBA foreground overlay directly onto
/// an NV12 background in 4:2:0 space without RGB roundtripping.
struct CompositeOverlayNv12Request {
    FramePlanes bg_planes;
    const Framebuffer& fg_src;
    FramePlanes out_planes;

    int width{0};
    int height{0};

    YuvMatrix matrix{YuvMatrix::BT709};
    ColorRange range{ColorRange::Limited};
};

/// Direct NV12 overlay compositor. Blends foreground RGBA on top of background
/// NV12 in 4:2:0 space with 2x2 chroma-subsampling-aware alpha weighting.
ConvertFrameResult composite_overlay_nv12(const CompositeOverlayNv12Request& req);

} // namespace chronon3d::video
