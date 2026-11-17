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
#include <span>
#include <vector>

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
        emit_pass_sync(cmd, {&dst_image}, runtime::GpuPassKind::Composite);
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
        emit_pass_sync(cmd, {&dst_image}, runtime::GpuPassKind::Composite);
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
    std::span<const std::uint32_t> run_lengths,
    std::span<const Vec2> fill_polygon,
    std::span<const std::uint32_t> fill_contours, bool round_cap) {
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

    const bool has_fill = fill_polygon.size() >= 3;
    // Which fill vertices close a ring is decided on the host, exactly as in
    // fill_polygon: the shader must not join the last vertex of one contour to
    // the first of the next.
    std::vector<std::uint32_t> fill_sizes;
    if (has_fill) {
        if (fill_contours.empty()) {
            fill_sizes.push_back(static_cast<std::uint32_t>(fill_polygon.size()));
        } else {
            std::size_t total = 0;
            for (const auto count : fill_contours) {
                if (count == 0U) continue;
                total += count;
                fill_sizes.push_back(count);
            }
            if (total != fill_polygon.size())
                throw std::invalid_argument(
                    "Vulkan dashed path fill contours must cover every vertex exactly once");
        }
        if (fill_sizes.empty())
            throw std::invalid_argument("Vulkan dashed path fill requires at least one contour");
    }
    // SSBO word layout: run count, one point count per run, then tightly
    // packed xy float bit patterns for every point, then — when the path is
    // filled — {contour count, [vertex count, xy…] × n}. This has no 128-byte
    // push constant limit and lets one invocation preserve software first-hit
    // order.
    const std::size_t fill_words = has_fill
        ? 1U + fill_sizes.size() + 2U * fill_polygon.size() : 0U;
    if (points.size() > (std::numeric_limits<std::size_t>::max() / 2U) - run_lengths.size() - 1U ||
        fill_words > (std::numeric_limits<std::size_t>::max() / 2U) - run_lengths.size() - 1U ||
        fill_words > std::numeric_limits<std::size_t>::max() -
            (1U + run_lengths.size() + 2U * points.size()))
        throw std::length_error("Vulkan dashed path geometry size overflow");
    std::vector<std::uint32_t> words(1U + run_lengths.size() + 2U * points.size() + fill_words);
    words[0] = static_cast<std::uint32_t>(run_lengths.size());
    for (std::size_t i = 0; i < run_lengths.size(); ++i) words[1U + i] = run_lengths[i];
    std::size_t cursor = 1U + run_lengths.size();
    for (const auto point : points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y))
            throw std::invalid_argument("Vulkan dashed path points must be finite");
        words[cursor++] = std::bit_cast<std::uint32_t>(point.x);
        words[cursor++] = std::bit_cast<std::uint32_t>(point.y);
    }
    if (has_fill) {
        words[cursor++] = static_cast<std::uint32_t>(fill_sizes.size());
        std::size_t vertex_index = 0U;
        for (const auto count : fill_sizes) {
            words[cursor++] = count;
            for (std::uint32_t index = 0; index < count; ++index) {
                const auto vertex = fill_polygon[vertex_index++];
                if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y))
                    throw std::invalid_argument("Vulkan dashed path fill vertices must be finite");
                words[cursor++] = std::bit_cast<std::uint32_t>(vertex.x);
                words[cursor++] = std::bit_cast<std::uint32_t>(vertex.y);
            }
        }
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
    emit_pass_sync(command, {&dst_image}, runtime::GpuPassKind::Composite);
    const VkBufferMemoryBarrier2KHR upload_barrier{
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR, nullptr,
        VK_PIPELINE_STAGE_2_HOST_BIT_KHR, VK_ACCESS_2_HOST_WRITE_BIT_KHR,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_READ_BIT_KHR,
        VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
        geometry.buffer, 0, bytes};
    emit_buffer_barriers2(command, std::span<const VkBufferMemoryBarrier2KHR>{&upload_barrier, 1});
    record_fill_rect(command, descriptors, dst_image, x0, y0, x1, y1,
                     color,
                     Vec4{8.0f, width, 0.0f, has_fill ? 1.0f : 0.0f},
                     Vec4{round_cap ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f});
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
        emit_pass_sync(cmd, {&dst_image}, runtime::GpuPassKind::Composite);
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

