#pragma once

#include <chronon3d/runtime/resource_desc.hpp>
#include <chronon3d/runtime/render_surface_handle.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
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

/// Affine source mapping for a device-local transform. Source mapping is
/// evaluated from canvas-space pixel centres using destination_origin; the
/// destination clip_rect is surface-local and half-open. This makes the
/// canvas→surface boundary explicit: dispatch, clip and destination pixels are
/// local 0-based, while only the source equation retains the authored canvas
/// basis needed by the affine shader.
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
    // Homography denominator (2.5D inverse projection), consumed as three
    // float bits by the shader; zero padding preserves the affine contract.
    std::uint32_t padding[3]{};
    // Store blend selector: 0 = replace (legacy default — the kernel writes
    // transparent black where the source does not cover the dispatch region),
    // 1 = SourceOver on premultiplied surfaces (same contract as
    // composite.comp Normal mode — destination pixels survive where the
    // projected source has zero coverage). Producers writing a fresh
    // destination keep 0; compositing onto a seeded destination must use 1.
    std::uint32_t blend_source_over{0};
};
static_assert(sizeof(SurfaceAffineTransform) == 100,
              "SurfaceAffineTransform must match affine_transform.comp push constants");

struct RectF {
    float x0{0.0f};
    float y0{0.0f};
    float x1{0.0f};
    float y1{0.0f};

    friend bool operator==(const RectF&, const RectF&) = default;
};

/// Populate the source equation consumed by the affine shader.
///
/// The shader evaluates source coordinates from canvas-space pixel centres:
///
///     source_x = x_x * canvas.x + x_y * canvas.y + x_offset
///     source_y = y_x * canvas.x + y_y * canvas.y + y_offset
///
/// The resulting source rectangle is surface-local and half-open. Keeping
/// this write in one helper prevents producers from mixing canvas origins with
/// surface-local source coordinates or forgetting the lower bound of a crop.
inline constexpr void set_canvas_source_mapping(
    SurfaceAffineTransform& transform,
    float source_x_x, float source_x_y, float source_x_offset,
    float source_y_x, float source_y_y, float source_y_offset,
    RectF source_bounds) noexcept {
    transform.source_x[0] = source_x_x;
    transform.source_x[1] = source_x_y;
    transform.source_x[2] = source_x_offset;
    transform.source_x[3] = source_bounds.x0;
    transform.source_y[0] = source_y_x;
    transform.source_y[1] = source_y_y;
    transform.source_y[2] = source_y_offset;
    transform.source_y[3] = source_bounds.y0;
    transform.max_x = source_bounds.x1;
    transform.max_y = source_bounds.y1;
}

/// Canonical basis conversion at a tight-surface boundary. Authored graph
/// rectangles remain in canvas coordinates; raster backends consume the
/// surface-local 0-based rectangle. Keep this conversion in one place so a
/// producer cannot silently subtract the origin twice (or not at all).
struct SurfaceCoordinateMapper {
    std::int32_t origin_x{0};
    std::int32_t origin_y{0};

    [[nodiscard]] constexpr RectF canvas_to_surface(RectF rect) const noexcept {
        rect.x0 -= static_cast<float>(origin_x);
        rect.y0 -= static_cast<float>(origin_y);
        rect.x1 -= static_cast<float>(origin_x);
        rect.y1 -= static_cast<float>(origin_y);
        return rect;
    }

    [[nodiscard]] constexpr RectF surface_to_canvas(RectF rect) const noexcept {
        rect.x0 += static_cast<float>(origin_x);
        rect.y0 += static_cast<float>(origin_y);
        rect.x1 += static_cast<float>(origin_x);
        rect.y1 += static_cast<float>(origin_y);
        return rect;
    }
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
    // Layout-space origin used by per-glyph MTSDF scale compensation.  These
    // replace the two unused padding words without changing the 48-byte ABI.
    float origin_x{0.0f};
    float origin_y{0.0f};

    std::uint16_t uv_x() const noexcept { return atlas_x; }
    std::uint16_t uv_y() const noexcept { return atlas_y; }
    std::int16_t local_x() const noexcept { return static_cast<std::int16_t>(plane_left); }
    std::int16_t local_y() const noexcept { return static_cast<std::int16_t>(plane_top); }
    std::uint16_t width() const noexcept { return atlas_w; }
    std::uint16_t height() const noexcept { return atlas_h; }
};
static_assert(sizeof(GlyphStatic) == 48,
              "GlyphStatic must be 48 bytes matching std430 layout");

