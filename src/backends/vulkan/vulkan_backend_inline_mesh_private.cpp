#include "vulkan_backend_impl.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <limits>

namespace chronon3d::backends::vulkan {

namespace {

// Bits shared with inline_mesh.comp. The per-pixel path is gated because the
// minimum guaranteed push-constant budget (128 bytes) leaves no room for the
// HDRI environment probe or an explicit rim-light override; those cases fail
// closed in the graph node instead of being silently dropped.
constexpr std::int32_t kShadingPerPixel = 1;
constexpr std::int32_t kShadingDoubleSided = 2;
constexpr std::int32_t kShadingDirectional = 4;
constexpr std::int32_t kShadingAmbient = 8;
constexpr std::int32_t kShadingEmissive = 16;
constexpr std::int32_t kShadingHasMrTexture = 32;

constexpr std::size_t kInlineMeshVertexStride = 11;

} // namespace

void VulkanBackend::Impl::draw_inline_mesh(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle texture,
    std::span<const float> projected_lit_vertices,
    std::span<const std::uint32_t> indices,
    std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1,
    const Color& tint,
    std::optional<float> alpha_cutoff,
    runtime::RenderSurfaceHandle depth_destination,
    float depth_marker,
    const graph::InlineMeshShadingParams& shading) {
    if (!frame_batch.active)
        throw std::logic_error("Vulkan inline mesh requires an active frame batch");
    if (projected_lit_vertices.empty() ||
        projected_lit_vertices.size() % kInlineMeshVertexStride != 0 ||
        indices.size() < 3 || indices.size() % 3 != 0)
        throw std::invalid_argument("Vulkan inline mesh geometry has invalid topology");
    const std::size_t vertex_count =
        projected_lit_vertices.size() / kInlineMeshVertexStride;
    // The push constants are signed GLSL ints; reject before narrowing rather
    // than allowing large, valid uint32 geometry to wrap negative in the shader.
    constexpr auto shader_count_limit = static_cast<std::size_t>(
        std::numeric_limits<std::int32_t>::max());
    if (vertex_count > shader_count_limit || indices.size() > shader_count_limit)
        throw std::length_error("Vulkan inline mesh exceeds signed shader count limits");
    auto& dst_image = resolve_image(destination);
    auto& texture_image = resolve_image(texture);
    const bool write_depth =
        depth_destination != runtime::kInvalidRenderSurfaceHandle;
    Image* depth_image = nullptr;
    if (write_depth) {
        depth_image = &resolve_image(depth_destination);
        if (!depth_image->initialized) {
            throw std::invalid_argument(
                "Vulkan inline mesh depth companion is not initialized");
        }
        if (depth_image->width != dst_image.width ||
            depth_image->height != dst_image.height) {
            throw std::invalid_argument(
                "Vulkan inline mesh depth companion extent mismatch");
        }
    }
    Image* mr_image = &texture_image;
    if (shading.per_pixel_pbr &&
        shading.metallic_roughness_texture != runtime::kInvalidRenderSurfaceHandle) {
        mr_image = &resolve_image(shading.metallic_roughness_texture);
        if (!mr_image->initialized || !mr_image->width || !mr_image->height)
            throw std::invalid_argument(
                "Vulkan inline mesh has an invalid metallic-roughness texture");
    }
    if (!dst_image.width || !dst_image.height || !texture_image.initialized ||
        !texture_image.width || !texture_image.height)
        throw std::invalid_argument("Vulkan inline mesh has an invalid destination or texture");
    x0 = std::clamp(x0, 0, static_cast<std::int32_t>(dst_image.width));
    y0 = std::clamp(y0, 0, static_cast<std::int32_t>(dst_image.height));
    x1 = std::clamp(x1, 0, static_cast<std::int32_t>(dst_image.width));
    y1 = std::clamp(y1, 0, static_cast<std::int32_t>(dst_image.height));
    if (x0 >= x1 || y0 >= y1) return;

    if (vertex_count > std::numeric_limits<std::size_t>::max() / kInlineMeshVertexStride)
        throw std::length_error("Vulkan inline mesh buffer size overflow");
    const std::size_t vertex_words = vertex_count * kInlineMeshVertexStride;
    if (indices.size() > std::numeric_limits<std::size_t>::max() - vertex_words)
        throw std::length_error("Vulkan inline mesh buffer size overflow");
    const std::size_t word_count = vertex_words + indices.size();
    if (word_count > std::numeric_limits<std::size_t>::max() / sizeof(float))
        throw std::length_error("Vulkan inline mesh byte size overflow");
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(word_count * sizeof(float));
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_device, &properties);
    if (bytes > properties.limits.maxStorageBufferRange)
        throw std::length_error("Vulkan inline mesh exceeds maxStorageBufferRange");
    if (frame_batch.pass_count >= kPathGeometryPassesPerSlot)
        throw std::logic_error("Vulkan inline mesh geometry pass capacity exhausted");

    const std::size_t buffer_slot = 1U + frame_batch.next_slot *
        kPathGeometryPassesPerSlot + frame_batch.pass_count;
    ensure_path_geometry_buffer(bytes, buffer_slot);
    auto& geometry = path_geometry_buffers[buffer_slot];
    if (!geometry.mapped)
        throw std::logic_error("Vulkan inline mesh buffer is not host mapped");
    std::memcpy(geometry.mapped, projected_lit_vertices.data(),
                projected_lit_vertices.size_bytes());
    auto* packed_indices = static_cast<std::byte*>(geometry.mapped) +
        projected_lit_vertices.size_bytes();
    std::memcpy(packed_indices, indices.data(), indices.size_bytes());

