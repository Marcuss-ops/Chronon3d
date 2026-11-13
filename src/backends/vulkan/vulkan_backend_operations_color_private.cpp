// vulkan_backend_operations_color_private.cpp — glow, bloom, color, lighting and matte op wrappers.
// Split from the oversized unit per issue #51 (LEANUP-001);
// out-of-class definitions, declared in vulkan_backend_impl.hpp.

#include "vulkan_backend_impl.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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
    GlowAdvancedParams params;
    params.radius = radius;
    params.intensity = intensity;
    params.color_r = tint.r;
    params.color_g = tint.g;
    params.color_b = tint.b;
    params.color_a = tint.a;
    params.color_b_r = tint.r;
    params.color_b_g = tint.g;
    params.color_b_b = tint.b;
    params.color_b_a = tint.a;
    params.threshold = 0.0f;
    params.falloff = 1.0f;
    glow_advanced(destination, source, scratch_horizontal, scratch_vertical, params);
}

void VulkanBackend::Impl::glow_advanced(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    runtime::RenderSurfaceHandle scratch_horizontal,
    runtime::RenderSurfaceHandle scratch_vertical,
    const GlowAdvancedParams& params) {
    if (!std::isfinite(params.radius) || params.radius < 0.0f || params.radius > 96.0f) {
        throw std::invalid_argument("Vulkan glow radius must be within [0, 96]");
    }
    if (!std::isfinite(params.intensity) || params.intensity < 0.0f ||
        params.intensity > 4.0f) {
        throw std::invalid_argument("Vulkan glow intensity must be within [0, 4]");
    }
    if (!std::isfinite(params.threshold) || params.threshold < 0.0f ||
        params.threshold > 1.0f || !std::isfinite(params.falloff) ||
        std::abs(params.falloff - 1.0f) > 1.0e-6f) {
        throw std::invalid_argument(
            "Vulkan glow advanced requires threshold in [0,1] and falloff == 1");
    }
    for (float value : {params.color_r, params.color_g, params.color_b, params.color_a,
                        params.color_b_r, params.color_b_g, params.color_b_b,
                        params.color_b_a}) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument("Vulkan glow advanced colors must be finite");
        }
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
    if (params.radius < 0.5f || params.intensity <= 0.0f) return;

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
        record_glow_prepare_advanced(cmd, prepare_descriptor, horizontal, src_image,
                                     params);
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
            0.5f, params.radius / (3.0f * std::max(scale_x, scale_y)));
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

void VulkanBackend::Impl::inner_glow_composite(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    const Color& tint, float intensity) {
    if (!std::isfinite(intensity) || intensity < 0.0f || intensity > 4.0f) {
        throw std::invalid_argument("Vulkan inner glow intensity must be within [0, 4]");
    }
    for (float value : {tint.r, tint.g, tint.b, tint.a}) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument("Vulkan inner glow tint must be finite");
        }
    }
    auto& dst = resolve_image(destination);
    auto& src_image = resolve_image(source);
    if (!src_image.initialized || !dst.initialized ||
        dst.width != src_image.width || dst.height != src_image.height) {
        throw std::invalid_argument("Vulkan inner glow references incompatible surfaces");
    }
    if (!frame_batch.active)
        throw std::logic_error("Vulkan inner glow requires an active frame batch");
    const auto descriptors = allocate_pass_descriptor_set();
    write_descriptors(descriptors, dst, src_image);
    const auto cmd = active_command_buffer();
    emit_pass_sync(cmd, {&dst, &src_image}, runtime::GpuPassKind::Composite);
    const float color[4]{tint.r, tint.g, tint.b, tint.a};
    record_composite(cmd, descriptors, dst, src_image, 10, intensity, color);
    dst.initialized = true;
    ++frame_batch.pass_count;
    ++stats.passes_executed;
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

