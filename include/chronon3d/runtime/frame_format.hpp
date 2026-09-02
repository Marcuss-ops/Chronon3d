#pragma once

#include <cstddef>
#include <cstdint>

namespace chronon3d::runtime {

// Canonical pixel taxonomy shared by runtime, cache and media boundaries.
// Existing values retain their numeric positions; new formats are appended.
enum class PixelFormat : std::uint8_t {
    Unknown,
    Rgba32Float,
    Rgba8Unorm,
    R8Unorm,
    Nv12,
    P010,
    Depth32Float,
    Bytes,
    Rgba16Float,
    Yuv420P,
    Rgb24,

    // Source-compatibility spellings. These are aliases in the SAME enum,
    // not a second media/encoder pixel taxonomy.
    RGBA8 = Rgba8Unorm,
    YUV420P = Yuv420P,
    NV12 = Nv12,
    RGB24 = Rgb24,
};

enum class ColorMatrix : std::uint8_t {
    Identity,
    Bt601,
    Bt709,
    Bt2020Ncl,

    // Compatibility spellings for the retired YuvMatrix taxonomy.
    BT601 = Bt601,
    BT709 = Bt709,
    BT2020 = Bt2020Ncl,
};

enum class ColorRange : std::uint8_t {
    Limited,
    Full
};

enum class TransferFunction : std::uint8_t {
    Srgb,
    Bt1886,
    Pq,
    Hlg,
    Linear
};

enum class ColorPrimaries : std::uint8_t {
    Bt709,
    Bt2020
};

enum class ChromaLocation : std::uint8_t {
    Left,
    Center,
    TopLeft
};

enum class AlphaMode : std::uint8_t {
    Opaque,
    Straight,
    Premultiplied
};

/// Exact pixel aspect ratio carried by the canonical image-format value.
/// This is deliberately domain-specific: media timeline rational time remains
/// a separate concept and does not need to be coupled to image semantics.
struct PixelAspectRatio {
    std::uint32_t numerator{1};
    std::uint32_t denominator{1};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return numerator != 0 && denominator != 0;
    }

    friend bool operator==(const PixelAspectRatio&, const PixelAspectRatio&) = default;
};

/// Single semantic image-format authority for runtime, cache and media
/// boundaries. Pixel format, color interpretation, alpha convention and pixel
/// aspect travel together so no side metadata can silently diverge.
struct FrameFormat {
    PixelFormat pixel{PixelFormat::Unknown};
    ColorPrimaries primaries{ColorPrimaries::Bt709};
    TransferFunction transfer{TransferFunction::Srgb};
    ColorMatrix matrix{ColorMatrix::Bt709};
    ColorRange range{ColorRange::Limited};
    union {
        ChromaLocation chroma;
        ChromaLocation chroma_location; // compatibility spelling, same storage
    };
    AlphaMode alpha{AlphaMode::Opaque};
    PixelAspectRatio pixel_aspect{};

    constexpr FrameFormat() noexcept : chroma(ChromaLocation::Left) {}

    // Compatibility conversion for call sites that previously carried only a
    // pixel enum. The resulting value is still the canonical FrameFormat.
    constexpr FrameFormat(PixelFormat pixel_value) noexcept
        : pixel(pixel_value), chroma(ChromaLocation::Left) {}

    constexpr FrameFormat(
        PixelFormat pixel_value,
        ColorPrimaries primaries_value,
        TransferFunction transfer_value,
        ColorMatrix matrix_value,
        ColorRange range_value,
        ChromaLocation chroma_value,
        AlphaMode alpha_value,
        PixelAspectRatio pixel_aspect_value = {}) noexcept
        : pixel(pixel_value),
          primaries(primaries_value),
          transfer(transfer_value),
          matrix(matrix_value),
          range(range_value),
          chroma(chroma_value),
          alpha(alpha_value),
          pixel_aspect(pixel_aspect_value) {}

    // Compatibility constructor for the retired ColorMetadata aggregate order.
    constexpr FrameFormat(
        ColorMatrix matrix_value,
        ColorRange range_value,
        TransferFunction transfer_value,
        ColorPrimaries primaries_value,
        ChromaLocation chroma_value) noexcept
        : primaries(primaries_value),
          transfer(transfer_value),
          matrix(matrix_value),
          range(range_value),
          chroma(chroma_value) {}

    [[nodiscard]] constexpr bool valid() const noexcept {
        return pixel != PixelFormat::Unknown && pixel_aspect.valid();
    }

    // Transitional convenience only: it exposes the pixel member of the one
    // canonical value and does not create a second format representation.
    [[nodiscard]] constexpr operator PixelFormat() const noexcept { return pixel; }

