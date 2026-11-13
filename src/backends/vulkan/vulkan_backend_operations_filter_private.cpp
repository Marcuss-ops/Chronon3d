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

void VulkanBackend::Impl::occlusion_tag(runtime::RenderSurfaceHandle mask_handle,
                                       runtime::RenderSurfaceHandle layer_depth_handle,
                                       runtime::RenderSurfaceHandle frame_depth_handle,
                                       bool merge) {
    auto& mask = resolve_image(mask_handle);
    auto& layer_depth = resolve_image(layer_depth_handle);
    auto& frame_depth = resolve_image(frame_depth_handle);
    if (!layer_depth.initialized || !mask.width || !mask.height ||
        layer_depth.width != mask.width || layer_depth.height != mask.height ||
        frame_depth.width != mask.width || frame_depth.height != mask.height) {
        throw std::invalid_argument(
            "Vulkan occlusion tag references incompatible depth surfaces");
    }
    if (!frame_batch.active) {
        throw std::logic_error("Vulkan occlusion tag requires an active frame batch");
    }
    const auto descriptors = allocate_pass_descriptor_set();
    write_dof_descriptors(descriptors, mask, layer_depth, frame_depth);
    const auto cmd = active_command_buffer();
    emit_pass_sync(cmd, {&mask, &layer_depth, &frame_depth},
                   runtime::GpuPassKind::Composite);
    record_occlusion_tag(cmd, descriptors, mask, layer_depth, frame_depth,
                         merge ? 1 : 0);
    mask.initialized = true;
    frame_depth.initialized = true;
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

void VulkanBackend::Impl::camera_ring(runtime::RenderSurfaceHandle destination,
                                       runtime::RenderSurfaceHandle source,
                                       const CameraRingParams& params) {
    params.validate();
    auto& dst_image = resolve_image(destination);
    auto& src_image = resolve_image(source);
    if (!src_image.initialized ||
        dst_image.width != src_image.width ||
        dst_image.height != src_image.height) {
        throw std::invalid_argument("Vulkan camera ring references incompatible surfaces");
    }
    if (!frame_batch.active) {
        throw std::logic_error("Vulkan camera ring requires an active frame batch");
    }
    const auto descriptors = allocate_pass_descriptor_set();
    write_descriptors(descriptors, dst_image, src_image);
    const auto cmd = active_command_buffer();
    emit_pass_sync(cmd, {&dst_image, &src_image}, runtime::GpuPassKind::Blur);
    record_camera_ring(cmd, descriptors, dst_image, src_image, params);
    dst_image.initialized = true;
    ++frame_batch.pass_count;
    ++stats.passes_executed;
}

void VulkanBackend::Impl::sdf_bevel(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    runtime::RenderSurfaceHandle scratch_a,
    runtime::RenderSurfaceHandle scratch_b,
    runtime::RenderSurfaceHandle scratch_c,
    const SdfBevelParams& params, float time_seconds) {
    const auto finite = [](float value) { return std::isfinite(value); };
    if (!finite(time_seconds) || !finite(params.bevel_width) ||
        !finite(params.bevel_height) || !finite(params.rim_width) ||
        !finite(params.rim_intensity) || !finite(params.highlight) ||
        !finite(params.shade) || !finite(params.light_angle) ||
        !finite(params.light_elevation) || !finite(params.light_orbit_speed) ||
        !finite(params.shadow_distance) || !finite(params.shadow_softness) ||
        !finite(params.shadow_opacity) || !finite(params.rim_color.r) ||
        !finite(params.rim_color.g) || !finite(params.rim_color.b) ||
        !finite(params.rim_color.a) || !finite(params.shadow_color.r) ||
        !finite(params.shadow_color.g) || !finite(params.shadow_color.b) ||
        !finite(params.shadow_color.a)) {
        throw std::invalid_argument(
            "Vulkan SDF bevel parameters must be finite");
    }
    auto& dst_image = resolve_image(destination);
    auto& src_image = resolve_image(source);
    auto& a_image = resolve_image(scratch_a);
    auto& b_image = resolve_image(scratch_b);
    auto& c_image = resolve_image(scratch_c);
    if (!src_image.initialized ||
        dst_image.width != src_image.width ||
        dst_image.height != src_image.height ||
        a_image.width != src_image.width ||
        a_image.height != src_image.height ||
        b_image.width != src_image.width ||
        b_image.height != src_image.height ||
        c_image.width != src_image.width ||
        c_image.height != src_image.height) {
        throw std::invalid_argument(
            "Vulkan SDF bevel references incompatible surfaces");
    }
    if (!frame_batch.active) {
        throw std::logic_error(
            "Vulkan SDF bevel requires an active frame batch");
    }

    constexpr float kPi = 3.14159265358979323846f;
    const float azimuth =
        (params.light_angle + params.light_orbit_speed * time_seconds) *
        (kPi / 180.0f);
    const float elevation =
        std::clamp(params.light_elevation, 0.0f, 89.0f) * (kPi / 180.0f);
    const float lx = std::cos(azimuth) * std::cos(elevation);
    const float ly = std::sin(azimuth) * std::cos(elevation);
    const float lz = std::sin(elevation);

    SdfBevelPush push{
        params.rim_color.r, params.rim_color.g, params.rim_color.b,
        params.rim_color.a,
        params.shadow_color.r, params.shadow_color.g, params.shadow_color.b,
        params.shadow_color.a,
        lx, ly, lz, 0.0f,
        params.bevel_width, params.bevel_height, params.rim_width,
        params.rim_intensity,
        params.highlight, params.shade, params.shadow_distance,
        params.shadow_softness,
        params.shadow_opacity, static_cast<float>(dst_image.width),
        static_cast<float>(dst_image.height), 0.0f};

    const auto d0 = allocate_pass_descriptor_set();
    write_matte_descriptors(d0, a_image, src_image, src_image);
    const auto d1 = allocate_pass_descriptor_set();
    write_matte_descriptors(d1, b_image, a_image, src_image);
    const auto d2 = allocate_pass_descriptor_set();
    write_matte_descriptors(d2, c_image, b_image, b_image);
    const auto d3 = allocate_pass_descriptor_set();
    write_matte_descriptors(d3, a_image, c_image, b_image);
    const auto d4 = allocate_pass_descriptor_set();
    write_matte_descriptors(d4, dst_image, src_image, a_image);

    const auto cmd = active_command_buffer();
    const auto dispatch = [&](VkDescriptorSet descriptors, Image& out,
                              const Image& in, const Image& aux, int mode) {
        emit_pass_sync(cmd, {&out, &in, &aux}, runtime::GpuPassKind::Blur);
        push.mode = static_cast<float>(mode);
        record_sdf_bevel(cmd, descriptors, out, in, push);
        out.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
    };
    dispatch(d0, a_image, src_image, src_image, 0);
    dispatch(d1, b_image, a_image, src_image, 1);
    dispatch(d2, c_image, b_image, b_image, 2);
    dispatch(d3, a_image, c_image, b_image, 3);
    dispatch(d4, dst_image, src_image, a_image, 4);
}

void VulkanBackend::Impl::warp_bulk(runtime::RenderSurfaceHandle destination,
                                    runtime::RenderSurfaceHandle source,
                                    runtime::RenderSurfaceHandle aux,
                                    const graph::WarpBulkParams& params) {
    if (params.mode < 0 || params.mode > 15 ||
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
