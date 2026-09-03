#pragma once

#include <chronon3d/runtime/resource_desc.hpp>
#include <chronon3d/runtime/render_surface_handle.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace chronon3d::runtime {

struct UploadTicket {
    std::uint64_t value{0};
    [[nodiscard]] bool valid() const noexcept { return value != 0; }
};

/// Temporary source-compatibility name. ResourceDesc is the canonical
/// descriptor authority and SurfaceDesc carries no independent state.
using SurfaceDesc = ResourceDesc;

/// Physical representation carried by a backend-neutral render plan.
enum class RenderSurfaceKind : std::uint8_t {
    CpuRgb,
    GpuRgb,
    GpuYuv,
    External,
};

[[nodiscard]] constexpr bool is_rgb_surface_format(PixelFormat format) noexcept {
    return is_rgb_pixel_format(format);
}

[[nodiscard]] constexpr bool is_rgb_surface_format(FrameFormat format) noexcept {
    return is_rgb_pixel_format(format.pixel);
}

[[nodiscard]] constexpr bool is_yuv_surface_format(PixelFormat format) noexcept {
    return format == PixelFormat::Nv12 || format == PixelFormat::P010;
}

[[nodiscard]] constexpr bool is_yuv_surface_format(FrameFormat format) noexcept {
    return is_yuv_surface_format(format.pixel);
}

[[nodiscard]] inline SurfaceDesc normalize_surface_desc(SurfaceDesc desc) noexcept {
    if (desc.bytes == 0) {
        desc.bytes = desc.tight_bytes();
    }
    return desc;
}

/// GPU graphic color surfaces are always normalized into Chronon's one render
/// domain. Media/external adapters must convert before entering this contract.
[[nodiscard]] inline SurfaceDesc normalize_render_color_surface_desc(
    SurfaceDesc desc) noexcept {
    if (is_rgb_surface_format(desc.format)) {
        desc.format = canonical_render_format();
        desc.bytes = desc.tight_bytes();
    }
    return normalize_surface_desc(std::move(desc));
}

/// Backend-neutral surface abstraction. It describes ownership and format
/// without exposing Vulkan, CUDA, or encoder-specific types. Concrete storage
/// remains owned by the corresponding backend or by the CPU fallback.
class RenderSurface {
public:
    virtual ~RenderSurface() = default;

    [[nodiscard]] virtual RenderSurfaceKind kind() const noexcept = 0;
    [[nodiscard]] virtual const SurfaceDesc& desc() const noexcept = 0;
    [[nodiscard]] virtual RenderSurfaceHandle handle() const noexcept = 0;

    /// CPU surfaces override this to expose the retained Framebuffer fallback.
    /// GPU and external surfaces intentionally return nullptr.
    [[nodiscard]] virtual Framebuffer* cpu_framebuffer() noexcept { return nullptr; }
    [[nodiscard]] virtual const Framebuffer* cpu_framebuffer() const noexcept {
        return nullptr;
    }

    [[nodiscard]] virtual bool valid() const noexcept {
        const auto& surface = desc();
        return surface.width != 0 && surface.height != 0 && surface.format.valid();
    }
};

/// CPU RGB implementation and compatibility bridge for the existing
/// high-precision Framebuffer. No second pixel store is introduced. The CPU
/// fallback is float32 storage but observes the same linear/premultiplied
/// working semantics as the canonical GPU render domain.
class CpuRgbSurface final : public RenderSurface {
public:
    explicit CpuRgbSurface(std::shared_ptr<Framebuffer> framebuffer)
        : m_framebuffer(std::move(framebuffer)),
          m_desc(m_framebuffer
              ? SurfaceDesc{
                    static_cast<std::uint32_t>(m_framebuffer->width()),
                    static_cast<std::uint32_t>(m_framebuffer->height()),
                    FrameFormat{
                        PixelFormat::Rgba32Float,
                        ColorPrimaries::Bt709,
                        TransferFunction::Linear,
                        ColorMatrix::Identity,
                        ColorRange::Full,
                        ChromaLocation::Left,
                        AlphaMode::Premultiplied,
                        PixelAspectRatio{1, 1}},
                    ResourceUsage::ColorAttachment,
                    LifetimeClass::FrameTransient,
                    m_framebuffer->size_bytes()}
              : SurfaceDesc{}) {}

