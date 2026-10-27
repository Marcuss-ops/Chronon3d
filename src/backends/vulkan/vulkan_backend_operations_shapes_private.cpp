// Vulkan fill and native shape operation wrappers.
#include "vulkan_backend_impl.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace chronon3d::backends::vulkan {

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
    std::uint32_t fill_vertex_count, const std::array<Vec2, 8>& fill_vertices,
    bool round_cap) {
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
                     Vec4{round_cap ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f}, vertices);
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

} // namespace chronon3d::backends::vulkan
