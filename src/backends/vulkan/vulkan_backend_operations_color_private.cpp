// vulkan_backend_operations_color_private.cpp — glow, bloom, color, lighting and matte op wrappers.
// Split from the oversized unit per issue #51 (LEANUP-001);
// out-of-class definitions, declared in vulkan_backend_impl.hpp.

#include "vulkan_backend_impl.hpp"

#include <cstdlib>
#include <type_traits>

namespace chronon3d::backends::vulkan {
namespace {

template <typename HandleT>
std::uint64_t t05_handle_value(HandleT handle) noexcept {
    if constexpr (std::is_pointer_v<HandleT>) {
        return static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(handle));
    } else {
        return static_cast<std::uint64_t>(handle);
    }
}

} // namespace

void VulkanBackend::Impl::glow(runtime::RenderSurfaceHandle destination,
                               runtime::RenderSurfaceHandle source,
                               runtime::RenderSurfaceHandle scratch_horizontal,
                               runtime::RenderSurfaceHandle scratch_vertical,
                               float radius, float intensity,
                               const Color& tint) {
    if (!(radius >= 0.0f) || radius > 96.0f) {
        throw std::invalid_argument("Vulkan glow radius must be within [0, 96]");
    }
    auto& dst = resolve_image(destination);
    auto& src_image = resolve_image(source);
    auto& horizontal = resolve_image(scratch_horizontal);
    auto& vertical = resolve_image(scratch_vertical);
    if (!src_image.initialized || !dst.initialized ||
        src_image.width != dst.width || src_image.height != dst.height ||
        horizontal.width != vertical.width || horizontal.height != vertical.height ||
        horizontal.width <= 0 || horizontal.height <= 0 ||
        horizontal.width > src_image.width || horizontal.height > src_image.height) {
        throw std::invalid_argument("Vulkan glow surfaces have incompatible dimensions");
    }
    if (radius < 0.5f || intensity <= 0.0f) return;

    if (frame_batch.active) {
        const auto prepare_descriptor = allocate_pass_descriptor_set();
        write_descriptors(prepare_descriptor, horizontal, src_image);
        const auto horizontal_blur_descriptor = allocate_pass_descriptor_set();
        write_descriptors(horizontal_blur_descriptor, vertical, horizontal);
        const auto vertical_blur_descriptor = allocate_pass_descriptor_set();
        write_descriptors(vertical_blur_descriptor, horizontal, vertical);
        const auto composite_descriptor = allocate_pass_descriptor_set();
        write_descriptors(composite_descriptor, dst, horizontal);
        const auto cmd = active_command_buffer();

        // One command-plan pass => consume its canonical transition stream
        // exactly once. Internal dispatch dependencies use the same
        // ResourceTransition -> Sync2 translator.
        emit_pass_sync(cmd, {&dst, &src_image, &horizontal, &vertical}, runtime::GpuPassKind::Glow);
        record_glow_prepare(cmd, prepare_descriptor, horizontal, src_image,
                            intensity, tint);
        horizontal.initialized = true;

        runtime::ResourceTransition writable_to_readable;
        writable_to_readable.range = runtime::image_range(runtime::ResourceAspect::Color);
        writable_to_readable.before = runtime::ResourceState{
            .stages = runtime::PipelineStage::ComputeShader,
            .access = runtime::AccessMask::ShaderWrite,
            .layout = runtime::ResourceLayout::General,
            .queue = runtime::QueueClass::Compute,
        };
        writable_to_readable.after = runtime::ResourceState{
            .stages = runtime::PipelineStage::ComputeShader,
            .access = runtime::AccessMask::ShaderRead,
            .layout = runtime::ResourceLayout::General,
            .queue = runtime::QueueClass::Compute,
        };
        emit_resource_transition(cmd, horizontal.image, writable_to_readable);

        const float scale_x = static_cast<float>(src_image.width) /
                              static_cast<float>(horizontal.width);
        const float scale_y = static_cast<float>(src_image.height) /
                              static_cast<float>(horizontal.height);
        const float sigma = std::max(
            0.5f, radius / (3.0f * std::max(scale_x, scale_y)));
        record_blur(cmd, horizontal_blur_descriptor, vertical, horizontal,
                    sigma, true, false, true);
        vertical.initialized = true;
        emit_resource_transition(cmd, vertical.image, writable_to_readable);

        runtime::ResourceTransition horizontal_for_write = writable_to_readable;
        horizontal_for_write.before = runtime::ResourceState{
            .stages = runtime::PipelineStage::ComputeShader,
            .access = runtime::AccessMask::ShaderRead,
            .layout = runtime::ResourceLayout::General,
            .queue = runtime::QueueClass::Compute,
        };
        horizontal_for_write.after = runtime::ResourceState{
            .stages = runtime::PipelineStage::ComputeShader,
            .access = runtime::AccessMask::ShaderWrite,
            .layout = runtime::ResourceLayout::General,
            .queue = runtime::QueueClass::Compute,
        };
        emit_resource_transition(cmd, horizontal.image, horizontal_for_write);
        record_blur(cmd, vertical_blur_descriptor, horizontal, vertical,
                    sigma, false, false, true);
        emit_resource_transition(cmd, horizontal.image, writable_to_readable);

        constexpr float identity_tint[4]{1.0f, 1.0f, 1.0f, 1.0f};
        record_composite(cmd, composite_descriptor, dst, horizontal,
                         8, 1.0f, identity_tint);
        dst.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }

    throw std::logic_error("Vulkan glow requires an active frame batch");
}

