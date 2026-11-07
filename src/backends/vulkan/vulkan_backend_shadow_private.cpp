// vulkan_backend_shadow_private.cpp — production shadow-map compute pass.
//
// The backend has no raster pipeline: casters are rasterized by a compute
// kernel into a light-space depth image, and a second kernel samples it with
// PCF. Both passes use the shared general descriptor layout (storage images
// 0..2, storage buffers 3..4) and the 128-byte push-constant budget.

#include "vulkan_backend_impl.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace chronon3d::backends::vulkan {

void VulkanBackend::Impl::record_shadow_caster(
    VkCommandBuffer command, VkDescriptorSet descriptors,
    const Image& /*depth_map*/, std::int32_t vertex_count,
    std::int32_t index_count, std::int32_t x0, std::int32_t y0,
    std::int32_t x1, std::int32_t y1) {
    struct PushConstants {
        std::int32_t vertex_count;
        std::int32_t index_count;
        std::int32_t x0, y0, x1, y1;
    } push{vertex_count, index_count, x0, y0, x1, y1};
    static_assert(sizeof(PushConstants) == 24);
    const auto pipeline = reinterpret_cast<VkPipeline>(
        kernels.registry.resolve(GpuKernelId::ShadowCaster));
    if (pipeline == VK_NULL_HANDLE)
        throw std::logic_error("Vulkan shadow caster pipeline was not registered");
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                            kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
    vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(push), &push);
    vkCmdDispatch(command,
        (static_cast<std::uint32_t>(x1 - x0) + 15U) / 16U,
        (static_cast<std::uint32_t>(y1 - y0) + 15U) / 16U, 1);
    ++stats.vk_cmd_dispatch_count;
}

void VulkanBackend::Impl::shadow_caster(
    runtime::RenderSurfaceHandle depth_map,
    std::span<const float> light_space_vertices,
    std::span<const std::uint32_t> indices,
    std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1) {
    if (!frame_batch.active)
        throw std::logic_error("Vulkan shadow caster requires an active frame batch");
    if (light_space_vertices.empty() || light_space_vertices.size() % 4 != 0 ||
        indices.size() < 3 || indices.size() % 3 != 0)
        throw std::invalid_argument("Vulkan shadow caster geometry has invalid topology");

    const std::size_t vertex_count = light_space_vertices.size() / 4;
    constexpr auto shader_count_limit =
        static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max());
    if (vertex_count > shader_count_limit || indices.size() > shader_count_limit)
        throw std::length_error("Vulkan shadow caster exceeds signed shader limits");

    auto& map = resolve_image(depth_map);
    if (!map.width || !map.height || map.view == VK_NULL_HANDLE)
        throw std::invalid_argument("Vulkan shadow caster has an invalid depth map");
    x0 = std::clamp(x0, 0, static_cast<std::int32_t>(map.width));
    y0 = std::clamp(y0, 0, static_cast<std::int32_t>(map.height));
    x1 = std::clamp(x1, 0, static_cast<std::int32_t>(map.width));
    y1 = std::clamp(y1, 0, static_cast<std::int32_t>(map.height));
    if (x0 >= x1 || y0 >= y1) return;

    if (vertex_count > std::numeric_limits<std::size_t>::max() / 4)
        throw std::length_error("Vulkan shadow caster buffer size overflow");
    const std::size_t vertex_words = vertex_count * 4;
    if (indices.size() > std::numeric_limits<std::size_t>::max() - vertex_words)
        throw std::length_error("Vulkan shadow caster buffer size overflow");
    const std::size_t word_count = vertex_words + indices.size();
    if (word_count > std::numeric_limits<std::size_t>::max() / sizeof(float))
        throw std::length_error("Vulkan shadow caster byte size overflow");
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(word_count * sizeof(float));
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_device, &properties);
    if (bytes > properties.limits.maxStorageBufferRange)
        throw std::length_error("Vulkan shadow caster exceeds maxStorageBufferRange");
    if (frame_batch.pass_count >= kPathGeometryPassesPerSlot)
        throw std::logic_error("Vulkan shadow caster pass capacity exhausted");

    const std::size_t buffer_slot = 1U + frame_batch.next_slot *
        kPathGeometryPassesPerSlot + frame_batch.pass_count;
    ensure_path_geometry_buffer(bytes, buffer_slot);
    auto& geometry = path_geometry_buffers[buffer_slot];
    if (!geometry.mapped)
        throw std::logic_error("Vulkan shadow caster buffer is not host mapped");
    std::memcpy(geometry.mapped, light_space_vertices.data(),
                light_space_vertices.size_bytes());
    auto* packed_indices = static_cast<std::byte*>(geometry.mapped) +
        light_space_vertices.size_bytes();
    std::memcpy(packed_indices, indices.data(), indices.size_bytes());

    const auto descriptors = allocate_pass_descriptor_set();
    const VkDescriptorImageInfo map_info{
        VK_NULL_HANDLE, map.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorBufferInfo geometry_info{geometry.buffer, 0, bytes};
    const VkWriteDescriptorSet writes[] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptors, 0, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &map_info, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptors, 3, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &geometry_info, nullptr}};
    vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);

    const auto command = active_command_buffer();
    emit_pass_sync(command, {&map}, runtime::GpuPassKind::ColorAdjust);
    const VkBufferMemoryBarrier2KHR upload_barrier{
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR, nullptr,
        VK_PIPELINE_STAGE_2_HOST_BIT_KHR, VK_ACCESS_2_HOST_WRITE_BIT_KHR,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_READ_BIT_KHR,
        VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
        geometry.buffer, 0, bytes};
    emit_buffer_barriers2(command,
        std::span<const VkBufferMemoryBarrier2KHR>{&upload_barrier, 1});

    record_shadow_caster(command, descriptors, map,
                         static_cast<std::int32_t>(vertex_count),
                         static_cast<std::int32_t>(indices.size()), x0, y0, x1, y1);
    map.initialized = true;
    ++frame_batch.pass_count;
    ++stats.passes_executed;
}