    [[nodiscard]] RenderSurfaceKind kind() const noexcept override {
        return RenderSurfaceKind::CpuRgb;
    }
    [[nodiscard]] const SurfaceDesc& desc() const noexcept override {
        return m_desc;
    }
    [[nodiscard]] RenderSurfaceHandle handle() const noexcept override {
        return kInvalidRenderSurfaceHandle;
    }
    [[nodiscard]] Framebuffer* cpu_framebuffer() noexcept override {
        return m_framebuffer.get();
    }
    [[nodiscard]] const Framebuffer* cpu_framebuffer() const noexcept override {
        return m_framebuffer.get();
    }
    [[nodiscard]] bool valid() const noexcept override {
        return m_framebuffer != nullptr && RenderSurface::valid();
    }

    [[nodiscard]] const std::shared_ptr<Framebuffer>& framebuffer() const noexcept {
        return m_framebuffer;
    }

private:
    std::shared_ptr<Framebuffer> m_framebuffer;
    SurfaceDesc m_desc{};
};

/// GPU RGB contract. The logical handle is resolved by RenderSurfaceRegistry
/// and the selected backend; this type owns no device resource. All RGB inputs
/// are normalized to RGBA16F Linear Premultiplied at construction.
class GpuRgbSurface final : public RenderSurface {
public:
    GpuRgbSurface(RenderSurfaceHandle handle, SurfaceDesc desc)
        : m_handle(handle),
          m_desc(normalize_render_color_surface_desc(std::move(desc))) {}

    [[nodiscard]] RenderSurfaceKind kind() const noexcept override {
        return RenderSurfaceKind::GpuRgb;
    }
    [[nodiscard]] const SurfaceDesc& desc() const noexcept override {
        return m_desc;
    }
    [[nodiscard]] RenderSurfaceHandle handle() const noexcept override {
        return m_handle;
    }
    [[nodiscard]] bool valid() const noexcept override {
        return m_handle != kInvalidRenderSurfaceHandle &&
               is_canonical_render_format(m_desc.format) &&
               RenderSurface::valid();
    }

private:
    RenderSurfaceHandle m_handle{kInvalidRenderSurfaceHandle};
    SurfaceDesc m_desc{};
};

/// GPU YUV contract for encoder-compatible NV12/P010 device surfaces.
class GpuYuvSurface final : public RenderSurface {
public:
    GpuYuvSurface(RenderSurfaceHandle handle, SurfaceDesc desc)
        : m_handle(handle), m_desc(normalize_surface_desc(std::move(desc))) {}

    [[nodiscard]] RenderSurfaceKind kind() const noexcept override {
        return RenderSurfaceKind::GpuYuv;
    }
    [[nodiscard]] const SurfaceDesc& desc() const noexcept override {
        return m_desc;
    }
    [[nodiscard]] RenderSurfaceHandle handle() const noexcept override {
        return m_handle;
    }
    [[nodiscard]] bool valid() const noexcept override {
        return m_handle != kInvalidRenderSurfaceHandle &&
               is_yuv_surface_format(m_desc.format) &&
               (m_desc.width % 2u) == 0 && (m_desc.height % 2u) == 0 &&
               RenderSurface::valid();
    }

private:
    RenderSurfaceHandle m_handle{kInvalidRenderSurfaceHandle};
    SurfaceDesc m_desc{};
};