void VulkanBackend::Impl::fill_polygon(
    runtime::RenderSurfaceHandle destination,
    std::int32_t x0, std::int32_t y0,
    std::int32_t x1, std::int32_t y1,
    std::span<const Vec2> vertices,
    std::span<const std::uint32_t> contours,
    const Color& color, float ramp) {
    if (vertices.size() < 3)
        throw std::invalid_argument("Vulkan polygon fill requires at least 3 vertices");
    if (vertices.size() > UINT32_MAX)
        throw std::invalid_argument("Vulkan polygon fill vertex count overflow");
    auto& dst_image = resolve_image(destination);
    if (dst_image.width == 0 || dst_image.height == 0)
        throw std::invalid_argument("Vulkan polygon fill references an empty surface");
    if (!frame_batch.active)
        throw std::logic_error("Vulkan polygon fill requires an active frame batch");
    // Which vertices close a ring is decided on the host: the shader must never
    // join the last vertex of one contour to the first of the next.
    std::vector<std::uint32_t> sizes;
    if (contours.empty()) {
        sizes.push_back(static_cast<std::uint32_t>(vertices.size()));
    } else {
        std::size_t total = 0;
        for (const auto count : contours) {
            if (count == 0U) continue;
            total += count;
            sizes.push_back(count);
        }
        if (total != vertices.size()) {
            throw std::invalid_argument(
                "Vulkan polygon fill contours must cover every vertex exactly once");
        }
    }
    if (sizes.empty())
        throw std::invalid_argument("Vulkan polygon fill requires at least one contour");
    // SSBO word layout: contour count, then per contour its vertex count and
    // the tightly packed xy float bit patterns. The push-constant block keeps
    // only the rect, the colour and the primitive kind.
    const std::size_t word_count = 1U + sizes.size() + 2U * vertices.size();
    std::vector<std::uint32_t> words(word_count);
    words[0] = static_cast<std::uint32_t>(sizes.size());
    std::size_t cursor = 1U;
    std::size_t vertex_index = 0U;
    for (const auto count : sizes) {
        words[cursor++] = count;
        for (std::uint32_t index = 0; index < count; ++index) {
            const auto vertex = vertices[vertex_index++];
            if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y))
                throw std::invalid_argument("Vulkan polygon fill vertices must be finite");
            words[cursor++] = std::bit_cast<std::uint32_t>(vertex.x);
            words[cursor++] = std::bit_cast<std::uint32_t>(vertex.y);
        }
    }
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(words.size() * sizeof(std::uint32_t));
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_device, &properties);
    if (bytes > properties.limits.maxStorageBufferRange)
        throw std::length_error("Vulkan polygon fill exceeds maxStorageBufferRange");
    if (frame_batch.pass_count >= kPathGeometryPassesPerSlot)
        throw std::logic_error("Vulkan polygon fill pass capacity exhausted");
    const std::size_t buffer_slot = 1U + frame_batch.next_slot * kPathGeometryPassesPerSlot +
                                    frame_batch.pass_count;
    ensure_path_geometry_buffer(bytes, buffer_slot);
    auto& geometry = path_geometry_buffers[buffer_slot];
    if (!geometry.mapped)
        throw std::logic_error("Vulkan polygon fill geometry buffer is not mapped");
    std::memcpy(geometry.mapped, words.data(), static_cast<std::size_t>(bytes));

    const auto descriptors = allocate_pass_descriptor_set();
    write_fill_rect_descriptors(descriptors, dst_image, geometry.buffer);
    const auto cmd = active_command_buffer();
    emit_pass_sync(cmd, {&dst_image}, runtime::GpuPassKind::Composite);
    const VkBufferMemoryBarrier2KHR upload_barrier{
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR, nullptr,
        VK_PIPELINE_STAGE_2_HOST_BIT_KHR, VK_ACCESS_2_HOST_WRITE_BIT_KHR,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_READ_BIT_KHR,
        VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
        geometry.buffer, 0, bytes};
    emit_buffer_barriers2(cmd, std::span<const VkBufferMemoryBarrier2KHR>{&upload_barrier, 1});
    record_fill_rect(cmd, descriptors, dst_image, x0, y0, x1, y1, color,
                     Vec4{10.0f, std::isfinite(ramp) ? std::max(ramp, 0.0f) : 1.0f,
                          static_cast<float>(vertices.size()), 0.0f},
                     Vec4{0.0f});
    dst_image.initialized = true;
    ++frame_batch.pass_count;
    ++stats.passes_executed;
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
        emit_pass_sync(cmd, {&dst_image}, runtime::GpuPassKind::Composite);
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
