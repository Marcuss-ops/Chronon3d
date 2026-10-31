// vulkan_backend_operations_filter_private.cpp — blur, dof, radial/warp/generate/lut and sharpen op wrappers.
// Split from the oversized unit per issue #51 (LEANUP-001);
// out-of-class definitions, declared in vulkan_backend_impl.hpp.

#include "vulkan_backend_impl.hpp"

namespace chronon3d::backends::vulkan {

void VulkanBackend::Impl::blur(runtime::RenderSurfaceHandle destination,
                               runtime::RenderSurfaceHandle source,
                               float radius, bool horizontal, bool box_kernel) {
    if (!(radius >= 0.0f) || radius > 192.0f) {
        throw std::invalid_argument("Vulkan blur radius must be within [0, 192]");
    }
    auto& dst_image = resolve_image(destination);
    auto& src_image = resolve_image(source);
    if (!src_image.initialized ||
        dst_image.width != src_image.width ||
        dst_image.height != src_image.height) {
        throw std::invalid_argument("Vulkan blur references incompatible surfaces");
    }
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_descriptors(descriptors, dst_image, src_image);
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst_image, &src_image}, runtime::GpuPassKind::Blur);
        record_blur(cmd, descriptors, dst_image, src_image, radius, horizontal,
                    box_kernel);
        dst_image.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error("Vulkan blur requires an active frame batch");
}

void VulkanBackend::Impl::directional_blur(runtime::RenderSurfaceHandle destination,
                                            runtime::RenderSurfaceHandle source,
                                            float dir_x, float dir_y,
                                            float length, int tap_count) {
    if (!std::isfinite(dir_x) || !std::isfinite(dir_y) ||
        !(length > 0.0f) || length > 64.0f ||
        tap_count < 2 || tap_count > 64) {
        throw std::invalid_argument(
            "Vulkan directional blur parameters are outside supported ranges");
    }
    auto& dst_image = resolve_image(destination);
    auto& src_image = resolve_image(source);
    if (!src_image.initialized ||
        dst_image.width != src_image.width ||
        dst_image.height != src_image.height) {
        throw std::invalid_argument("Vulkan directional blur references incompatible surfaces");
    }
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_descriptors(descriptors, dst_image, src_image);
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst_image, &src_image}, runtime::GpuPassKind::Blur);
        record_directional_blur(cmd, descriptors, dst_image, src_image,
                                dir_x, dir_y, length, tap_count);
        dst_image.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error("Vulkan directional blur requires an active frame batch");
}

void VulkanBackend::Impl::dof_depth_stamp(runtime::RenderSurfaceHandle depth_handle,
                                           runtime::RenderSurfaceHandle layer_handle,
                                           const DofDepthStampPush& push) {
    auto& depth = resolve_image(depth_handle);
    auto& layer = resolve_image(layer_handle);
    if (!layer.initialized) {
        throw std::invalid_argument("Vulkan DOF depth stamp references an uninitialized layer");
    }
    if (!frame_batch.active) {
        throw std::logic_error("Vulkan DOF depth stamp requires an active frame batch");
    }
    const auto descriptors = allocate_pass_descriptor_set();
    write_descriptors(descriptors, depth, layer);
    const auto cmd = active_command_buffer();
    emit_pass_sync(cmd, {&depth, &layer}, runtime::GpuPassKind::Blur);
    record_dof_depth_stamp(cmd, descriptors, depth, layer, push);
    depth.initialized = true;
    ++frame_batch.pass_count;
    ++stats.passes_executed;
}

void VulkanBackend::Impl::dof_blur(runtime::RenderSurfaceHandle destination_handle,
                                   runtime::RenderSurfaceHandle source_handle,
                                   runtime::RenderSurfaceHandle depth_handle,
                                   const DofBlurPush& push) {
    auto& dst_image = resolve_image(destination_handle);
    auto& src_image = resolve_image(source_handle);
    auto& depth = resolve_image(depth_handle);
    if (!src_image.initialized || !depth.initialized ||
        dst_image.width != src_image.width || dst_image.height != src_image.height ||
        depth.width != dst_image.width || depth.height != dst_image.height) {
        throw std::invalid_argument("Vulkan DOF blur references incompatible surfaces");
    }
    if (!frame_batch.active) {
        throw std::logic_error("Vulkan DOF blur requires an active frame batch");
    }
    const auto descriptors = allocate_pass_descriptor_set();
    write_dof_descriptors(descriptors, dst_image, src_image, depth);
    const auto cmd = active_command_buffer();
    emit_pass_sync(cmd, {&dst_image, &src_image, &depth}, runtime::GpuPassKind::Blur);
    record_dof_blur(cmd, descriptors, dst_image, src_image, depth, push);
    dst_image.initialized = true;
    ++frame_batch.pass_count;
    ++stats.passes_executed;
}

void VulkanBackend::Impl::radial_blur(runtime::RenderSurfaceHandle destination,
                                       runtime::RenderSurfaceHandle source,
                                       float center_x_px, float center_y_px,
                                       float amount, int tap_count) {
    if (!std::isfinite(center_x_px) || !std::isfinite(center_y_px) ||
        !(amount > 0.0f) || amount > 8.0f ||
        tap_count < 2 || tap_count > 64) {
        throw std::invalid_argument(
            "Vulkan radial blur parameters are outside supported ranges");
    }
    auto& dst_image = resolve_image(destination);
    auto& src_image = resolve_image(source);
    if (!src_image.initialized ||
        dst_image.width != src_image.width ||
        dst_image.height != src_image.height) {
        throw std::invalid_argument("Vulkan radial blur references incompatible surfaces");
    }
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_descriptors(descriptors, dst_image, src_image);
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst_image, &src_image}, runtime::GpuPassKind::Blur);
        record_radial_blur(cmd, descriptors, dst_image, src_image,
                           center_x_px, center_y_px, amount, tap_count);
        dst_image.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error("Vulkan radial blur requires an active frame batch");
}

