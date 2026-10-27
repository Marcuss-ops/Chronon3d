#include <chronon3d/render_graph/render_backend.hpp>

namespace chronon3d::graph {

const char* render_backend_error_code_name(RenderBackendErrorCode code) noexcept {
    switch (code) {
        case RenderBackendErrorCode::UnsupportedCapability: return "UnsupportedCapability";
        case RenderBackendErrorCode::InvalidInput:         return "InvalidInput";
        case RenderBackendErrorCode::ExecutionFailure:     return "ExecutionFailure";
    }
    return "Unknown";  // unreachable in well-formed enum usage
}

[[nodiscard]] chronon::Error RenderBackendError::canonical_error(
        std::string component) const {
        const auto kind = code == RenderBackendErrorCode::InvalidInput
            ? chronon::ErrorKind::InvalidPlan
            : code == RenderBackendErrorCode::UnsupportedCapability
                ? chronon::ErrorKind::Recoverable
                : chronon::ErrorKind::FrameFailed;
        return chronon::from_legacy<RenderBackendErrorCode>(
            kind, "render_backend", "chronon3d::graph::RenderBackendErrorCode",
            render_backend_error_code_name(code), message, std::move(component));
    }

[[nodiscard]] std::string RenderBackendStatsApi::lane_name() const {
        return "software";
    }

[[nodiscard]] RenderCapabilities RenderBackendStatsApi::capabilities() const noexcept {
        return RenderCapabilities{};
    }

RenderCounters* RenderBackendStatsApi::counters() { return nullptr; }

std::shared_ptr<cache::FramebufferPool> RenderBackendStatsApi::framebuffer_pool() { return nullptr; }

void RenderBackendStatsApi::export_gpu_telemetry_counters(
        std::vector<std::pair<std::string, std::uint64_t>>& /*out*/) const {}

[[nodiscard]] bool RenderBackendStatsApi::supports_native_video_surface() const noexcept {
        return false;
    }

[[nodiscard]] bool RenderBackendStatsApi::supports_native_surfaces() const noexcept {
        return false;
    }

[[nodiscard]] bool RenderBackendStatsApi::is_batching_supported() const noexcept {
        return supports_native_surfaces();
    }

RenderOpResult RenderBackendSurfaceApi::create_video_encode_surface(
        runtime::RenderSurfaceHandle /*handle*/,
        const runtime::SurfaceDesc& /*desc*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::create_video_encode_surface: unsupported"));
    }

RenderOpResult RenderBackendSurfaceApi::copy_surface_to_video_encode(
        runtime::RenderSurfaceHandle /*source*/,
        runtime::RenderSurfaceHandle /*destination*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::copy_surface_to_video_encode: unsupported"));
    }

void RenderBackendFrameApi::begin_frame_batch() {}

void RenderBackendFrameApi::end_frame_batch() {}

void RenderBackendFrameApi::begin_plan_batch(const runtime::CommandPlan& /*plan*/) {}

void RenderBackendFrameApi::bind_compiled_parameters(std::span<const std::byte> /*parameters*/) {}

void RenderBackendFrameApi::begin_command_batch() {}

void RenderBackendFrameApi::end_command_batch() {}

RenderOpResult RenderBackendFrameApi::draw_node(
        Framebuffer& /*fb*/,
        const RenderNode& /*node*/,
        const RenderState& /*state*/,
        const Camera& /*camera*/,
        int /*width*/,
        int /*height*/
    ) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::draw_node: node drawing is not supported by this backend"));
    }

[[nodiscard]] std::optional<RenderBackendError> RenderBackendFrameApi::validate_render_node(
        const RenderNode& /*node*/) const {
        return std::nullopt;
    }

[[nodiscard]] std::shared_ptr<const renderer::ProcessorRegistrySnapshot>
    RenderBackendStatsApi::processor_snapshot() const noexcept {
        return nullptr;
    }

[[nodiscard]] bool RenderBackendStatsApi::requires_processor_snapshot() const noexcept {
        return false;
    }

