// vulkan_backend_operations_private.cpp — VulkanBackend::Impl render pass
// operation wrappers (composite, fill, transform, blur, glow, matte…).
//
// Text batching, layer batching, resource helpers and submission flow live in
// vulkan_backend_text_ops_private.cpp, vulkan_backend_layer_ops_private.cpp,
// vulkan_backend_resources_private.cpp and vulkan_backend_submission_private.cpp.

#include "vulkan_backend_impl.hpp"

#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace chronon3d::backends::vulkan {

void VulkanBackend::Impl::composite(runtime::RenderSurfaceHandle destination,
                                    runtime::RenderSurfaceHandle source,
                                    BlendMode mode,
                                    const std::optional<raster::BBox>& clip,
                                    bool replace) {
    auto& dst_image = resolve_image(destination);
    auto& src_image = resolve_image(source);
    if (!src_image.initialized ||
        dst_image.width != src_image.width ||
        dst_image.height != src_image.height) {
        throw std::invalid_argument(
            "Vulkan composite references incompatible surfaces: dst=" +
            std::to_string(dst_image.width) + "x" +
            std::to_string(dst_image.height) + " src=" +
            std::to_string(src_image.width) + "x" +
            std::to_string(src_image.height) +
            (src_image.initialized ? "" : " src-uninitialized"));
    }
    const std::int32_t blend_mode = replace ? 2 :
        (mode == BlendMode::Add ? 1 : mode == BlendMode::Multiply ? 3 :
         mode == BlendMode::Screen ? 4 : mode == BlendMode::Overlay ? 5 : 0);
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_descriptors(descriptors, dst_image, src_image);
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst_image, &src_image});
        record_composite(cmd, descriptors, dst_image, src_image,
                         blend_mode, 1.0f, kIdentityTint, clip);
        dst_image.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error("Vulkan composite requires an active frame batch");
}

void VulkanBackend::Impl::fill_rect(runtime::RenderSurfaceHandle destination,
                                    std::int32_t x0, std::int32_t y0,
                                    std::int32_t x1, std::int32_t y1,
                                    const Color& color) {
    auto& dst_image = resolve_image(destination);
    if (dst_image.width == 0 || dst_image.height == 0) {
        throw std::invalid_argument("Vulkan fill_rect references an empty surface");
    }
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_fill_rect_descriptors(descriptors, dst_image);
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst_image});
        record_fill_rect(cmd, descriptors, dst_image, x0, y0, x1, y1, color);
        dst_image.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error("Vulkan fill_rect requires an active frame batch");
}

void VulkanBackend::Impl::initialize_transparent_surface(
    runtime::RenderSurfaceHandle destination) {
    auto& dst_image = resolve_image(destination);
    if (dst_image.width == 0 || dst_image.height == 0) {
        throw std::invalid_argument("Vulkan transparent initialization references an empty surface");
    }
    if (!frame_batch.active) {
        throw std::logic_error("Vulkan transparent initialization requires an active frame batch");
    }
    const auto descriptors = allocate_pass_descriptor_set();
    write_fill_rect_descriptors(descriptors, dst_image);
    const auto cmd = active_command_buffer();
    // Initialization is deliberately not part of the compiled pass index.
    // It establishes transparent untouched pixels before the first planned
    // affine/composite operation without shifting plan transitions.
    emit_unplanned_compute_sync(cmd, {&dst_image});
    record_fill_rect(cmd, descriptors, dst_image, 0, 0,
                     static_cast<std::int32_t>(dst_image.width),
                     static_cast<std::int32_t>(dst_image.height),
                     Color::transparent());
    dst_image.initialized = true;
    // The initialization dispatch is intentionally outside the compiled
    // pass stream. Publish its shader writes explicitly before the first
    // planned pass touches this image; otherwise that pass may still use the
    // plan's pre-initialization state and race the transparent clear.
    emit_unplanned_compute_sync(cmd, {&dst_image});
    ++stats.passes_executed;
}

void VulkanBackend::Impl::fill_solid_shape(
    runtime::RenderSurfaceHandle destination,
    std::int32_t x0, std::int32_t y0,
    std::int32_t x1, std::int32_t y1,
    const Vec4& shape, const Vec4& line,
    const Color& color,
    const std::array<Vec2, 8>& vertices) {
    auto& dst_image = resolve_image(destination);
    if (dst_image.width == 0 || dst_image.height == 0) {
        throw std::invalid_argument("Vulkan shape fill references an empty surface");
    }
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_fill_rect_descriptors(descriptors, dst_image);
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst_image});
        record_fill_rect(cmd, descriptors, dst_image, x0, y0, x1, y1,
                         color, shape, line, vertices);
        dst_image.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error(
        "Vulkan fill_solid_shape requires an active frame batch");
}

