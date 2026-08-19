#pragma once

#include <chronon3d/runtime/render_surface_handle.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>

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
    Unknown, Rgba32Float, Rgba8Unorm, Depth32Float, Bytes
};

/// Backend-neutral description used by the lifetime planner and resource
/// resolvers. It contains no Framebuffer or Vulkan type.
struct SurfaceDesc {
    std::uint32_t width{0};
    std::uint32_t height{0};
    PixelFormat format{PixelFormat::Unknown};
    ResourceUsage usage{ResourceUsage::Generic};
    LifetimeClass lifetime{LifetimeClass::FrameTransient};
    std::size_t bytes{0};
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
    std::uint32_t destination_padding[2]{};
    std::int32_t clip_rect[4]{};
    std::uint32_t clip_enabled{0};
    std::uint32_t padding[3]{};
};
static_assert(sizeof(SurfaceAffineTransform) == 96,
              "SurfaceAffineTransform must match affine_transform.comp push constants");

/// Per-glyph instance for the GPU text-run kernel.  It locates one glyph
/// quad inside a packed atlas texture and places it in the destination
/// canvas.  The byte layout MUST match the std430 `GlyphInstance` block in
/// text_run.comp exactly (three ivec2 + four floats = 40 bytes, no padding),
/// because instances are copied verbatim into the kernel's storage buffer.
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
};
static_assert(sizeof(GlyphInstance) == 40,
              "GlyphInstance must stay 32 bytes to match text_run.comp");

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
            desc.bytes = static_cast<std::size_t>(desc.width) * desc.height *
                (desc.format == PixelFormat::Depth32Float ? sizeof(float) : sizeof(float) * 4);
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

private:
    RenderSurfaceHandle m_next_handle{1};
    std::unordered_map<RenderSurfaceHandle, SurfaceRecord> m_surfaces;
};

} // namespace chronon3d::runtime
