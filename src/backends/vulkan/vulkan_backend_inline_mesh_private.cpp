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
constexpr std::size_t kInlineMeshBinningThreshold = 64;
constexpr std::uint32_t kInlineMeshMaxTrisPerTile = 256;

[[nodiscard]] std::uint64_t hash_indices_fnv1a(
    std::span<const std::uint32_t> indices) noexcept {
    std::uint64_t h = 14695981039346656037ULL;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(indices.data());
    const std::size_t len = indices.size_bytes();
    for (std::size_t i = 0; i < len; ++i) {
        h ^= bytes[i];
        h *= 1099511628211ULL;
    }
    return h;
}

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
    const VkDeviceSize vertex_bytes =
        static_cast<VkDeviceSize>(projected_lit_vertices.size_bytes());
    const VkDeviceSize index_bytes =
        static_cast<VkDeviceSize>(indices.size_bytes());
    if (vertex_bytes > max_storage_buffer_range ||
        index_bytes > max_storage_buffer_range)
        throw std::length_error("Vulkan inline mesh exceeds maxStorageBufferRange");
    if (frame_batch.pass_count >= kPathGeometryPassesPerSlot)
        throw std::logic_error("Vulkan inline mesh geometry pass capacity exhausted");

    const std::size_t buffer_slot = 1U + frame_batch.next_slot *
        kPathGeometryPassesPerSlot + frame_batch.pass_count;
    ensure_path_geometry_buffer(vertex_bytes, buffer_slot);
    auto& geometry = path_geometry_buffers[buffer_slot];
    if (!geometry.mapped)
        throw std::logic_error("Vulkan inline mesh buffer is not host mapped");
    std::memcpy(geometry.mapped, projected_lit_vertices.data(),
                projected_lit_vertices.size_bytes());

    // Fase B: static index buffer separation (zero steady-state index memcpy).
    const std::uint64_t idx_hash = hash_indices_fnv1a(indices);
    InlineMeshStaticIndexEntry* index_entry = nullptr;
    InlineMeshStaticIndexEntry* lru_entry = &inline_mesh_static_indices[0];
    for (auto& entry : inline_mesh_static_indices) {
        if (entry.buffer.buffer != VK_NULL_HANDLE &&
            entry.hash == idx_hash &&
            entry.bytes == indices.size_bytes()) {
            index_entry = &entry;
            break;
        }
        if (entry.last_use < lru_entry->last_use) {
            lru_entry = &entry;
        }
    }
    bool uploaded_indices = false;
    if (index_entry == nullptr) {
        index_entry = lru_entry;
        if (index_entry->buffer.size < index_bytes) {
            if (index_entry->buffer.buffer != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
                memory_manager.destroy_buffer(index_entry->buffer);
            }
            const VkBufferCreateInfo index_ci{
                VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, index_bytes,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE,
                0, nullptr};
            index_entry->buffer = memory_manager.create_buffer(
                index_ci, VulkanMemoryClass::HostUpload);
        }
        if (!index_entry->buffer.mapped)
            throw std::logic_error("Vulkan inline mesh index buffer is not host mapped");
        std::memcpy(index_entry->buffer.mapped, indices.data(),
                    indices.size_bytes());
        index_entry->hash = idx_hash;
        index_entry->bytes = indices.size_bytes();
        uploaded_indices = true;
    }
    index_entry->last_use = ++inline_mesh_static_use_seq;

    if (inline_mesh_dummy_ssbo.buffer == VK_NULL_HANDLE) {
        const VkBufferCreateInfo dummy_ci{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, 16,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE,
            0, nullptr};
        inline_mesh_dummy_ssbo = memory_manager.create_buffer(
            dummy_ci, VulkanMemoryClass::DeviceLocal);
    }

    const std::size_t tri_count = indices.size() / 3U;
    const bool use_binning = tri_count > kInlineMeshBinningThreshold;
    const std::uint32_t tiles_x =
        (static_cast<std::uint32_t>(x1 - x0) + 15U) / 16U;
    const std::uint32_t tiles_y =
        (static_cast<std::uint32_t>(y1 - y0) + 15U) / 16U;
    const std::size_t tile_count =
        static_cast<std::size_t>(tiles_x) * static_cast<std::size_t>(tiles_y);
    const VkDeviceSize tile_counts_bytes =
        static_cast<VkDeviceSize>(std::max<std::size_t>(tile_count, 1U) * sizeof(std::uint32_t));
    const VkDeviceSize tile_tris_bytes =
        static_cast<VkDeviceSize>(std::max<std::size_t>(tile_count, 1U) *
                                  kInlineMeshMaxTrisPerTile * sizeof(std::uint32_t));
    if (use_binning) {
        if (inline_mesh_tile_counts_buffer.size < tile_counts_bytes) {
            if (inline_mesh_tile_counts_buffer.buffer != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
                memory_manager.destroy_buffer(inline_mesh_tile_counts_buffer);
            }
            const VkBufferCreateInfo tc_ci{
                VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, tile_counts_bytes,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
            inline_mesh_tile_counts_buffer = memory_manager.create_buffer(
                tc_ci, VulkanMemoryClass::DeviceLocal);
        }
        if (inline_mesh_tile_tris_buffer.size < tile_tris_bytes) {
            if (inline_mesh_tile_tris_buffer.buffer != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
                memory_manager.destroy_buffer(inline_mesh_tile_tris_buffer);
            }
            const VkBufferCreateInfo tt_ci{
                VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, tile_tris_bytes,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE,
                0, nullptr};
            inline_mesh_tile_tris_buffer = memory_manager.create_buffer(
                tt_ci, VulkanMemoryClass::DeviceLocal);
        }
    }

    const auto descriptors = allocate_inline_mesh_descriptor_set();
    if (!write_depth && dummy_rg32f_image.image == VK_NULL_HANDLE) {
        make_image(dummy_rg32f_image, 1, 1, false, VK_FORMAT_R32G32_SFLOAT);
    }
    const VkDescriptorImageInfo dst_info{
        VK_NULL_HANDLE, dst_image.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo texture_info{
        VK_NULL_HANDLE, texture_image.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo depth_info{
        VK_NULL_HANDLE, write_depth ? depth_image->view : dummy_rg32f_image.view,
        VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo mr_info{
        VK_NULL_HANDLE, mr_image->view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorBufferInfo mesh_info{geometry.buffer, 0, vertex_bytes};
    const VkDescriptorBufferInfo index_info{index_entry->buffer.buffer, 0, index_bytes};
    const VkDescriptorBufferInfo tile_counts_info{
        use_binning ? inline_mesh_tile_counts_buffer.buffer : inline_mesh_dummy_ssbo.buffer,
        0, use_binning ? tile_counts_bytes : 16};
    const VkDescriptorBufferInfo tile_tris_info{
        use_binning ? inline_mesh_tile_tris_buffer.buffer : inline_mesh_dummy_ssbo.buffer,
        0, use_binning ? tile_tris_bytes : 16};
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
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &mr_info, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptors, 5, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &index_info, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptors, 6, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &tile_counts_info, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptors, 7, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &tile_tris_info, nullptr}};
    if (descriptors != VK_NULL_HANDLE) {
        vkUpdateDescriptorSets(device, 8, writes, 0, nullptr);
    }

    const auto command = active_command_buffer();
    if (write_depth && depth_image != mr_image)
        emit_pass_sync(command, {&dst_image, &texture_image, mr_image, depth_image},
                       runtime::GpuPassKind::Composite);
    else if (!write_depth && !dummy_rg32f_image.initialized) {
        emit_pass_sync(command, {&dst_image, &texture_image, mr_image, &dummy_rg32f_image},
                       runtime::GpuPassKind::Composite);
        dummy_rg32f_image.initialized = true;
    } else
        emit_pass_sync(command, {&dst_image, &texture_image, mr_image},
                       runtime::GpuPassKind::Composite);

    std::array<VkBufferMemoryBarrier2KHR, 2> upload_barriers{};
    std::uint32_t upload_barrier_count = 0;
    upload_barriers[upload_barrier_count++] = {
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR, nullptr,
        VK_PIPELINE_STAGE_2_HOST_BIT_KHR, VK_ACCESS_2_HOST_WRITE_BIT_KHR,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_READ_BIT_KHR,
        VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
        geometry.buffer, 0, vertex_bytes};
    if (uploaded_indices) {
        upload_barriers[upload_barrier_count++] = {
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR, nullptr,
            VK_PIPELINE_STAGE_2_HOST_BIT_KHR, VK_ACCESS_2_HOST_WRITE_BIT_KHR,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_READ_BIT_KHR,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
            index_entry->buffer.buffer, 0, index_bytes};
    }
    emit_buffer_barriers2(command,
        std::span<const VkBufferMemoryBarrier2KHR>{upload_barriers.data(), upload_barrier_count});

    if (use_binning) {
        vkCmdFillBuffer(command, inline_mesh_tile_counts_buffer.buffer,
                        0, tile_counts_bytes, 0);
        const VkBufferMemoryBarrier2KHR clear_barrier{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR, nullptr,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR, VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
            VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
            inline_mesh_tile_counts_buffer.buffer, 0, tile_counts_bytes};
        emit_buffer_barriers2(command,
            std::span<const VkBufferMemoryBarrier2KHR>{&clear_barrier, 1});

        const auto bin_descriptors = allocate_inline_mesh_bin_descriptor_set();
        const VkWriteDescriptorSet bin_writes[] = {
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, bin_descriptors, 0, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &mesh_info, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, bin_descriptors, 1, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &index_info, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, bin_descriptors, 2, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &tile_counts_info, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, bin_descriptors, 3, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &tile_tris_info, nullptr}};
        if (bin_descriptors != VK_NULL_HANDLE) {
            vkUpdateDescriptorSets(device, 4, bin_writes, 0, nullptr);
        }
        const auto bin_pipeline = reinterpret_cast<VkPipeline>(
            kernels.registry.resolve(GpuKernelId::InlineMeshBin));
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, bin_pipeline);
        if (bin_descriptors == VK_NULL_HANDLE && descriptor_arena.push_descriptor_enabled()) {
            descriptor_arena.push_compute_descriptors(
                command, kernels.inline_mesh_bin_layout, 4, bin_writes);
        } else {
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    kernels.inline_mesh_bin_layout, 0, 1,
                                    &bin_descriptors, 0, nullptr);
        }
        struct BinPush {
            std::int32_t vertex_count;
            std::int32_t tri_count;
            std::int32_t x0, y0, x1, y1;
            std::int32_t tiles_x, tiles_y;
            std::int32_t max_tris_per_tile;
        } bin_push{
            static_cast<std::int32_t>(vertex_count),
            static_cast<std::int32_t>(tri_count),
            x0, y0, x1, y1,
            static_cast<std::int32_t>(tiles_x),
            static_cast<std::int32_t>(tiles_y),
            static_cast<std::int32_t>(kInlineMeshMaxTrisPerTile)};
        vkCmdPushConstants(command, kernels.inline_mesh_bin_layout,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(bin_push), &bin_push);
        vkCmdDispatch(command, static_cast<std::uint32_t>((tri_count + 63U) / 64U), 1, 1);
        ++stats.vk_cmd_dispatch_count;

        const VkBufferMemoryBarrier2KHR bin_barriers[2]{
            {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR, nullptr,
             VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
             VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_READ_BIT_KHR,
             VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
             inline_mesh_tile_counts_buffer.buffer, 0, tile_counts_bytes},
            {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR, nullptr,
             VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
             VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_READ_BIT_KHR,
             VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
             inline_mesh_tile_tris_buffer.buffer, 0, tile_tris_bytes}};
        emit_buffer_barriers2(command,
            std::span<const VkBufferMemoryBarrier2KHR>{bin_barriers, 2});
    }

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
        kernels.registry.resolve(
            use_binning ? GpuKernelId::InlineMeshBinned : GpuKernelId::InlineMesh));
    if (pipeline == VK_NULL_HANDLE)
        throw std::logic_error("Vulkan inline mesh pipeline was not registered");
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    if (descriptors == VK_NULL_HANDLE && descriptor_arena.push_descriptor_enabled()) {
        descriptor_arena.push_compute_descriptors(
            command, kernels.inline_mesh_layout, 8, writes);
    } else {
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.inline_mesh_layout, 0, 1, &descriptors, 0, nullptr);
    }
    vkCmdPushConstants(command, kernels.inline_mesh_layout, VK_SHADER_STAGE_COMPUTE_BIT,
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
