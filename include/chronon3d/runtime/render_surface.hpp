#pragma once

#include <chronon3d/runtime/render_surface_handle.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace chronon3d::runtime {

struct UploadTicket {
    std::uint64_t value{0};
    [[nodiscard]] bool valid() const noexcept { return value != 0; }
};

enum class ResourceKind : std::uint8_t { Color, Depth, Yuv, Bytes };
enum class LifetimeClass : std::uint8_t { FrameTransient, PipelineSlot, JobPersistent };
enum class ResourceUsage : std::uint8_t {
    Generic, ColorAttachment, DepthAttachment, Storage
};
enum class PixelFormat : std::uint8_t {
    Unknown,
    Rgba32Float,
    Rgba8Unorm,
    R8Unorm,
    Nv12,
    P010,
    Depth32Float,
    Bytes
};

/// Color metadata according to video/broadcast standards.
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
    Hlg
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

struct ColorMetadata {
    ColorMatrix matrix{ColorMatrix::Bt709};
    ColorRange range{ColorRange::Limited};
    TransferFunction transfer{TransferFunction::Srgb};
    ColorPrimaries primaries{ColorPrimaries::Bt709};
    ChromaLocation chroma_location{ChromaLocation::Left};
};

/// Single canonical calculation of tight surface bytes across all backends.
[[nodiscard]] constexpr std::size_t tight_surface_bytes(
    PixelFormat fmt, std::uint32_t width, std::uint32_t height) noexcept {
    const std::size_t w = static_cast<std::size_t>(width);
    const std::size_t h = static_cast<std::size_t>(height);
    switch (fmt) {
        case PixelFormat::Rgba32Float: return w * h * 16;
        case PixelFormat::Rgba8Unorm:   return w * h * 4;
        case PixelFormat::R8Unorm:      return w * h;
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

/// Backend-neutral description used by the lifetime planner and resource
/// resolvers. It contains no Framebuffer or Vulkan type.
struct SurfaceDesc {
    std::uint32_t width{0};
    std::uint32_t height{0};
    PixelFormat format{PixelFormat::Unknown};
    ResourceUsage usage{ResourceUsage::Generic};
    LifetimeClass lifetime{LifetimeClass::FrameTransient};
    std::size_t bytes{0};
    ColorMetadata color{};
};

/// Affine source mapping for a device-local transform. Coordinates are in
/// canvas space and are evaluated at destination pixel centres. The source
/// bounds are half-open, matching the CPU transform kernels.
struct SurfaceAffineTransform {
    float source_x[4]{}; // inverse H row: canvas x coefficient, y coefficient, constant, min
    float source_y[4]{}; // inverse H row: canvas x coefficient, y coefficient, constant, min
    float max_x{0.0f};
    float max_y{0.0f};
    float opacity{1.0f};
    std::uint32_t bilinear{1};
    std::int32_t destination_origin_x{0};
    std::int32_t destination_origin_y{0};
    std::int32_t dispatch_origin_x{0};
    std::int32_t dispatch_origin_y{0};
    std::int32_t clip_rect[4]{};
    std::uint32_t clip_enabled{0};
    std::uint32_t padding[3]{};
};
static_assert(sizeof(SurfaceAffineTransform) == 96,
              "SurfaceAffineTransform must match affine_transform.comp push constants");

/// Per-glyph static data, immutable across frames.  Carried once from
/// prepare() and never changed.  16 bytes per glyph.
struct GlyphStatic {
    std::uint16_t atlas_page{0};
    std::uint16_t uv_x{0};
    std::uint16_t uv_y{0};
    std::int16_t local_x{0};
    std::int16_t local_y{0};
    std::uint16_t width{0};
    std::uint16_t height{0};
    std::uint16_t pad{0};
};
static_assert(sizeof(GlyphStatic) == 16,
              "GlyphStatic must be 16 bytes");

/// Per-run dynamic data, updated once per frame.  24 bytes per text run
/// (NOT per glyph).  All glyphs in a run share the same transform.
struct TextRunDynamic {
    float tx{0.0f};
    float ty{0.0f};
    float sx{1.0f};
    float sy{1.0f};
    float opacity{1.0f};
    std::uint32_t color{0xFFFFFFFF};  // RGBA packed
};
static_assert(sizeof(TextRunDynamic) == 24,
              "TextRunDynamic must be 24 bytes");

/// Legacy per-glyph instance for the GPU text-run kernel.  Kept for
/// backward compatibility; new code uses GlyphStatic + TextRunDynamic.
struct GlyphInstance {
    std::int32_t dst_x{0};     // destination canvas origin (pixels)
    std::int32_t dst_y{0};
    std::int32_t atlas_x{0};   // packed atlas origin (pixels)
    std::int32_t atlas_y{0};
    std::int32_t width{0};     // glyph quad size (pixels)
    std::int32_t height{0};
    float opacity{1.0f};       // per-glyph premultiplied opacity
    float scale_x{1.0f};
    float scale_y{1.0f};
    float pad{0.0f};
    float highlight_start_frame{-1.0f};
    float highlight_end_frame{-1.0f};
};
static_assert(sizeof(GlyphInstance) == 48,
              "GlyphInstance must stay 48 bytes to match text_run.comp");

struct SurfaceRecord {
    RenderSurfaceHandle handle{kInvalidRenderSurfaceHandle};
    SurfaceDesc desc{};
    std::size_t physical_slot{std::numeric_limits<std::size_t>::max()};
};

/// Engine-local registry for logical surfaces. It owns identity and lifetime
/// metadata only; backing storage remains the responsibility of the selected
/// backend/resource planner. The registry is deliberately instance-owned.
class RenderSurfaceRegistry {
public:
    [[nodiscard]] RenderSurfaceHandle create(SurfaceDesc desc) {
        if (desc.width == 0 || desc.height == 0 || desc.format == PixelFormat::Unknown) {
            return kInvalidRenderSurfaceHandle;
        }
        if (desc.bytes == 0) {
            desc.bytes = tight_surface_bytes(desc.format, desc.width, desc.height);
        }
        const auto handle = m_next_handle++;
        m_surfaces.emplace(handle, SurfaceRecord{
            handle, desc, std::numeric_limits<std::size_t>::max()});
        return handle;
    }

    bool release(RenderSurfaceHandle handle) noexcept {
        return handle != kInvalidRenderSurfaceHandle && m_surfaces.erase(handle) != 0;
    }

    [[nodiscard]] const SurfaceRecord* lookup(RenderSurfaceHandle handle) const noexcept {
        const auto it = m_surfaces.find(handle);
        return it == m_surfaces.end() ? nullptr : &it->second;
    }

    bool bind_physical_slot(RenderSurfaceHandle handle, std::size_t slot) noexcept {
        auto it = m_surfaces.find(handle);
        if (it == m_surfaces.end()) return false;
        it->second.physical_slot = slot;
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_surfaces.size(); }

    [[nodiscard]] std::vector<RenderSurfaceHandle> handles_with_lifetime(
        LifetimeClass lifetime) const {
        std::vector<RenderSurfaceHandle> handles;
        handles.reserve(m_surfaces.size());
        for (const auto& [handle, record] : m_surfaces) {
            if (record.desc.lifetime == lifetime) handles.push_back(handle);
        }
        return handles;
    }

private:
    RenderSurfaceHandle m_next_handle{1};
    std::unordered_map<RenderSurfaceHandle, SurfaceRecord> m_surfaces;
};

} // namespace chronon3d::runtime