/// External surface contract for imported/interoperable resources. The token
/// is opaque and non-owning; its lifetime is controlled by the producer.
class ExternalSurface final : public RenderSurface {
public:
    ExternalSurface(RenderSurfaceHandle handle, SurfaceDesc desc,
                    std::uintptr_t external_token)
        : m_handle(handle),
          m_desc(normalize_surface_desc(std::move(desc))),
          m_external_token(external_token) {}

    [[nodiscard]] RenderSurfaceKind kind() const noexcept override {
        return RenderSurfaceKind::External;
    }
    [[nodiscard]] const SurfaceDesc& desc() const noexcept override {
        return m_desc;
    }
    [[nodiscard]] RenderSurfaceHandle handle() const noexcept override {
        return m_handle;
    }
    [[nodiscard]] std::uintptr_t external_token() const noexcept {
        return m_external_token;
    }
    [[nodiscard]] bool valid() const noexcept override {
        return m_handle != kInvalidRenderSurfaceHandle &&
               m_external_token != 0 && RenderSurface::valid();
    }

private:
    RenderSurfaceHandle m_handle{kInvalidRenderSurfaceHandle};
    SurfaceDesc m_desc{};
    std::uintptr_t m_external_token{0};
};

/// Affine source mapping for a device-local transform. Coordinates are in
/// canvas space and are evaluated at destination pixel centres. The source
/// bounds are half-open, matching the CPU transform kernels.
struct SurfaceAffineTransform {
    float source_x[4]{};
    float source_y[4]{};
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

struct RectF {
    float x0{0.0f};
    float y0{0.0f};
    float x1{0.0f};
    float y1{0.0f};

    friend bool operator==(const RectF&, const RectF&) = default;
};

struct TextRunStatic {
    std::uint32_t first_glyph{0};
    std::uint32_t glyph_count{0};
    RectF local_bbox{};
};

struct alignas(16) GlyphStatic {
    std::uint32_t run_index{0};
    std::uint16_t atlas_page{0};
    std::uint16_t flags{0};

    std::uint16_t atlas_x{0};
    std::uint16_t atlas_y{0};
    std::uint16_t atlas_w{0};
    std::uint16_t atlas_h{0};

    float plane_left{0.0f};
    float plane_top{0.0f};
    float plane_right{0.0f};
    float plane_bottom{0.0f};

    std::uint32_t draw_order{0};
    std::uint32_t pad0{0};
    std::uint32_t pad1{0};
    std::uint32_t pad2{0};

    std::uint16_t uv_x() const noexcept { return atlas_x; }
    std::uint16_t uv_y() const noexcept { return atlas_y; }
    std::int16_t local_x() const noexcept { return static_cast<std::int16_t>(plane_left); }
    std::int16_t local_y() const noexcept { return static_cast<std::int16_t>(plane_top); }
    std::uint16_t width() const noexcept { return atlas_w; }
    std::uint16_t height() const noexcept { return atlas_h; }
};
static_assert(sizeof(GlyphStatic) == 48,
              "GlyphStatic must be 48 bytes matching std430 layout");

struct TextRunDynamic {
    float tx{0.0f};
    float ty{0.0f};
    float sx{1.0f};
    float sy{1.0f};
    float opacity{1.0f};
    std::uint32_t color{0xFFFFFFFF};
};
static_assert(sizeof(TextRunDynamic) == 24,
              "TextRunDynamic must be 24 bytes");

struct GlyphInstance {
    std::int32_t dst_x{0};
    std::int32_t dst_y{0};
    std::int32_t atlas_x{0};
    std::int32_t atlas_y{0};
    std::int32_t width{0};
    std::int32_t height{0};
    float opacity{1.0f};
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

class RenderSurfaceRegistry {
public:
    [[nodiscard]] RenderSurfaceHandle create(SurfaceDesc desc) {
        if (desc.width == 0 || desc.height == 0 || !desc.format.valid()) {
            return kInvalidRenderSurfaceHandle;
        }
        if (desc.bytes == 0) {
            desc.bytes = desc.tight_bytes();
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
