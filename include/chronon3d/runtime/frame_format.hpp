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
};

enum class ColorMatrix : std::uint8_t {
    Identity,
    Bt601,
    Bt709,
    Bt2020Ncl
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

// Compatibility value used by SurfaceDesc while it is migrated to FrameFormat.
// It is intentionally a value type, not a second format authority.
struct ColorMetadata {
    ColorMatrix matrix{ColorMatrix::Bt709};
    ColorRange range{ColorRange::Limited};
    TransferFunction transfer{TransferFunction::Srgb};
    ColorPrimaries primaries{ColorPrimaries::Bt709};
    ChromaLocation chroma_location{ChromaLocation::Left};

    friend bool operator==(const ColorMetadata&, const ColorMetadata&) = default;
};

// Single semantic image-format authority. Pixel aspect will join this value
// when the exact Rational type lands with FrameTimeContext; duplicating a
// one-off rational type here would create the kind of parallel truth this
// contract exists to remove.
struct FrameFormat {
    PixelFormat pixel{PixelFormat::Unknown};
    ColorPrimaries primaries{ColorPrimaries::Bt709};
    TransferFunction transfer{TransferFunction::Srgb};
    ColorMatrix matrix{ColorMatrix::Bt709};
    ColorRange range{ColorRange::Limited};
    ChromaLocation chroma{ChromaLocation::Left};
    AlphaMode alpha{AlphaMode::Opaque};

    friend bool operator==(const FrameFormat&, const FrameFormat&) = default;
};

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
        color.chroma_location,
        alpha};
}

[[nodiscard]] constexpr ColorMetadata color_metadata(FrameFormat format) noexcept {
    return ColorMetadata{
        format.matrix,
        format.range,
        format.transfer,
        format.primaries,
        format.chroma};
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
        AlphaMode::Premultiplied};
}

[[nodiscard]] constexpr bool is_canonical_render_format(FrameFormat format) noexcept {
    return format == canonical_render_format();
}

// Single canonical calculation of tightly packed bytes. Padded/external
// resources can still carry an explicit byte count in their resource descriptor.
[[nodiscard]] constexpr std::size_t tight_surface_bytes(
    PixelFormat fmt, std::uint32_t width, std::uint32_t height) noexcept {
    const std::size_t w = static_cast<std::size_t>(width);
    const std::size_t h = static_cast<std::size_t>(height);
    switch (fmt) {
        case PixelFormat::Rgba32Float: return w * h * 16;
        case PixelFormat::Rgba16Float: return w * h * 8;
        case PixelFormat::Rgba8Unorm: return w * h * 4;
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

} // namespace chronon3d::runtime