void VulkanBackend::Impl::bloom(runtime::RenderSurfaceHandle destination,
                                runtime::RenderSurfaceHandle source,
                                runtime::RenderSurfaceHandle scratch_horizontal,
                                runtime::RenderSurfaceHandle scratch_vertical,
                                float threshold, float radius, float intensity) {
    if (!std::isfinite(threshold) || threshold < 0.0f || threshold > 1.0f ||
        !std::isfinite(radius) || radius < 0.0f || radius > 64.0f ||
        !std::isfinite(intensity) || intensity < 0.0f || intensity > 4.0f) {
        throw std::invalid_argument("Vulkan bloom parameters are outside supported ranges");
    }
    auto& dst = resolve_image(destination);
    auto& src_image = resolve_image(source);
    auto& horizontal = resolve_image(scratch_horizontal);
    auto& vertical = resolve_image(scratch_vertical);
    if (!src_image.initialized || !dst.initialized ||
        src_image.width != dst.width || src_image.height != dst.height ||
        horizontal.width != src_image.width || horizontal.height != src_image.height ||
        vertical.width != src_image.width || vertical.height != src_image.height) {
        throw std::invalid_argument("Vulkan bloom surfaces have incompatible dimensions");
    }
    if (!frame_batch.active)
        throw std::logic_error("Vulkan bloom requires an active frame batch");

    const auto threshold_descriptor = allocate_pass_descriptor_set();
    write_descriptors(threshold_descriptor, horizontal, src_image);
    VkDescriptorSet horizontal_blur_descriptor = VK_NULL_HANDLE;
    VkDescriptorSet vertical_blur_descriptor = VK_NULL_HANDLE;
    const bool blur_enabled = radius > 0.5f;
    const float box_radius = std::ceil(radius);
    if (blur_enabled) {
        horizontal_blur_descriptor = allocate_pass_descriptor_set();
        write_descriptors(horizontal_blur_descriptor, vertical, horizontal);
        vertical_blur_descriptor = allocate_pass_descriptor_set();
        write_descriptors(vertical_blur_descriptor, horizontal, vertical);
    }
    const auto composite_descriptor = allocate_pass_descriptor_set();
    write_descriptors(composite_descriptor, dst, horizontal);
    const auto command = active_command_buffer();
    emit_pass_sync(command, {&dst, &src_image, &horizontal, &vertical}, runtime::GpuPassKind::Bloom);
    record_bloom_threshold(command, threshold_descriptor, horizontal, src_image, threshold);
    horizontal.initialized = true;

    runtime::ResourceTransition ready_for_read;
    ready_for_read.range = runtime::image_range(runtime::ResourceAspect::Color);
    ready_for_read.before = runtime::ResourceState{
        .stages = runtime::PipelineStage::ComputeShader,
        .access = runtime::AccessMask::ShaderWrite,
        .layout = runtime::ResourceLayout::General,
        .queue = runtime::QueueClass::Compute,
    };
    ready_for_read.after = runtime::ResourceState{
        .stages = runtime::PipelineStage::ComputeShader,
        .access = runtime::AccessMask::ShaderRead,
        .layout = runtime::ResourceLayout::General,
        .queue = runtime::QueueClass::Compute,
    };
    if (blur_enabled) {
        emit_resource_transition(command, horizontal.image, ready_for_read);
        record_blur(command, horizontal_blur_descriptor, vertical, horizontal,
                    box_radius, true, true);
        vertical.initialized = true;
        emit_resource_transition(command, vertical.image, ready_for_read);
        runtime::ResourceTransition horizontal_for_write = ready_for_read;
        horizontal_for_write.before = runtime::ResourceState{
            .stages = runtime::PipelineStage::ComputeShader,
            .access = runtime::AccessMask::ShaderRead,
            .layout = runtime::ResourceLayout::General,
            .queue = runtime::QueueClass::Compute,
        };
        horizontal_for_write.after = runtime::ResourceState{
            .stages = runtime::PipelineStage::ComputeShader,
            .access = runtime::AccessMask::ShaderWrite,
            .layout = runtime::ResourceLayout::General,
            .queue = runtime::QueueClass::Compute,
        };
        emit_resource_transition(command, horizontal.image, horizontal_for_write);
        record_blur(command, vertical_blur_descriptor, horizontal, vertical,
                    box_radius, false, true);
        horizontal.initialized = true;
        emit_resource_transition(command, horizontal.image, ready_for_read);
    } else {
        emit_resource_transition(command, horizontal.image, ready_for_read);
    }
    constexpr float identity_tint[4]{1.0f, 1.0f, 1.0f, 1.0f};
    record_composite(command, composite_descriptor, dst, horizontal,
                     6, intensity, identity_tint);
    dst.initialized = true;
    ++frame_batch.pass_count;
    ++stats.passes_executed;
}