void VulkanBackend::Impl::fill_dashed_path(
    runtime::RenderSurfaceHandle destination,
    std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1,
    float width, const Color& color, std::span<const Vec2> points,
    std::span<const std::uint32_t> run_lengths, bool has_fill,
    std::uint32_t fill_vertex_count, const std::array<Vec2, 8>& fill_vertices) {
    if (!frame_batch.active)
        throw std::logic_error("Vulkan dashed path requires an active frame batch");
    if (points.empty() || run_lengths.empty() || run_lengths.size() > UINT32_MAX)
        throw std::invalid_argument("Vulkan dashed path geometry is empty or too large");
    std::size_t point_sum = 0;
    for (const auto count : run_lengths) {
        if (count < 2 || count > points.size() - point_sum)
            throw std::invalid_argument("Vulkan dashed path run lengths are invalid");
        point_sum += count;
    }
    if (point_sum != points.size())
        throw std::invalid_argument("Vulkan dashed path run lengths do not cover all points");

    // SSBO word layout: run count, one point count per run, then tightly
    // packed xy float bit patterns for every point. This has no 128-byte push
    // constant limit and lets one invocation preserve software first-hit order.
    if (points.size() > (std::numeric_limits<std::size_t>::max() / 2U) - run_lengths.size() - 1U)
        throw std::length_error("Vulkan dashed path geometry size overflow");
    std::vector<std::uint32_t> words(1U + run_lengths.size() + 2U * points.size());
    words[0] = static_cast<std::uint32_t>(run_lengths.size());
    for (std::size_t i = 0; i < run_lengths.size(); ++i) words[1U + i] = run_lengths[i];
    std::size_t cursor = 1U + run_lengths.size();
    for (const auto point : points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y))
            throw std::invalid_argument("Vulkan dashed path points must be finite");
        words[cursor++] = std::bit_cast<std::uint32_t>(point.x);
        words[cursor++] = std::bit_cast<std::uint32_t>(point.y);
    }
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(words.size() * sizeof(std::uint32_t));
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_device, &properties);
    if (bytes > properties.limits.maxStorageBufferRange)
        throw std::length_error("Vulkan dashed path exceeds maxStorageBufferRange");
    if (frame_batch.pass_count >= kPathGeometryPassesPerSlot)
        throw std::logic_error("Vulkan dashed path pass capacity exhausted");
    const std::size_t buffer_slot = 1U + frame_batch.next_slot * kPathGeometryPassesPerSlot +
                                    frame_batch.pass_count;
    ensure_path_geometry_buffer(bytes, buffer_slot);
    auto& geometry = path_geometry_buffers[buffer_slot];
    if (!geometry.mapped)
        throw std::logic_error("Vulkan dashed path geometry buffer is not mapped");
    std::memcpy(geometry.mapped, words.data(), static_cast<std::size_t>(bytes));

    auto& dst_image = resolve_image(destination);
    const auto descriptors = allocate_pass_descriptor_set();
    write_dashed_path_descriptors(descriptors, dst_image, geometry.buffer);
    const auto command = active_command_buffer();
    emit_pass_sync(command, {&dst_image});
    const VkBufferMemoryBarrier2KHR upload_barrier{
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR, nullptr,
        VK_PIPELINE_STAGE_2_HOST_BIT_KHR, VK_ACCESS_2_HOST_WRITE_BIT_KHR,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_READ_BIT_KHR,
        VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
        geometry.buffer, 0, bytes};
    emit_buffer_barriers2(command, std::span<const VkBufferMemoryBarrier2KHR>{&upload_barrier, 1});
    std::array<Vec2, 8> vertices{};
    for (std::size_t i = 0; i < fill_vertex_count; ++i) vertices[i] = fill_vertices[i];
    record_fill_rect(command, descriptors, dst_image, x0, y0, x1, y1,
                     color,
                     Vec4{8.0f, width, static_cast<float>(fill_vertex_count), has_fill ? 1.0f : 0.0f},
                     Vec4{0.0f}, vertices);
    dst_image.initialized = true;
    ++frame_batch.pass_count;
    ++stats.passes_executed;
}