    const auto descriptors = allocate_pass_descriptor_set();
    const VkDescriptorImageInfo dst_info{
        VK_NULL_HANDLE, dst_image.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo texture_info{
        VK_NULL_HANDLE, texture_image.view, VK_IMAGE_LAYOUT_GENERAL};
    // The shader always declares binding 2; when this draw does not publish
    // depth it is bound to the destination (unused, but a valid descriptor).
    const VkDescriptorImageInfo depth_info{
        VK_NULL_HANDLE, write_depth ? depth_image->view : dst_image.view,
        VK_IMAGE_LAYOUT_GENERAL};
    // Binding 4 is likewise always declared: without a per-pixel material the
    // base-color image is bound and the shader's flag gate never samples it.
    const VkDescriptorImageInfo mr_info{
        VK_NULL_HANDLE, mr_image->view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorBufferInfo mesh_info{geometry.buffer, 0, bytes};
    const VkWriteDescriptorSet writes[] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptors, 0, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dst_info, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptors, 1, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &texture_info, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptors, 2, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &depth_info, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptors, 3, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &mesh_info, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptors, 4, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &mr_info, nullptr, nullptr}};
    vkUpdateDescriptorSets(device, 5, writes, 0, nullptr);

    const auto command = active_command_buffer();
    // Barrier every image the pass touches exactly once. The depth companion
    // and the metallic-roughness map may legitimately be the same surface.
    if (write_depth && depth_image != mr_image)
        emit_pass_sync(command, {&dst_image, &texture_image, mr_image, depth_image},
                       runtime::GpuPassKind::Composite);
    else
        emit_pass_sync(command, {&dst_image, &texture_image, mr_image},
                       runtime::GpuPassKind::Composite);
    const VkBufferMemoryBarrier2KHR upload_barrier{
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR, nullptr,
        VK_PIPELINE_STAGE_2_HOST_BIT_KHR, VK_ACCESS_2_HOST_WRITE_BIT_KHR,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_READ_BIT_KHR,
        VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
        geometry.buffer, 0, bytes};
    emit_buffer_barriers2(command,
        std::span<const VkBufferMemoryBarrier2KHR>{&upload_barrier, 1});

    std::int32_t shading_flags = 0;
    if (shading.per_pixel_pbr) shading_flags |= kShadingPerPixel;
    if (shading.double_sided) shading_flags |= kShadingDoubleSided;
    if (shading.directional_enabled) shading_flags |= kShadingDirectional;
    if (shading.ambient_enabled) shading_flags |= kShadingAmbient;
    if (shading.emissive_enabled) shading_flags |= kShadingEmissive;
    if (shading.metallic_roughness_texture != runtime::kInvalidRenderSurfaceHandle)
        shading_flags |= kShadingHasMrTexture;

    struct PushConstants {
        std::int32_t vertex_count;
        std::int32_t index_count;
        std::int32_t x0, y0, x1, y1;
        float tint_r, tint_g, tint_b, tint_a;
        float alpha_cutoff;
        std::int32_t has_alpha_cutoff;
        float depth_marker;
        std::int32_t write_depth;
        float material_metallic;
        float material_roughness;
        float material_diffuse;
        std::int32_t shading_flags;
        float light_dir_x, light_dir_y, light_dir_z;
        float light_color_r, light_color_g, light_color_b;
        float ambient_color_r, ambient_color_g, ambient_color_b;
        float light_diffuse;
        float ambient;
        float emissive_r, emissive_g, emissive_b;
    } push{
        static_cast<std::int32_t>(vertex_count),
        static_cast<std::int32_t>(indices.size()), x0, y0, x1, y1,
        tint.r, tint.g, tint.b, tint.a,
        alpha_cutoff.value_or(0.0f), alpha_cutoff ? 1 : 0,
        depth_marker, write_depth ? 1 : 0,
        shading.metallic, shading.roughness, shading.diffuse, shading_flags,
        shading.light_direction.x, shading.light_direction.y, shading.light_direction.z,
        shading.light_color.r, shading.light_color.g, shading.light_color.b,
        shading.ambient_color.r, shading.ambient_color.g, shading.ambient_color.b,
        shading.light_diffuse, shading.ambient,
        shading.emissive.r, shading.emissive.g, shading.emissive.b};
    static_assert(sizeof(PushConstants) == 128);
    const auto pipeline = reinterpret_cast<VkPipeline>(
        kernels.registry.resolve(GpuKernelId::InlineMesh));
    if (pipeline == VK_NULL_HANDLE)
        throw std::logic_error("Vulkan inline mesh pipeline was not registered");
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                            kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
    vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(push), &push);
    vkCmdDispatch(command,
        (static_cast<std::uint32_t>(x1 - x0) + 15U) / 16U,
        (static_cast<std::uint32_t>(y1 - y0) + 15U) / 16U, 1);
    dst_image.initialized = true;
    if (write_depth) depth_image->initialized = true;
    ++frame_batch.pass_count;
    ++stats.passes_executed;
    ++stats.vk_cmd_dispatch_count;
}

} // namespace chronon3d::backends::vulkan