void VulkanBackend::Impl::color_adjust(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    float brightness, float contrast,
    const Color& tint, float tint_amount) {
    auto& dst = resolve_image(destination);
    auto& src_image = resolve_image(source);
    if (!src_image.initialized ||
        dst.width != src_image.width || dst.height != src_image.height) {
        throw std::invalid_argument(
            "Vulkan color adjust references incompatible surfaces");
    }
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_descriptors(descriptors, dst, src_image);
        if (std::getenv("CHRONON3D_T05_DESCRIPTOR_DIAG")) {
            spdlog::info(
                "[t05-descriptor] op=color-adjust pass={} set=0x{:x} "
                "src_image=0x{:x} src_view=0x{:x} src_alloc=0x{:x} "
                "src_extent={}x{} src_init={} dst_image=0x{:x} "
                "dst_view=0x{:x} dst_alloc=0x{:x} dst_extent={}x{} dst_init={}",
                frame_batch.pass_count, t05_handle_value(descriptors),
                t05_handle_value(src_image.image), t05_handle_value(src_image.view),
                t05_handle_value(src_image.allocation), src_image.width,
                src_image.height, src_image.initialized,
                t05_handle_value(dst.image), t05_handle_value(dst.view),
                t05_handle_value(dst.allocation), dst.width, dst.height,
                dst.initialized);
        }
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst, &src_image}, runtime::GpuPassKind::ColorAdjust);
        record_color_adjust(cmd, descriptors, dst, src_image,
                            brightness, contrast, tint, tint_amount);
        dst.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error("Vulkan color_adjust requires an active frame batch");
}