    friend constexpr bool operator==(const FrameFormat& lhs,
                                     const FrameFormat& rhs) noexcept {
        return lhs.pixel == rhs.pixel &&
               lhs.primaries == rhs.primaries &&
               lhs.transfer == rhs.transfer &&
               lhs.matrix == rhs.matrix &&
               lhs.range == rhs.range &&
               lhs.chroma == rhs.chroma &&
               lhs.alpha == rhs.alpha &&
               lhs.pixel_aspect == rhs.pixel_aspect;
    }
};

[[nodiscard]] constexpr bool operator==(FrameFormat lhs, PixelFormat rhs) noexcept {
    return lhs.pixel == rhs;
}
[[nodiscard]] constexpr bool operator==(PixelFormat lhs, FrameFormat rhs) noexcept {
    return lhs == rhs.pixel;
}

// Legacy name retained only as a type alias so existing adapter call sites do
// not reintroduce a parallel color-metadata representation.
using ColorMetadata = FrameFormat;

[[nodiscard]] constexpr FrameFormat make_frame_format(
    PixelFormat pixel,
    ColorMetadata color = {},
    AlphaMode alpha = AlphaMode::Opaque) noexcept {
    return FrameFormat{
        pixel,
        color.primaries,
        color.transfer,
        color.matrix,
        color.range,
        color.chroma,
        alpha,
        color.pixel_aspect};
}

[[nodiscard]] constexpr ColorMetadata color_metadata(FrameFormat format) noexcept {
    format.pixel = PixelFormat::Unknown;
    return format;
}

// Graphic color passes have exactly one working domain. Boundary adapters are
// responsible for converting media formats into/out of this representation.
[[nodiscard]] constexpr FrameFormat canonical_render_format() noexcept {
    return FrameFormat{
        PixelFormat::Rgba16Float,
        ColorPrimaries::Bt709,
        TransferFunction::Linear,
        ColorMatrix::Identity,
        ColorRange::Full,
        ChromaLocation::Left,
        AlphaMode::Premultiplied,
        PixelAspectRatio{1, 1}};
}

[[nodiscard]] constexpr bool is_canonical_render_format(FrameFormat format) noexcept {
    return format == canonical_render_format();
}

[[nodiscard]] constexpr bool is_rgb_pixel_format(PixelFormat format) noexcept {
    return format == PixelFormat::Rgba32Float ||
           format == PixelFormat::Rgba16Float ||
           format == PixelFormat::Rgba8Unorm ||
           format == PixelFormat::Rgb24;
}

[[nodiscard]] constexpr bool is_media_color_pixel_format(PixelFormat format) noexcept {
    return format == PixelFormat::Nv12 || format == PixelFormat::P010 ||
           format == PixelFormat::Yuv420P;
}

// Single canonical calculation of tightly packed bytes. Padded/external
// resources may still provide an explicit allocation-size override at the
// resource request boundary; descriptors themselves do not own format math.
[[nodiscard]] constexpr std::size_t tight_surface_bytes(
    PixelFormat fmt, std::uint32_t width, std::uint32_t height) noexcept {
    const std::size_t w = static_cast<std::size_t>(width);
    const std::size_t h = static_cast<std::size_t>(height);
    switch (fmt) {
        case PixelFormat::Rgba32Float: return w * h * 16;
        case PixelFormat::Rgba16Float: return w * h * 8;
        case PixelFormat::Rgba8Unorm: return w * h * 4;
        case PixelFormat::Rgb24: return w * h * 3;
        case PixelFormat::R8Unorm: return w * h;
        case PixelFormat::Yuv420P: {
            const std::size_t chroma_w = (w + 1) / 2;
            const std::size_t chroma_h = (h + 1) / 2;
            return w * h + 2 * chroma_w * chroma_h;
        }
        case PixelFormat::Nv12: {
            const std::size_t chroma_w = (w + 1) & ~static_cast<std::size_t>(1);
            const std::size_t chroma_h = (h + 1) / 2;
            return w * h + chroma_w * chroma_h;
        }
        case PixelFormat::P010: {
            const std::size_t chroma_w = (w + 1) & ~static_cast<std::size_t>(1);
            const std::size_t chroma_h = (h + 1) / 2;
            return w * h * 2 + chroma_w * chroma_h * 2;
        }
        case PixelFormat::Depth32Float: return w * h * 4;
        case PixelFormat::Bytes:
        case PixelFormat::Unknown:
        default:
            return w * h;
    }
}

[[nodiscard]] constexpr std::size_t tight_surface_bytes(
    FrameFormat format, std::uint32_t width, std::uint32_t height) noexcept {
    return tight_surface_bytes(format.pixel, width, height);
}

} // namespace chronon3d::runtime