void VulkanBackend::Impl::bloom_pyramid(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    runtime::RenderSurfaceHandle scratch_full,
    std::span<const runtime::RenderSurfaceHandle> mips,
    float threshold, float radius, float intensity) {
    if (!std::isfinite(threshold) || threshold < 0.0f || threshold > 1.0f ||
        !std::isfinite(radius) || radius < 0.0f || radius > 96.0f ||
        !std::isfinite(intensity) || intensity < 0.0f || intensity > 4.0f) {
        throw std::invalid_argument(
            "Vulkan bloom pyramid parameters are outside supported ranges");
    }
    if (mips.size() < 2 || mips.size() > 8) {
        throw std::invalid_argument(
            "Vulkan bloom pyramid requires between 2 and 8 mip surfaces");
    }

    auto& dst = resolve_image(destination);
    auto& src_image = resolve_image(source);
    auto& full = resolve_image(scratch_full);
    if (!src_image.initialized ||
        src_image.width != dst.width || src_image.height != dst.height ||
        full.width != src_image.width || full.height != src_image.height) {
        throw std::invalid_argument(
            "Vulkan bloom pyramid has incompatible full-resolution surfaces");
    }
    if (full.format != VK_FORMAT_R16G16B16A16_SFLOAT) {
        throw std::invalid_argument(
            "Vulkan bloom pyramid full-resolution scratch must be R16G16B16A16_SFLOAT");
    }

    std::array<Image*, 8> levels{};
    std::uint32_t expected_width = src_image.width;
    std::uint32_t expected_height = src_image.height;
    for (std::size_t i = 0; i < mips.size(); ++i) {
        auto& level = resolve_image(mips[i]);
        expected_width = std::max(1u, expected_width / 2u);
        expected_height = std::max(1u, expected_height / 2u);
        if (level.width != expected_width || level.height != expected_height) {
            throw std::invalid_argument(
                "Vulkan bloom pyramid mip dimensions do not form a half-resolution chain");
        }
        if (level.format != VK_FORMAT_R16G16B16A16_SFLOAT) {
            throw std::invalid_argument(
                "Vulkan bloom pyramid mip surfaces must be R16G16B16A16_SFLOAT");
        }
        levels[i] = &level;
    }
    if (!frame_batch.active) {
        throw std::logic_error("Vulkan bloom pyramid requires an active frame batch");
    }

    const auto command = active_command_buffer();
    emit_pass_sync(command,
                   {&dst, &src_image, &full,
                    levels[0], levels[1], levels[2], levels[3],
                    levels[4], levels[5], levels[6], levels[7]},
                   runtime::GpuPassKind::Bloom);

    const auto transition = [&](Image& image, bool write_to_read) {
        runtime::ResourceTransition barrier;
        barrier.range = runtime::image_range(runtime::ResourceAspect::Color);
        barrier.before = runtime::ResourceState{
            .stages = runtime::PipelineStage::ComputeShader,
            .access = write_to_read ? runtime::AccessMask::ShaderWrite
                                    : runtime::AccessMask::ShaderRead,
            .layout = runtime::ResourceLayout::General,
            .queue = runtime::QueueClass::Compute,
        };
        barrier.after = runtime::ResourceState{
            .stages = runtime::PipelineStage::ComputeShader,
            .access = write_to_read ? runtime::AccessMask::ShaderRead
                                    : runtime::AccessMask::ShaderWrite,
            .layout = runtime::ResourceLayout::General,
            .queue = runtime::QueueClass::Compute,
        };
        emit_resource_transition(command, image.image, barrier);
    };

    // Pass 0 prefilters the scene (rgba32f) into mip 0 (rgba16f): soft-knee
    // threshold plus the Karis-weighted 13-tap kernel. The remaining levels
    // average the already-filtered half-float energy.
    {
        Image& destination_mip = *levels[0];
        const auto descriptors = allocate_pass_descriptor_set();
        write_descriptors(descriptors, destination_mip, src_image);
        record_bloom_prefilter(command, descriptors, destination_mip, src_image,
                               threshold, 0.5f);
        destination_mip.initialized = true;
        transition(destination_mip, true);
    }
    for (std::size_t i = 1; i < mips.size(); ++i) {
        Image& destination_mip = *levels[i];
        const Image& source_mip = *levels[i - 1];
        const auto descriptors = allocate_pass_descriptor_set();
        write_descriptors(descriptors, destination_mip, source_mip);
        record_bloom_downsample(command, descriptors, destination_mip, source_mip);
        destination_mip.initialized = true;
        transition(destination_mip, true);
    }

    // Upsample from the coarsest level, adding each tent-filtered level to
    // the already-downsampled data stored in the next finer mip.
    for (std::size_t i = mips.size(); i-- > 1;) {
        Image& destination_mip = *levels[i - 1];
        Image& source_mip = *levels[i];
        transition(destination_mip, false);
        const auto descriptors = allocate_pass_descriptor_set();
        write_descriptors(descriptors, destination_mip, source_mip);
        record_bloom_upsample(command, descriptors, destination_mip, source_mip,
                              1.0f, 1.0f, true);
        transition(destination_mip, true);
    }

    // The final upsample writes the full-resolution bloom texture; the
    // existing composite kernel then adds it to the source with linear
    // additive energy instead of the saturating screen operator.
    transition(full, false);
    const auto final_descriptors = allocate_pass_descriptor_set();
    write_descriptors(final_descriptors, full, *levels[0]);
    record_bloom_upsample(command, final_descriptors, full, *levels[0],
                          1.0f, 1.0f, false);
    full.initialized = true;
    transition(full, true);

    // Add the rgba16f bloom to the rgba32f layer without clamping: energy above
    // 1.0 must survive for the output transform (exposure + ACES) to compress.
    const auto composite_descriptors = allocate_pass_descriptor_set();
    write_descriptors(composite_descriptors, dst, full);
    record_bloom_composite(command, composite_descriptors, dst, full, intensity);
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
    lighting_points(destination, source, multiplier, {}, 0,
                    Vec3{0.0f, 0.0f, 1.0f}, 0.0f, 0, 0, 0.0f);
}