void VulkanBackend::Impl::color_transform(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    const graph::NativeColorTransform& transform) {
    auto& dst = resolve_image(destination);
    auto& src_image = resolve_image(source);
    if (!src_image.initialized ||
        dst.width != src_image.width || dst.height != src_image.height) {
        throw std::invalid_argument(
            "Vulkan color transform references incompatible surfaces");
    }
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_descriptors(descriptors, dst, src_image);
        if (std::getenv("CHRONON3D_T05_DESCRIPTOR_DIAG")) {
            spdlog::info(
                "[t05-descriptor] op=color-transform pass={} set=0x{:x} "
                "src_image=0x{:x} src_view=0x{:x} src_alloc=0x{:x} "
                "src_extent={}x{} src_init={} dst_image=0x{:x} "
                "dst_view=0x{:x} dst_alloc=0x{:x} dst_extent={}x{} dst_init={}",
                frame_batch.pass_count, t05_handle_value(descriptors),
                t05_handle_value(src_image.image), t05_handle_value(src_image.view),
                t05_handle_value(src_image.allocation), src_image.width,
                src_image.height, src_image.initialized,
                t05_handle_value(dst.image), t05_handle_value(dst.view),
                t05_handle_value(dst.allocation), dst.width, dst.height,
                dst.initialized);
        }
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst, &src_image}, runtime::GpuPassKind::ColorAdjust);
        record_color_transform(cmd, descriptors, dst, src_image, transform);
        dst.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error("Vulkan color_transform requires an active frame batch");
}

void VulkanBackend::Impl::chromatic_transform(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    const ChromaticAberrationParams& params) {
    const auto finite = [](float value) { return std::isfinite(value); };
    const auto valid_channel = [&](const RgbChannelTransform& channel) {
        return finite(channel.translation.x) && finite(channel.translation.y) &&
            std::abs(channel.translation.x) <= 2048.0f &&
            std::abs(channel.translation.y) <= 2048.0f &&
            finite(channel.scale.x) && finite(channel.scale.y) &&
            channel.scale.x >= 0.01f && channel.scale.x <= 100.0f &&
            channel.scale.y >= 0.01f && channel.scale.y <= 100.0f &&
            finite(channel.rotation_degrees) &&
            std::abs(channel.rotation_degrees) <= 3600.0f &&
            finite(channel.opacity) && channel.opacity >= 0.0f && channel.opacity <= 1.0f;
    };
    if (!finite(params.red_offset) || !finite(params.blue_offset) ||
        std::abs(params.red_offset) > 128.0f || std::abs(params.blue_offset) > 128.0f ||
        !finite(params.center.x) || !finite(params.center.y) ||
        std::abs(params.center.x) > 1.0e6f || std::abs(params.center.y) > 1.0e6f ||
        !finite(params.velocity.x) || !finite(params.velocity.y) ||
        std::abs(params.velocity.x) > 1.0e6f || std::abs(params.velocity.y) > 1.0e6f ||
        !valid_channel(params.red) || !valid_channel(params.green) || !valid_channel(params.blue))
        throw std::invalid_argument("Vulkan RGB channel parameters are outside supported ranges");
    switch (params.offset_mode) {
        case ChromaticOffsetMode::Constant:
        case ChromaticOffsetMode::Radial:
        case ChromaticOffsetMode::Velocity: break;
        default: throw std::invalid_argument("Vulkan RGB offset mode is invalid");
    }
    auto& dst = resolve_image(destination);
    auto& src_image = resolve_image(source);
    if (destination == source)
        throw std::invalid_argument("Vulkan RGB transform requires distinct source and destination surfaces");
    if (!src_image.initialized || dst.width != src_image.width || dst.height != src_image.height)
        throw std::invalid_argument("Vulkan RGB transform references incompatible surfaces");
    if (!frame_batch.active)
        throw std::logic_error("Vulkan RGB transform requires an active frame batch");
    const auto descriptors = allocate_pass_descriptor_set();
    write_descriptors(descriptors, dst, src_image);
    const auto command = active_command_buffer();
    emit_pass_sync(command, {&dst, &src_image}, runtime::GpuPassKind::ColorAdjust);
    record_chromatic_transform(command, descriptors, dst, src_image, params);
    dst.initialized = true;
    ++frame_batch.pass_count;
    ++stats.passes_executed;
}

