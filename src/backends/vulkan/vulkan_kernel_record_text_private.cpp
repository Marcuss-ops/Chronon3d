// vulkan_kernel_record_text_private.cpp — text run, layer batch and text-tile kernel recording.
// Split from the oversized unit per issue #51 (LEANUP-001);
// out-of-class definitions, declared in vulkan_backend_impl.hpp.

#include "vulkan_backend_impl.hpp"

namespace chronon3d::backends::vulkan {

namespace {

void emit_buffer_barrier2(VkDevice device, VkCommandBuffer command, VkBuffer buffer,
                          VkPipelineStageFlags2KHR src_stage, VkAccessFlags2KHR src_access,
                          VkPipelineStageFlags2KHR dst_stage, VkAccessFlags2KHR dst_access) {
    if (buffer == VK_NULL_HANDLE) return;
    const auto pipeline_barrier2 = reinterpret_cast<PFN_vkCmdPipelineBarrier2KHR>(
        vkGetDeviceProcAddr(device, "vkCmdPipelineBarrier2KHR"));
    if (!pipeline_barrier2) {
        throw std::runtime_error(
            "Vulkan: vkCmdPipelineBarrier2KHR unavailable after synchronization2 enablement");
    }
    const VkBufferMemoryBarrier2KHR barrier{
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR,
        nullptr,
        src_stage,
        src_access,
        dst_stage,
        dst_access,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        buffer,
        0,
        VK_WHOLE_SIZE};
    const VkDependencyInfoKHR dependency{
        VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,
        nullptr,
        0,
        0,
        nullptr,
        1,
        &barrier,
        0,
        nullptr};
    pipeline_barrier2(command, &dependency);
}

} // namespace