RenderOpResult RenderBackendSurfaceApi::create_surface(
        runtime::RenderSurfaceHandle /*handle*/,
        const runtime::SurfaceDesc& /*desc*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::create_surface: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::release_surface(
        runtime::RenderSurfaceHandle /*handle*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::release_surface: native surfaces are not supported"));
    }

[[nodiscard]] bool RenderBackendSurfaceApi::is_native_surface_valid(
        runtime::RenderSurfaceHandle /*handle*/) const noexcept { return false; }

void RenderBackendFrameApi::release_frame_transient_surfaces() noexcept {}

void RenderBackendFrameApi::retire_frame_transient_surfaces() noexcept {}

RenderOpResult RenderBackendSurfaceApi::upload_surface(
        runtime::RenderSurfaceHandle /*handle*/,
        const runtime::SurfaceDesc& /*desc*/,
        std::span<const float> /*rgba*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::upload_surface: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::upload_surface_region(
        runtime::RenderSurfaceHandle /*handle*/,
        const runtime::SurfaceDesc& /*desc*/,
        std::int32_t /*x*/, std::int32_t /*y*/,
        std::uint32_t /*width*/, std::uint32_t /*height*/,
        std::span<const float> /*rgba*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::upload_surface_region: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::upload_surface_async(
        runtime::RenderSurfaceHandle /*handle*/,
        const runtime::SurfaceDesc& /*desc*/,
        std::span<const float> /*rgba*/,
        runtime::UploadTicket& /*ticket*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::upload_surface_async: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::upload_surface_rows_async(
        runtime::RenderSurfaceHandle /*handle*/,
        const runtime::SurfaceDesc& /*desc*/,
        const raster::BBox& /*clip*/,
        const UploadRows& /*rows*/,
        runtime::UploadTicket& /*ticket*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::upload_surface_rows_async: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::wait_upload(const runtime::UploadTicket& /*ticket*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::wait_upload: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::download_surface(
        runtime::RenderSurfaceHandle /*handle*/,
        std::span<float> /*rgba*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::download_surface: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::composite_surfaces(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        BlendMode /*mode*/,
        CompositeOperator /*op*/,
        const std::optional<raster::BBox>& /*clip*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::composite_surfaces: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::composite_surface_weighted(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        float /*opacity*/,
        const std::optional<raster::BBox>& /*clip*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::composite_surface_weighted: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::accumulate_surface_weighted(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        float /*weight*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::accumulate_surface_weighted: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::copy_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        const std::optional<raster::BBox>& /*clip*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::copy_surface: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::fill_rect_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        std::int32_t /*x0*/, std::int32_t /*y0*/,
        std::int32_t /*x1*/, std::int32_t /*y1*/,
        const Color& /*color*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::fill_rect_surface: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::initialize_transparent_surface(
        runtime::RenderSurfaceHandle /*destination*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::initialize_transparent_surface: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::transform_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        int /*offset_x*/, int /*offset_y*/, float /*opacity*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::transform_surface: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::transform_surface_affine(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        const runtime::SurfaceAffineTransform& /*transform*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::transform_surface_affine: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::blur_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        float /*radius*/, bool /*horizontal*/, bool /*box_kernel*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::blur_surface: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::dof_depth_stamp_surface(
        runtime::RenderSurfaceHandle /*depth*/,
        runtime::RenderSurfaceHandle /*layer*/,
        float /*depth_z*/, float /*radius*/,
        int /*clip_x0*/, int /*clip_y0*/, int /*clip_x1*/, int /*clip_y1*/,
        int /*origin_x*/, int /*origin_y*/, bool /*reset*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::dof_depth_stamp_surface: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::dof_blur_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        runtime::RenderSurfaceHandle /*depth*/,
        bool /*vertical*/,
        int /*roi_x0*/, int /*roi_y0*/, int /*roi_x1*/, int /*roi_y1*/,
        float /*max_radius*/, bool /*normalize_opaque*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::dof_blur_surface: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::directional_blur_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        float /*dir_x*/, float /*dir_y*/, float /*length*/, int /*tap_count*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::directional_blur_surface: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::radial_blur_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        float /*center_x_px*/, float /*center_y_px*/,
        float /*amount*/, int /*tap_count*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::radial_blur_surface: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::warp_bulk_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        runtime::RenderSurfaceHandle /*aux*/,
        const WarpBulkParams& /*params*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::warp_bulk_surface: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::generate_bulk_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        const GenerateBulkParams& /*params*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::generate_bulk_surface: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::lut_bulk_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        runtime::RenderSurfaceHandle /*aux*/,
        const LutBulkParams& /*params*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::lut_bulk_surface: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::sharpen_combine_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        runtime::RenderSurfaceHandle /*blurred*/,
        float /*amount*/, float /*threshold*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::sharpen_combine_surface: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::glow_surfaces(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        runtime::RenderSurfaceHandle /*scratch_horizontal*/,
        runtime::RenderSurfaceHandle /*scratch_vertical*/,
        float /*radius*/, float /*intensity*/, const Color& /*tint*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::glow_surfaces: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::bloom_surfaces(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        runtime::RenderSurfaceHandle /*scratch_horizontal*/,
        runtime::RenderSurfaceHandle /*scratch_vertical*/,
        float /*threshold*/, float /*radius*/, float /*intensity*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::bloom_surfaces: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::yuv_overlay_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        runtime::PixelFormat /*format*/,
        const std::optional<raster::BBox>& /*dirty_region*/,
        runtime::YuvExecutionMode /*mode*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::yuv_overlay_surface: native YUV is not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::color_adjust_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        float /*brightness*/, float /*contrast*/,
        const Color& /*tint*/, float /*tint_amount*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::color_adjust_surface: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::color_transform_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        const NativeColorTransform& /*transform*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::color_transform_surface: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::chromatic_transform_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        const ChromaticAberrationParams& /*params*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::chromatic_transform_surface: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::lighting_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        const Color& /*multiplier*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::lighting_surface: native surfaces are not supported"));
    }

RenderOpResult RenderBackendSurfaceApi::matte_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*target*/,
        runtime::RenderSurfaceHandle /*matte*/,
        bool /*luma*/, bool /*inverted*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::matte_surface: native surfaces are not supported"));
    }

RenderOpResult RenderBackendTextureApi::draw_text_run(
        Framebuffer& /*fb*/,
        const chronon3d::TextRunShape& /*shape*/,
        const glm::mat4& /*model_matrix*/,
        float /*opacity*/
    ) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::draw_text_run: not supported by this backend (capabilities().text_run == false)"
        ));
    }