void VulkanBackend::Impl::lighting(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    const Color& multiplier) {
    auto& dst = resolve_image(destination);
    auto& src_image = resolve_image(source);
    if (dst.image == src_image.image) {
        throw std::invalid_argument(
            "Vulkan lighting destination aliases its source image: destination=" +
            std::to_string(destination) + " source=" + std::to_string(source));
    }
    if (!src_image.initialized ||
        dst.width != src_image.width || dst.height != src_image.height) {
        throw std::invalid_argument(
            "Vulkan lighting references incompatible surfaces: destination=" +
            std::to_string(destination) + " " + std::to_string(dst.width) + "x" +
            std::to_string(dst.height) + " source=" + std::to_string(source) +
            " " + std::to_string(src_image.width) + "x" +
            std::to_string(src_image.height) + " initialized=" +
            (src_image.initialized ? "true" : "false"));
    }
    if (std::getenv("CHRONON3D_T05_SURFACE_ID_DIAG")) {
        spdlog::info(
            "[t05-surface-identity] op=lighting src={} src_image=0x{:x} "
            "dst={} dst_image=0x{:x} src_init={} dst_init={} pass={} plan={} "
            "multiplier={:.6f},{:.6f},{:.6f}",
            source, t05_handle_value(src_image.image), destination,
            t05_handle_value(dst.image), src_image.initialized,
            dst.initialized, frame_batch.pass_count,
            frame_batch.command_plan != nullptr,
            multiplier.r, multiplier.g, multiplier.b);
    }
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_descriptors(descriptors, dst, src_image);
        if (std::getenv("CHRONON3D_T05_DESCRIPTOR_DIAG")) {
            spdlog::info(
                "[t05-descriptor] op=lighting pass={} set=0x{:x} "
                "src_image=0x{:x} src_view=0x{:x} src_alloc=0x{:x} "
                "src_extent={}x{} src_init={} dst_image=0x{:x} "
                "dst_view=0x{:x} dst_alloc=0x{:x} dst_extent={}x{} dst_init={}",
                frame_batch.pass_count, t05_handle_value(descriptors),
                t05_handle_value(src_image.image), t05_handle_value(src_image.view),
                t05_handle_value(src_image.allocation), src_image.width,
                src_image.height, src_image.initialized,
                t05_handle_value(dst.image), t05_handle_value(dst.view),
                t05_handle_value(dst.allocation), dst.width, dst.height,
                dst.initialized);
        }
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst, &src_image}, runtime::GpuPassKind::ColorAdjust);
        record_lighting(cmd, descriptors, dst, src_image, multiplier);
        dst.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error("Vulkan lighting requires an active frame batch");
}

void VulkanBackend::Impl::matte(runtime::RenderSurfaceHandle destination,
                                runtime::RenderSurfaceHandle target,
                                runtime::RenderSurfaceHandle matte_surface,
                                bool luma, bool inverted) {
    auto& dst = resolve_image(destination);
    auto& target_image = resolve_image(target);
    auto& matte_image = resolve_image(matte_surface);
    if (!target_image.initialized || !matte_image.initialized ||
        dst.width != target_image.width || dst.height != target_image.height ||
        dst.width != matte_image.width || dst.height != matte_image.height) {
        throw std::invalid_argument("Vulkan matte references incompatible surfaces");
    }
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_matte_descriptors(descriptors, dst, target_image, matte_image);
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst, &target_image, &matte_image}, runtime::GpuPassKind::Matte);
        record_matte(cmd, descriptors, dst, target_image, matte_image,
                     luma, inverted);
        dst.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error("Vulkan matte requires an active frame batch");
}
} // namespace chronon3d::backends::vulkan