void VulkanBackend::Impl::lighting_points(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    const Color& multiplier,
    std::span<const float> packed_lights,
    std::uint32_t light_count,
    Vec3 normal,
    float layer_z,
    int origin_x,
    int origin_y,
    float diffuse) {
    constexpr std::size_t kFloatsPerLight = 16;
    constexpr std::uint32_t kMaxGpuLights = 64;
    if (light_count > kMaxGpuLights ||
        packed_lights.size() != static_cast<std::size_t>(light_count) * kFloatsPerLight) {
        throw std::invalid_argument("Vulkan lighting light payload has invalid count or size");
    }
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
        if (frame_batch.pass_count >= kPathGeometryPassesPerSlot)
            throw std::logic_error("Vulkan lighting pass capacity exhausted");
        const std::size_t buffer_slot = 1U + frame_batch.next_slot *
            kPathGeometryPassesPerSlot + frame_batch.pass_count;
        const std::size_t light_floats = std::max<std::size_t>(
            kFloatsPerLight, packed_lights.size());
        const VkDeviceSize light_bytes = static_cast<VkDeviceSize>(
            light_floats * sizeof(float));
        ensure_path_geometry_buffer(light_bytes, buffer_slot);
        auto& light_buffer = path_geometry_buffers[buffer_slot];
        if (!light_buffer.mapped)
            throw std::logic_error("Vulkan lighting light buffer is not host mapped");
        std::memset(light_buffer.mapped, 0, light_bytes);
        if (!packed_lights.empty())
            std::memcpy(light_buffer.mapped, packed_lights.data(), packed_lights.size_bytes());
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
        record_lighting(cmd, descriptors, dst, src_image, multiplier, normal,
                        layer_z, origin_x, origin_y, diffuse, light_count,
                        light_buffer.buffer, light_bytes);
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
                                bool luma, bool inverted,
                                int matte_origin_x, int matte_origin_y) {
    auto& dst = resolve_image(destination);
    auto& target_image = resolve_image(target);
    auto& matte_image = resolve_image(matte_surface);
    const std::int64_t matte_x1 = static_cast<std::int64_t>(matte_origin_x) + dst.width;
    const std::int64_t matte_y1 = static_cast<std::int64_t>(matte_origin_y) + dst.height;
    if (!target_image.initialized || !matte_image.initialized ||
        dst.width != target_image.width || dst.height != target_image.height ||
        matte_x1 <= 0 || matte_y1 <= 0 ||
        matte_origin_x >= static_cast<int>(matte_image.width) ||
        matte_origin_y >= static_cast<int>(matte_image.height)) {
        throw std::invalid_argument("Vulkan matte references incompatible surfaces");
    }
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_matte_descriptors(descriptors, dst, target_image, matte_image);
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst, &target_image, &matte_image}, runtime::GpuPassKind::Matte);
        record_matte(cmd, descriptors, dst, target_image, matte_image,
                     luma, inverted, matte_origin_x, matte_origin_y);
        dst.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error("Vulkan matte requires an active frame batch");
}
} // namespace chronon3d::backends::vulkan