RenderOpResult RenderBackendTextureApi::draw_text_run_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*atlas*/,
        std::span<const runtime::GlyphInstance> /*glyphs*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::draw_text_run_surface: not supported by this backend"));
    }

RenderOpResult RenderBackendTextureApi::draw_text_run_surface_timed(
        runtime::RenderSurfaceHandle destination,
        runtime::RenderSurfaceHandle atlas,
        std::span<const runtime::GlyphInstance> glyphs,
        float current_frame,
        const Color& highlight_color,
        bool highlight_enabled) {
        (void)current_frame;
        (void)highlight_color;
        (void)highlight_enabled;
        return draw_text_run_surface(destination, atlas, glyphs);
    }

RenderOpResult RenderBackendTextureApi::draw_text_batch(
        runtime::RenderSurfaceHandle /*destination*/,
        std::span<const runtime::GlyphStatic> /*glyphs*/,
        std::span<const runtime::TextRunDynamic> /*runs*/,
        std::span<const runtime::RenderSurfaceHandle> /*atlas_pages*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::draw_text_batch: not supported by this backend"));
    }

RenderOpResult RenderBackendTextureApi::execute_layer_batch(
        runtime::RenderSurfaceHandle /*destination*/,
        std::span<const runtime::LayerInstance> /*instances*/,
        std::span<const runtime::RenderSurfaceHandle> /*resources*/,
        std::span<const float> /*transforms*/,
        std::span<const float> /*paints*/) {
        return RenderOpResult(make_render_backend_error(
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::execute_layer_batch: not supported by this backend"));
    }

RenderOpResult RenderBackendTextureApi::execute_layer_batch(
        runtime::RenderSurfaceHandle destination,
        const runtime::GpuLayerBatch& batch,
        std::span<const runtime::RenderSurfaceHandle> resources,
        std::span<const float> transforms,
        std::span<const float> paints) {
        return execute_layer_batch(
            destination, std::span<const runtime::LayerInstance>(batch.instances),
            resources, transforms, paints);
    }

} // namespace chronon3d::graph