void VulkanBackend::Impl::record_shadow_apply(
    VkCommandBuffer command, VkDescriptorSet descriptors,
    const Image& destination, const Image& /*source*/, const Image& /*depth_map*/,
    const rendering::ShadowApplyParams& params) {
    const auto pipeline = reinterpret_cast<VkPipeline>(
        kernels.registry.resolve(GpuKernelId::ShadowApply));
    if (pipeline == VK_NULL_HANDLE)
        throw std::logic_error("Vulkan shadow apply pipeline was not registered");
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                            kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
    // ShadowApplyParams is exactly the 128-byte push-constant block the shader
    // declares, including 16-byte alignment for its five vec4 lanes.
    vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(params), &params);
    vkCmdDispatch(command, (destination.width + 15U) / 16U,
                  (destination.height + 15U) / 16U, 1);
    ++stats.vk_cmd_dispatch_count;
}

void VulkanBackend::Impl::shadow_apply(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    runtime::RenderSurfaceHandle depth_map,
    const rendering::ShadowApplyParams& params) {
    auto& dst = resolve_image(destination);
    auto& src_image = resolve_image(source);
    auto& map = resolve_image(depth_map);
    if (dst.image == src_image.image)
        throw std::invalid_argument(
            "Vulkan shadow apply destination aliases its source image");
    if (!src_image.initialized || !map.initialized ||
        dst.width != src_image.width || dst.height != src_image.height)
        throw std::invalid_argument("Vulkan shadow apply references incompatible surfaces");
    if (!frame_batch.active)
        throw std::logic_error("Vulkan shadow apply requires an active frame batch");
    if (frame_batch.pass_count >= kPathGeometryPassesPerSlot)
        throw std::logic_error("Vulkan shadow apply pass capacity exhausted");

    const auto descriptors = allocate_pass_descriptor_set();
    write_descriptors(descriptors, dst, src_image);
    const VkDescriptorImageInfo map_info{
        VK_NULL_HANDLE, map.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkWriteDescriptorSet map_write{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptors, 2, 0, 1,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &map_info, nullptr, nullptr};
    vkUpdateDescriptorSets(device, 1, &map_write, 0, nullptr);

    const auto command = active_command_buffer();
    emit_pass_sync(command, {&dst, &src_image, &map},
                   runtime::GpuPassKind::ColorAdjust);
    record_shadow_apply(command, descriptors, dst, src_image, map, params);
    dst.initialized = true;
    ++frame_batch.pass_count;
    ++stats.passes_executed;
}

} // namespace chronon3d::backends::vulkan