void VulkanBackend::Impl::fill_path(runtime::RenderSurfaceHandle destination,
                                    std::span<const Vec2> vertices,
                                    const Color& color) {
    if (vertices.size() < 3 || vertices.size() > 8) {
        throw std::invalid_argument("Vulkan path fill supports 3..8 vertices");
    }
    auto& dst_image = resolve_image(destination);
    std::array<Vec2, 8> polygon{};
    std::copy(vertices.begin(), vertices.end(), polygon.begin());
    const Vec4 shape{4.0f, 0.0f, 0.0f, static_cast<float>(vertices.size())};
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_fill_rect_descriptors(descriptors, dst_image);
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst_image});
        record_fill_rect(cmd, descriptors, dst_image, 0, 0,
                         static_cast<std::int32_t>(dst_image.width),
                         static_cast<std::int32_t>(dst_image.height),
                         color, shape, Vec4{0.0f}, polygon);
        dst_image.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error("Vulkan fill_path requires an active frame batch");
}

void VulkanBackend::Impl::fill_grid(
    runtime::RenderSurfaceHandle destination,
    std::int32_t x0, std::int32_t y0,
    std::int32_t x1, std::int32_t y1,
    const Color& line_color,
    const Vec4& shape, const Vec4& line,
    const std::array<Vec2, 8>& vertices) {
    auto& dst_image = resolve_image(destination);
    if (dst_image.width == 0 || dst_image.height == 0) {
        throw std::invalid_argument("Vulkan grid fill references an empty surface");
    }
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_fill_rect_descriptors(descriptors, dst_image);
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst_image});
        record_fill_rect(cmd, descriptors, dst_image, x0, y0, x1, y1,
                         line_color, shape, line, vertices);
        dst_image.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error("Vulkan grid fill requires an active frame batch");
}

void VulkanBackend::Impl::transform(runtime::RenderSurfaceHandle destination,
                                    runtime::RenderSurfaceHandle source,
                                    int offset_x, int offset_y, float opacity) {
    auto& dst_image = resolve_image(destination);
    auto& src_image = resolve_image(source);
    if (!src_image.initialized || dst_image.width == 0 || dst_image.height == 0) {
        throw std::invalid_argument(
            "Vulkan transform references incompatible surfaces: dst=" +
            std::to_string(dst_image.width) + "x" +
            std::to_string(dst_image.height) + " src=" +
            std::to_string(src_image.width) + "x" +
            std::to_string(src_image.height) +
            (src_image.initialized ? "" : " src-uninitialized"));
    }
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_descriptors(descriptors, dst_image, src_image);
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst_image, &src_image});
        record_transform(cmd, descriptors, dst_image, src_image,
                         offset_x, offset_y, opacity);
        dst_image.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error("Vulkan transform requires an active frame batch");
}

void VulkanBackend::Impl::transform_affine(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    const runtime::SurfaceAffineTransform& transform_value) {
    auto& dst_image = resolve_image(destination);
    auto& src_image = resolve_image(source);
    if (!src_image.initialized || dst_image.width == 0 || dst_image.height == 0) {
        throw std::invalid_argument(
            "Vulkan affine transform references incompatible surfaces");
    }
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_descriptors(descriptors, dst_image, src_image);
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst_image, &src_image});
        record_transform_affine(cmd, descriptors,
                                dst_image, src_image, transform_value);
        dst_image.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error(
        "Vulkan affine transform requires an active frame batch");
}