    void VulkanBackend::Impl::record_text_run(VkCommandBuffer command, VkDescriptorSet descriptors,
                         const Image& destination, std::int32_t glyph_count,
                         VkBuffer instance_buffer, bool instance_updated,
                         float current_frame, const Color& highlight_color,
                         bool highlight_enabled,
                         std::int32_t dispatch_origin_x,
                         std::int32_t dispatch_origin_y,
                         std::int32_t dispatch_end_x,
                         std::int32_t dispatch_end_y) {
        // Multiple TextRun passes can occur in one frame (for example the
        // watermark followed by subtitles). They intentionally share the
        // per-frame instance buffer, but the previous dispatch may still be
        // reading it when the next vkCmdUpdateBuffer records its write.
        // Keep both halves of that dependency on Synchronization2.
        if (instance_updated) {
            emit_buffer_barrier2(
                device, command, instance_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
                VK_ACCESS_2_SHADER_READ_BIT_KHR,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
                VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR);
            emit_buffer_barrier2(
                device, command, instance_buffer,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
                VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
                VK_ACCESS_2_SHADER_READ_BIT_KHR);
        }
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::TextRun)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            std::int32_t glyph_count;
            float current_frame;
            std::int32_t highlight_enabled;
            std::int32_t padding;
            float highlight_color[4];
            std::int32_t dispatch_origin[2];
        } params{glyph_count, current_frame, highlight_enabled ? 1 : 0, 0,
                 {highlight_color.r, highlight_color.g,
                  highlight_color.b, highlight_color.a}, {0, 0}};
        const auto x0 = std::clamp(dispatch_origin_x, 0,
                                   static_cast<std::int32_t>(destination.width));
        const auto y0 = std::clamp(dispatch_origin_y, 0,
                                   static_cast<std::int32_t>(destination.height));
        const auto x1 = std::clamp(dispatch_end_x, x0,
                                   static_cast<std::int32_t>(destination.width));
        const auto y1 = std::clamp(dispatch_end_y, y0,
                                   static_cast<std::int32_t>(destination.height));
        params.dispatch_origin[0] = x0;
        params.dispatch_origin[1] = y0;
        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command, (x1 - x0 + 15) / 16,
                      (y1 - y0 + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void VulkanBackend::Impl::record_layer_batch(VkCommandBuffer command, VkDescriptorSet descriptors,
                            const Image& destination, std::int32_t instance_count,
                            VkBuffer instance_buffer, bool instance_updated,
                            std::int32_t dispatch_origin_x,
                            std::int32_t dispatch_origin_y,
                            std::int32_t dispatch_end_x,
                            std::int32_t dispatch_end_y) {
        if (instance_updated) {
            emit_buffer_barrier2(
                device, command, instance_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
                VK_ACCESS_2_SHADER_READ_BIT_KHR,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
                VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR);
            emit_buffer_barrier2(
                device, command, instance_buffer,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
                VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
                VK_ACCESS_2_SHADER_READ_BIT_KHR);
        }
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::LayerBatch)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.general_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            std::int32_t instance_count;
            std::int32_t padding0;
            std::int32_t dispatch_origin[2];
        } params{instance_count, 0, {dispatch_origin_x, dispatch_origin_y}};
        const auto x0 = std::clamp(dispatch_origin_x, 0,
                                   static_cast<std::int32_t>(destination.width));
        const auto y0 = std::clamp(dispatch_origin_y, 0,
                                   static_cast<std::int32_t>(destination.height));
        const auto x1 = std::clamp(dispatch_end_x, x0,
                                   static_cast<std::int32_t>(destination.width));
        const auto y1 = std::clamp(dispatch_end_y, y0,
                                   static_cast<std::int32_t>(destination.height));
        params.dispatch_origin[0] = x0;
        params.dispatch_origin[1] = y0;
        vkCmdPushConstants(command, kernels.general_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command, (x1 - x0 + 15) / 16,
                      (y1 - y0 + 15) / 16, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void VulkanBackend::Impl::record_text_tile_bin(VkCommandBuffer command, VkDescriptorSet descriptors,
                              std::int32_t glyph_count, std::int32_t tiles_x,
                              std::int32_t tiles_y, std::int32_t max_glyphs_per_tile,
                              std::int32_t dst_width, std::int32_t dst_height) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(
                              GpuKernelId::TextTileBin)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.text_tile_bin_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            std::uint32_t glyph_count;
            std::uint32_t tiles_x;
            std::uint32_t tiles_y;
            std::uint32_t max_glyphs_per_tile;
            std::uint32_t dst_width;
            std::uint32_t dst_height;
        } params{static_cast<std::uint32_t>(glyph_count), static_cast<std::uint32_t>(tiles_x),
                 static_cast<std::uint32_t>(tiles_y), static_cast<std::uint32_t>(max_glyphs_per_tile),
                 static_cast<std::uint32_t>(dst_width), static_cast<std::uint32_t>(dst_height)};
        vkCmdPushConstants(command, kernels.text_tile_bin_layout,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);
        vkCmdDispatch(command, (static_cast<std::uint32_t>(glyph_count) + 63) / 64, 1, 1);
        ++stats.vk_cmd_dispatch_count;
    }

    void VulkanBackend::Impl::record_text_tile_raster(VkCommandBuffer command, VkDescriptorSet descriptors,
                                 std::int32_t tiles_x, std::int32_t tiles_y,
                                 std::int32_t max_glyphs_per_tile,
                                 const Image& destination, const Image& atlas) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernels.registry.resolve(
                              GpuKernelId::TextTileRaster)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                kernels.text_tile_raster_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            std::uint32_t tiles_x;
            std::uint32_t tiles_y;
            std::uint32_t max_glyphs_per_tile;
            std::uint32_t dst_width;
            std::uint32_t dst_height;
            float atlas_inv_w;
            float atlas_inv_h;
        } params{static_cast<std::uint32_t>(tiles_x), static_cast<std::uint32_t>(tiles_y),
                 static_cast<std::uint32_t>(max_glyphs_per_tile), destination.width,
                 destination.height, 1.0f / static_cast<float>(atlas.width),
                 1.0f / static_cast<float>(atlas.height)};
        vkCmdPushConstants(command, kernels.text_tile_raster_layout,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);
        vkCmdDispatch(command, static_cast<std::uint32_t>(tiles_x),
                      static_cast<std::uint32_t>(tiles_y), 1);
        ++stats.vk_cmd_dispatch_count;
    }
} // namespace chronon3d::backends::vulkan