void VulkanBackend::Impl::warp_bulk(runtime::RenderSurfaceHandle destination,
                                    runtime::RenderSurfaceHandle source,
                                    runtime::RenderSurfaceHandle aux,
                                    const graph::WarpBulkParams& params) {
    if (params.mode < 0 || params.mode > 12 ||
        (params.edge_mode != 0 && params.edge_mode != 1)) {
        throw std::invalid_argument(
            "Vulkan warp bulk parameters are outside supported ranges");
    }
    auto& dst_image = resolve_image(destination);
    auto& src_image = resolve_image(source);
    if (!src_image.initialized ||
        dst_image.width != src_image.width ||
        dst_image.height != src_image.height) {
        throw std::invalid_argument("Vulkan warp bulk references incompatible surfaces");
    }
    const bool needs_aux = (params.mode == 12);
    auto& aux_image = needs_aux ? resolve_image(aux) : src_image;
    if (needs_aux && !aux_image.initialized) {
        throw std::invalid_argument("Vulkan warp bulk displacement mode requires an aux surface");
    }
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_matte_descriptors(descriptors, dst_image, src_image, aux_image);
        const auto cmd = active_command_buffer();
        if (needs_aux) emit_pass_sync(cmd, {&dst_image, &src_image, &aux_image}, runtime::GpuPassKind::AffineTransform);
        else emit_pass_sync(cmd, {&dst_image, &src_image}, runtime::GpuPassKind::AffineTransform);
        record_warp_bulk(cmd, descriptors, dst_image, src_image, params);
        dst_image.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error("Vulkan warp bulk requires an active frame batch");
}

void VulkanBackend::Impl::generate_bulk(runtime::RenderSurfaceHandle destination,
                                        runtime::RenderSurfaceHandle source,
                                        const graph::GenerateBulkParams& params) {
    if (params.mode < 0 || params.mode > 5) {
        throw std::invalid_argument(
            "Vulkan generate bulk parameters are outside supported ranges");
    }
    auto& dst_image = resolve_image(destination);
    auto& src_image = resolve_image(source);
    if (!src_image.initialized ||
        dst_image.width != src_image.width ||
        dst_image.height != src_image.height) {
        throw std::invalid_argument("Vulkan generate bulk references incompatible surfaces");
    }
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_descriptors(descriptors, dst_image, src_image);
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst_image, &src_image}, runtime::GpuPassKind::AffineTransform);
        record_generate_bulk(cmd, descriptors, dst_image, src_image, params);
        dst_image.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error("Vulkan generate bulk requires an active frame batch");
}

void VulkanBackend::Impl::lut_bulk(runtime::RenderSurfaceHandle destination,
                                   runtime::RenderSurfaceHandle source,
                                   runtime::RenderSurfaceHandle aux,
                                   const graph::LutBulkParams& params) {
    if (params.mode < 0 || params.mode > 1) {
        throw std::invalid_argument(
            "Vulkan lut bulk parameters are outside supported ranges");
    }
    auto& dst_image = resolve_image(destination);
    auto& src_image = resolve_image(source);
    auto& aux_image = resolve_image(aux);
    if (!src_image.initialized || !aux_image.initialized ||
        dst_image.width != src_image.width ||
        dst_image.height != src_image.height) {
        throw std::invalid_argument("Vulkan lut bulk references incompatible surfaces");
    }
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_matte_descriptors(descriptors, dst_image, src_image, aux_image);
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst_image, &src_image, &aux_image}, runtime::GpuPassKind::ColorAdjust);
        record_lut_bulk(cmd, descriptors, dst_image, src_image, params);
        dst_image.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error("Vulkan lut bulk requires an active frame batch");
}

void VulkanBackend::Impl::sharpen_combine(runtime::RenderSurfaceHandle destination,
                                          runtime::RenderSurfaceHandle source,
                                          runtime::RenderSurfaceHandle blurred,
                                          float amount, float threshold) {
    if (!std::isfinite(amount) || std::abs(amount) > 4.0f ||
        !std::isfinite(threshold) || threshold < 0.0f || threshold > 1.0f) {
        throw std::invalid_argument(
            "Vulkan sharpen combine parameters are outside supported ranges");
    }
    auto& dst_image = resolve_image(destination);
    auto& src_image = resolve_image(source);
    auto& blurred_image = resolve_image(blurred);
    if (!src_image.initialized || !blurred_image.initialized ||
        dst_image.width != src_image.width || dst_image.height != src_image.height ||
        dst_image.width != blurred_image.width || dst_image.height != blurred_image.height) {
        throw std::invalid_argument(
            "Vulkan sharpen combine references incompatible surfaces");
    }
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_matte_descriptors(descriptors, dst_image, src_image, blurred_image);
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst_image, &src_image, &blurred_image}, runtime::GpuPassKind::Blur);
        record_sharpen_combine(cmd, descriptors, dst_image, src_image,
                               blurred_image, amount, threshold);
        dst_image.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error("Vulkan sharpen combine requires an active frame batch");
}
} // namespace chronon3d::backends::vulkan