void VulkanBackend::Impl::blur(runtime::RenderSurfaceHandle destination,
                               runtime::RenderSurfaceHandle source,
                               float radius, bool horizontal) {
    if (!(radius >= 0.0f) || radius > 32.0f) {
        throw std::invalid_argument("Vulkan blur radius must be within [0, 32]");
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
        emit_pass_sync(cmd, {&dst_image, &src_image});
        record_blur(cmd, descriptors, dst_image, src_image, radius, horizontal);
        dst_image.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error("Vulkan blur requires an active frame batch");
}

void VulkanBackend::Impl::glow(runtime::RenderSurfaceHandle destination,
                               runtime::RenderSurfaceHandle source,
                               runtime::RenderSurfaceHandle scratch_horizontal,
                               runtime::RenderSurfaceHandle scratch_vertical,
                               float radius, float intensity,
                               const Color& tint) {
    if (!(radius >= 0.0f) || radius > 64.0f) {
        throw std::invalid_argument("Vulkan glow radius must be within [0, 64]");
    }
    auto& dst = resolve_image(destination);
    auto& src_image = resolve_image(source);
    auto& horizontal = resolve_image(scratch_horizontal);
    auto& vertical = resolve_image(scratch_vertical);
    if (!src_image.initialized || !dst.initialized ||
        src_image.width != dst.width || src_image.height != dst.height ||
        horizontal.width != src_image.width || horizontal.height != src_image.height ||
        vertical.width != src_image.width || vertical.height != src_image.height) {
        throw std::invalid_argument("Vulkan glow surfaces have incompatible dimensions");
    }
    const float tint_rgba[4] = {tint.r, tint.g, tint.b, tint.a};

    if (frame_batch.active) {
        const auto horizontal_descriptor = allocate_pass_descriptor_set();
        write_descriptors(horizontal_descriptor, horizontal, src_image);
        const auto vertical_descriptor = allocate_pass_descriptor_set();
        write_descriptors(vertical_descriptor, vertical, horizontal);
        const auto composite_descriptor = allocate_pass_descriptor_set();
        write_descriptors(composite_descriptor, dst, vertical);
        const auto cmd = active_command_buffer();

        // One command-plan pass => consume its canonical transition stream
        // exactly once. Internal dispatch dependencies use the same
        // ResourceTransition -> Sync2 translator.
        emit_pass_sync(cmd, {&dst, &src_image, &horizontal, &vertical});
        record_blur(cmd, horizontal_descriptor, horizontal, src_image,
                    radius, true);
        horizontal.initialized = true;

        runtime::ResourceTransition horizontal_ready;
        horizontal_ready.range = runtime::image_range(runtime::ResourceAspect::Color);
        horizontal_ready.before = runtime::ResourceState{
            .stages = runtime::PipelineStage::ComputeShader,
            .access = runtime::AccessMask::ShaderWrite,
            .layout = runtime::ResourceLayout::General,
            .queue = runtime::QueueClass::Compute,
        };
        horizontal_ready.after = runtime::ResourceState{
            .stages = runtime::PipelineStage::ComputeShader,
            .access = runtime::AccessMask::ShaderRead,
            .layout = runtime::ResourceLayout::General,
            .queue = runtime::QueueClass::Compute,
        };
        emit_resource_transition(cmd, horizontal.image, horizontal_ready);

        record_blur(cmd, vertical_descriptor, vertical, horizontal,
                    radius, false);
        vertical.initialized = true;

        runtime::ResourceTransition vertical_ready = horizontal_ready;
        emit_resource_transition(cmd, vertical.image, vertical_ready);

        record_composite(cmd, composite_descriptor, dst, vertical,
                         1, intensity, tint_rgba);
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
    if (radius > 0.0f) {
        horizontal_blur_descriptor = allocate_pass_descriptor_set();
        write_descriptors(horizontal_blur_descriptor, vertical, horizontal);
        vertical_blur_descriptor = allocate_pass_descriptor_set();
        write_descriptors(vertical_blur_descriptor, horizontal, vertical);
    }
    const auto composite_descriptor = allocate_pass_descriptor_set();
    write_descriptors(composite_descriptor, dst, horizontal);
    const auto command = active_command_buffer();
    emit_pass_sync(command, {&dst, &src_image, &horizontal, &vertical});
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
    if (radius > 0.0f) {
        emit_resource_transition(command, horizontal.image, ready_for_read);
        record_blur(command, horizontal_blur_descriptor, vertical, horizontal, radius, true);
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
        record_blur(command, vertical_blur_descriptor, horizontal, vertical, radius, false);
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
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst, &src_image});
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
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst, &src_image});
        record_color_transform(cmd, descriptors, dst, src_image, transform);
        dst.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error("Vulkan color_transform requires an active frame batch");
}

void VulkanBackend::Impl::lighting(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    const Color& multiplier) {
    auto& dst = resolve_image(destination);
    auto& src_image = resolve_image(source);
    if (!src_image.initialized ||
        dst.width != src_image.width || dst.height != src_image.height) {
        throw std::invalid_argument("Vulkan lighting references incompatible surfaces");
    }
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_descriptors(descriptors, dst, src_image);
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst, &src_image});
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
        emit_pass_sync(cmd, {&dst, &target_image, &matte_image});
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