struct alignas(16) TextRunDynamic {
    float tx{0.0f};
    float ty{0.0f};
    float sx{1.0f};
    float sy{1.0f};
    float opacity{1.0f};
    std::uint32_t color{0xFFFFFFFF};
    std::uint32_t visible_glyph_count{std::numeric_limits<std::uint32_t>::max()};
    /// Bit 0 selects shaped plane placement for coverage glyphs. Zero keeps
    /// the legacy bitmap-origin contract used by compatibility callers.
    std::uint32_t flags{0};
    std::uint32_t stroke_color{0};
    std::uint32_t shadow_color{0};
    std::uint32_t effect_flags{0};
    // IEEE-754 bits of the MTSDF page's distance_range_px. Zero means the
    // shader uses its legacy-safe default for compatibility callers.
    std::uint32_t distance_range_bits{0};
    float stroke_width{0.0f};
    float shadow_dx{0.0f};
    float shadow_dy{0.0f};
    float shadow_blur{0.0f};
    // GPU text animator parameters. The selector is compiled once; these
    // values are the small per-frame binding block consumed by both tile
    // passes. `flags` bit 1 enables the selector and bit 2 selects Smooth
    // (Square is the default).
    float selector_start{0.0f};
    float selector_end{100.0f};
    float selector_amount{100.0f};
    float animator_tracking{0.0f};
    float animator_position_x{0.0f};
    float animator_position_y{0.0f};
    float animator_blur{0.0f};
    std::uint32_t selector_unit_count{0};
    float animator_opacity{1.0f};
    float animator_scale_x{1.0f};
    float animator_scale_y{1.0f};
    /// Per-glyph Z rotation in degrees, applied around the glyph layout
    /// origin exactly like animator_scale_* (forward value; the shader
    /// weights it by the per-glyph selector weight).
    float animator_rotation_z{0.0f};
    /// Per-glyph fill target (pack_text_color). The raster shader lerps
    /// `color` towards this value by the selector weight when the animator
    /// fill flag (see gpu_text_selector_metadata.hpp) is set.
    std::uint32_t animator_fill_color{0};
    std::uint32_t animator_reserved3{0};
    std::uint32_t animator_reserved4{0};
    std::uint32_t animator_reserved5{0};
};
static_assert(sizeof(TextRunDynamic) == 128,
              "TextRunDynamic must be 128 bytes matching std430");
static_assert(alignof(TextRunDynamic) == 16,
              "TextRunDynamic must be 16-byte aligned for std430 arrays");

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

// lookup() hands the record back by value out of a noexcept method, so the copy
// must not be able to throw. The record is all scalars today; this keeps it
// that way instead of discovering the violation as a terminate() in the field.
static_assert(std::is_nothrow_copy_constructible_v<SurfaceRecord>,
              "RenderSurfaceRegistry::lookup() returns SurfaceRecord by value "
              "from a noexcept method");

class RenderSurfaceRegistry {
public:
    [[nodiscard]] RenderSurfaceHandle create(SurfaceDesc desc) {
        if (desc.width == 0 || desc.height == 0 || !desc.format.valid()) {
            return kInvalidRenderSurfaceHandle;
        }
        if (desc.bytes == 0) {
            desc.bytes = desc.tight_bytes();
        }
        std::lock_guard lock(m_mutex);
        const auto handle = m_next_handle++;
        m_surfaces.emplace(handle, SurfaceRecord{
            handle, desc, std::numeric_limits<std::size_t>::max()});
        return handle;
    }

    bool release(RenderSurfaceHandle handle) noexcept {
        if (handle == kInvalidRenderSurfaceHandle) return false;
        std::lock_guard lock(m_mutex);
        const auto it = m_surfaces.find(handle);
        if (it != m_surfaces.end()) {
            m_surfaces.erase(it);
            return true;
        }
        return false;
    }

    /// Returns a copy of the record, or nullopt when the handle is unknown.
    ///
    /// It deliberately does NOT hand out a pointer into the map. Callers use
    /// the record after the lock is released, and a concurrent release() of the
    /// same handle would leave that pointer addressing an erased element — the
    /// mutex would then guard the lookup without protecting the read. Copying
    /// is what makes the lock sufficient.
    [[nodiscard]] std::optional<SurfaceRecord> lookup(RenderSurfaceHandle handle) const noexcept {
        std::lock_guard lock(m_mutex);
        const auto it = m_surfaces.find(handle);
        if (it == m_surfaces.end()) return std::nullopt;
        return it->second;
    }

    bool bind_physical_slot(RenderSurfaceHandle handle, std::size_t slot) noexcept {
        std::lock_guard lock(m_mutex);
        auto it = m_surfaces.find(handle);
        if (it == m_surfaces.end()) return false;
        it->second.physical_slot = slot;
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        std::lock_guard lock(m_mutex);
        return m_surfaces.size();
    }

    [[nodiscard]] std::vector<RenderSurfaceHandle> handles_with_lifetime(
        LifetimeClass lifetime) const {
        std::lock_guard lock(m_mutex);
        std::vector<RenderSurfaceHandle> handles;
        handles.reserve(m_surfaces.size());
        for (const auto& [handle, record] : m_surfaces) {
            if (record.desc.lifetime == lifetime) handles.push_back(handle);
        }
        return handles;
    }

private:
    mutable std::mutex m_mutex;
    RenderSurfaceHandle m_next_handle{1};
    std::unordered_map<RenderSurfaceHandle, SurfaceRecord> m_surfaces;
};

} // namespace chronon3d::runtime
