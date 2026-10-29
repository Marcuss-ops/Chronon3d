#pragma once

#include <chronon3d/render_graph/render_backend_types.hpp>
#include <chronon3d/render_graph/render_backend_surface_types.hpp>

#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/compositor/composite_operator.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/runtime/gpu_command_plan.hpp>
#include <chronon3d/runtime/render_surface.hpp>

#include <cstdint>
#include <optional>
#include <span>

namespace chronon3d::graph {

/// Native surface allocation, transfer, compositing, and effect operations.
class RenderBackendSurfaceApi {
public:
    virtual ~RenderBackendSurfaceApi() = default;

    virtual RenderOpResult create_video_encode_surface(
        runtime::RenderSurfaceHandle handle, const runtime::SurfaceDesc& desc);
    virtual RenderOpResult copy_surface_to_video_encode(
        runtime::RenderSurfaceHandle source, runtime::RenderSurfaceHandle destination);
    virtual RenderOpResult create_surface(
        runtime::RenderSurfaceHandle handle, const runtime::SurfaceDesc& desc);
    virtual RenderOpResult release_surface(runtime::RenderSurfaceHandle handle);
    [[nodiscard]] virtual bool is_native_surface_valid(
        runtime::RenderSurfaceHandle handle) const noexcept;
    virtual RenderOpResult upload_surface(
        runtime::RenderSurfaceHandle handle, const runtime::SurfaceDesc& desc,
        std::span<const float> rgba);
    virtual RenderOpResult upload_surface_region(
        runtime::RenderSurfaceHandle handle, const runtime::SurfaceDesc& desc,
        std::int32_t x, std::int32_t y, std::uint32_t width, std::uint32_t height,
        std::span<const float> rgba);
    virtual RenderOpResult upload_surface_async(
        runtime::RenderSurfaceHandle handle, const runtime::SurfaceDesc& desc,
        std::span<const float> rgba, runtime::UploadTicket& ticket);

    struct UploadRows { std::span<const Color> first_row{}; std::size_t stride{0}; };
    virtual RenderOpResult upload_surface_rows_async(
        runtime::RenderSurfaceHandle handle, const runtime::SurfaceDesc& desc,
        const raster::BBox& clip, const UploadRows& rows, runtime::UploadTicket& ticket);
    virtual RenderOpResult wait_upload(const runtime::UploadTicket& ticket);
    virtual RenderOpResult download_surface(
        runtime::RenderSurfaceHandle handle, std::span<float> rgba);
    virtual RenderOpResult composite_surfaces(
        runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle source,
        BlendMode mode = BlendMode::Normal,
        CompositeOperator op = CompositeOperator::SourceOver,
        const std::optional<raster::BBox>& clip = std::nullopt);
    virtual RenderOpResult composite_surface_weighted(
        runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle source,
        float opacity, const std::optional<raster::BBox>& clip = std::nullopt);
    virtual RenderOpResult accumulate_surface_weighted(
        runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle source,
        float weight);
    virtual RenderOpResult copy_surface(
        runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle source,
        const std::optional<raster::BBox>& clip = std::nullopt);
    // Copies used to preserve an internal framebuffer value do not represent
    // a compiled graph pass and must not consume a CommandPlan transition.
    virtual RenderOpResult copy_surface_auxiliary(
        runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle source,
        const std::optional<raster::BBox>& clip = std::nullopt);
    virtual RenderOpResult fill_rect_surface(
        runtime::RenderSurfaceHandle destination, std::int32_t x0, std::int32_t y0,
        std::int32_t x1, std::int32_t y1, const Color& color);
    virtual RenderOpResult initialize_transparent_surface(runtime::RenderSurfaceHandle destination);
    virtual RenderOpResult transform_surface(
        runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle source,
        int offset_x, int offset_y, float opacity);
    virtual RenderOpResult transform_surface_affine(
        runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle source,
        const runtime::SurfaceAffineTransform& transform);
    virtual RenderOpResult blur_surface(
        runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle source,
        float radius, bool horizontal, bool box_kernel = false);
    virtual RenderOpResult dof_depth_stamp_surface(
        runtime::RenderSurfaceHandle depth, runtime::RenderSurfaceHandle layer,
        float depth_z, float radius, int clip_x0, int clip_y0, int clip_x1, int clip_y1,
        int origin_x, int origin_y, bool reset);
    virtual RenderOpResult dof_blur_surface(
        runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle source,
        runtime::RenderSurfaceHandle depth, bool vertical, int roi_x0, int roi_y0,
        int roi_x1, int roi_y1, float max_radius, bool normalize_opaque);
    virtual RenderOpResult directional_blur_surface(
        runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle source,
        float dir_x, float dir_y, float length, int tap_count);
    virtual RenderOpResult radial_blur_surface(
        runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle source,
        float center_x_px, float center_y_px, float amount, int tap_count);
    virtual RenderOpResult warp_bulk_surface(
        runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle source,
        runtime::RenderSurfaceHandle aux, const WarpBulkParams& params);
    virtual RenderOpResult generate_bulk_surface(
        runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle source,
        const GenerateBulkParams& params);
    virtual RenderOpResult lut_bulk_surface(
        runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle source,
        runtime::RenderSurfaceHandle aux, const LutBulkParams& params);
    virtual RenderOpResult sharpen_combine_surface(
        runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle source,
        runtime::RenderSurfaceHandle blurred, float amount, float threshold);
    virtual RenderOpResult glow_surfaces(
        runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle source,
        runtime::RenderSurfaceHandle scratch_horizontal,
        runtime::RenderSurfaceHandle scratch_vertical, float radius, float intensity,
        const Color& tint);
    virtual RenderOpResult bloom_surfaces(
        runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle source,
        runtime::RenderSurfaceHandle scratch_horizontal,
        runtime::RenderSurfaceHandle scratch_vertical, float threshold, float radius,
        float intensity);
    virtual RenderOpResult yuv_overlay_surface(
        runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle source,
        runtime::PixelFormat format, const std::optional<raster::BBox>& dirty_region,
        runtime::YuvExecutionMode mode);
    virtual RenderOpResult color_adjust_surface(
        runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle source,
        float brightness, float contrast, const Color& tint, float tint_amount);
    virtual RenderOpResult color_transform_surface(
        runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle source,
        const NativeColorTransform& transform);
    virtual RenderOpResult chromatic_transform_surface(
        runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle source,
        const ChromaticAberrationParams& params);
    virtual RenderOpResult lighting_surface(
        runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle source,
        const Color& multiplier);
    virtual RenderOpResult matte_surface(
        runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle target,
        runtime::RenderSurfaceHandle matte, bool luma, bool inverted);
};

} // namespace chronon3d::graph
