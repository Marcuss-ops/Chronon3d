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
                                    bool replace, float source_scale,
                                    bool weighted_source_over) {
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
    const std::int32_t blend_mode = replace ? 2 : weighted_source_over ? 7 :
        (mode == BlendMode::Add ? 1 : mode == BlendMode::Multiply ? 3 :
         mode == BlendMode::Screen ? 4 : mode == BlendMode::Overlay ? 5 : 0);
    const bool premultiplied_source_over =
        !replace && !weighted_source_over && mode == BlendMode::Normal;
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_descriptors(descriptors, dst_image, src_image);
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst_image, &src_image});
        record_composite(cmd, descriptors, dst_image, src_image,
                         blend_mode, source_scale, kIdentityTint, clip,
                         premultiplied_source_over);
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
        record_fill_rect(cmd, descriptors, dst_image, x0, y0, x1, y1,
                         color, Vec4{-1.0f, 0.0f, 0.0f, 0.0f});
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
                     Color::transparent(), Vec4{-1.0f, 0.0f, 0.0f, 0.0f});
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
    const std::array<Vec2, 8>& vertices,
    std::span<const std::uint32_t> mask_words) {
    auto& dst_image = resolve_image(destination);
    if (dst_image.width == 0 || dst_image.height == 0) {
        throw std::invalid_argument("Vulkan shape fill references an empty surface");
    }
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        VkBuffer geometry_buffer = VK_NULL_HANDLE;
        VkDeviceSize geometry_bytes = 0;
        if (!mask_words.empty()) {
            if (mask_words.size() > std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t))
                throw std::length_error("Vulkan shape mask size overflow");
            geometry_bytes = static_cast<VkDeviceSize>(mask_words.size() * sizeof(std::uint32_t));
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(physical_device, &properties);
            if (geometry_bytes > properties.limits.maxStorageBufferRange)
                throw std::length_error("Vulkan shape mask exceeds maxStorageBufferRange");
            if (frame_batch.pass_count >= kPathGeometryPassesPerSlot)
                throw std::logic_error("Vulkan shape mask pass capacity exhausted");
            const std::size_t buffer_slot = 1U + frame_batch.next_slot * kPathGeometryPassesPerSlot +
                                            frame_batch.pass_count;
            ensure_path_geometry_buffer(geometry_bytes, buffer_slot);
            auto& geometry = path_geometry_buffers[buffer_slot];
            if (!geometry.mapped)
                throw std::logic_error("Vulkan shape mask buffer is not mapped");
            std::memcpy(geometry.mapped, mask_words.data(), static_cast<std::size_t>(geometry_bytes));
            geometry_buffer = geometry.buffer;
        }
        write_fill_rect_descriptors(descriptors, dst_image, geometry_buffer);
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst_image});
        if (geometry_buffer != VK_NULL_HANDLE) {
            const std::size_t buffer_slot = 1U + frame_batch.next_slot * kPathGeometryPassesPerSlot +
                                            frame_batch.pass_count;
            const auto& geometry = path_geometry_buffers[buffer_slot];
            const VkBufferMemoryBarrier2KHR upload_barrier{
                VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR, nullptr,
                VK_PIPELINE_STAGE_2_HOST_BIT_KHR, VK_ACCESS_2_HOST_WRITE_BIT_KHR,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_READ_BIT_KHR,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                geometry.buffer, 0, geometry_bytes};
            emit_buffer_barriers2(cmd, std::span<const VkBufferMemoryBarrier2KHR>{&upload_barrier, 1});
        }
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
        emit_pass_sync(cmd, {&dst_image, &src_image});
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
        emit_pass_sync(cmd, {&dst_image, &src_image});
        record_directional_blur(cmd, descriptors, dst_image, src_image,
                                dir_x, dir_y, length, tap_count);
        dst_image.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error("Vulkan directional blur requires an active frame batch");
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
        emit_pass_sync(cmd, {&dst_image, &src_image, &blurred_image});
        record_sharpen_combine(cmd, descriptors, dst_image, src_image,
                               blurred_image, amount, threshold);
        dst_image.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error("Vulkan sharpen combine requires an active frame batch");
}

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
        emit_pass_sync(cmd, {&dst, &src_image, &horizontal, &vertical});
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
    emit_pass_sync(command, {&dst, &src_image});
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
