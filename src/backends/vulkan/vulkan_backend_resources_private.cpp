// vulkan_backend_resources_private.cpp — VulkanBackend::Impl resource
// lifecycle helpers (images, staging and per-pass ring buffers).

#include "vulkan_backend_impl.hpp"

namespace chronon3d::backends::vulkan {

void VulkanBackend::Impl::destroy_image(Image& target) {
    if (target.cuda_to_vulkan != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, target.cuda_to_vulkan, nullptr);
    }
    if (target.vulkan_to_cuda != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, target.vulkan_to_cuda, nullptr);
    }
    if (target.view != VK_NULL_HANDLE) vkDestroyImageView(device, target.view, nullptr);
    if (target.image != VK_NULL_HANDLE) {
        VulkanImageAllocation img_alloc{
            .image = target.image,
            .allocation = target.allocation,
            .size = 0,
            .exportable = target.exportable};
        memory_manager.destroy_image(img_alloc);
    }
    target = {};
}

void VulkanBackend::Impl::destroy_upload_slot(VulkanUploadRing::UploadSlot& slot) {
    if (slot.command_buffer != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(device, command_pool, 1, &slot.command_buffer);
    }
    if (slot.fence != VK_NULL_HANDLE) vkDestroyFence(device, slot.fence, nullptr);
    if (slot.buffer_allocation.buffer != VK_NULL_HANDLE) {
        memory_manager.destroy_buffer(slot.buffer_allocation);
    }
    slot = {};
}

void VulkanBackend::Impl::make_image(Image& target,
                                     std::uint32_t width,
                                     std::uint32_t height,
                                     bool exportable,
                                     VkFormat format) {
    VkExternalMemoryImageCreateInfo external_image{
        VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    external_image.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    VkImageCreateInfo info{
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr, 0, VK_IMAGE_TYPE_2D,
        format, {width, height, 1}, 1, 1,
        VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_EXCLUSIVE, 0, nullptr, VK_IMAGE_LAYOUT_UNDEFINED};
    info.pNext = exportable ? &external_image : nullptr;

    const auto mem_class = exportable
        ? VulkanMemoryClass::ExternalExportable
        : VulkanMemoryClass::DeviceLocal;
    const auto alloc = memory_manager.create_image(info, mem_class);
    target.image = alloc.image;
    target.allocation = alloc.allocation;
    if (debug_context) {
        debug_context->set_image_name(
            target.image,
            exportable ? "Chronon3D.CudaExportableImage" :
                         "Chronon3D.DeviceLocalImage");
    }

    const VkImageViewCreateInfo view{
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr, 0, target.image,
        VK_IMAGE_VIEW_TYPE_2D, format, {},
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    check(vkCreateImageView(device, &view, nullptr, &target.view),
          "vkCreateImageView");
    if (debug_context) {
        debug_context->set_image_view_name(
            target.view,
            exportable ? "Chronon3D.CudaExportableImageView" :
                         "Chronon3D.DeviceLocalImageView");
    }
    target.width = width;
    target.height = height;
    target.format = format;
    target.exportable = exportable;
}

void VulkanBackend::Impl::ensure_images(std::uint32_t width,
                                        std::uint32_t height) {
    if (dst.width == width && dst.height == height &&
        src.image != VK_NULL_HANDLE) return;
    destroy_image(dst);
    destroy_image(src);
    check(vkResetDescriptorPool(device, descriptor_pool, 0),
          "vkResetDescriptorPool");
    make_image(dst, width, height);
    make_image(src, width, height);
    descriptor_set = VK_NULL_HANDLE;
    glow_descriptor_sets = {};
    bind_descriptors(dst, src);
}

void VulkanBackend::Impl::ensure_staging(VkDeviceSize bytes) {
    if (staging.size >= bytes) return;
    if (staging.buffer != VK_NULL_HANDLE) {
        memory_manager.destroy_buffer(staging);
    }
    const VkBufferCreateInfo info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, bytes,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
    staging = memory_manager.create_buffer(info, VulkanMemoryClass::HostUpload);
    if (debug_context) {
        debug_context->set_buffer_name(staging.buffer,
                                       "Chronon3D.Buffer.Staging");
    }
    ++stats.staging_allocations;
}

void VulkanBackend::Impl::ensure_glyph_instance_buffer(VkDeviceSize bytes,
                                                        std::size_t index) {
    if (index >= kGlyphInstanceRingSize) index = 0;
    if (glyph_instance_buffers[index].size >= bytes) return;
    if (glyph_instance_buffers[index].buffer != VK_NULL_HANDLE) {
        memory_manager.destroy_buffer(glyph_instance_buffers[index]);
    }
    const VkBufferCreateInfo info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, bytes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
    glyph_instance_buffers[index] =
        memory_manager.create_buffer(info, VulkanMemoryClass::DeviceLocal);
    if (debug_context) {
        debug_context->set_buffer_name(glyph_instance_buffers[index].buffer,
                                       "Chronon3D.Buffer.GlyphInstance");
    }
    glyph_instance_hashes[index] = 0;
    glyph_instance_sizes[index] = 0;
}

void VulkanBackend::Impl::ensure_layer_instance_buffer(VkDeviceSize bytes,
                                                        std::size_t index) {
    if (index >= kGlyphInstanceRingSize) index = 0;
    if (layer_instance_buffers[index].size >= bytes) return;
    if (layer_instance_buffers[index].buffer != VK_NULL_HANDLE) {
        memory_manager.destroy_buffer(layer_instance_buffers[index]);
    }
    const VkBufferCreateInfo info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, bytes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
    layer_instance_buffers[index] =
        memory_manager.create_buffer(info, VulkanMemoryClass::DeviceLocal);
    if (debug_context) {
        debug_context->set_buffer_name(layer_instance_buffers[index].buffer,
                                       "Chronon3D.Buffer.LayerInstance");
    }
    layer_instance_hashes[index] = 0;
    layer_instance_sizes[index] = 0;
}

void VulkanBackend::Impl::ensure_text_run_dynamic_buffer(VkDeviceSize bytes,
                                                          std::size_t index) {
    if (index >= kGlyphInstanceRingSize) index = 0;
    if (text_run_dynamic_buffers[index].size >= bytes) return;
    if (text_run_dynamic_buffers[index].buffer != VK_NULL_HANDLE) {
        memory_manager.destroy_buffer(text_run_dynamic_buffers[index]);
    }
    const VkBufferCreateInfo info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, bytes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
    text_run_dynamic_buffers[index] =
        memory_manager.create_buffer(info, VulkanMemoryClass::DeviceLocal);
    if (debug_context) {
        debug_context->set_buffer_name(text_run_dynamic_buffers[index].buffer,
                                       "Chronon3D.Buffer.TextRunDynamic");
    }
    text_run_dynamic_hashes[index] = 0;
    text_run_dynamic_sizes[index] = 0;
}

void VulkanBackend::Impl::ensure_text_tile_buffer(VkDeviceSize bytes,
                                                   std::size_t index,
                                                   bool indices) {
    if (index >= kGlyphInstanceRingSize) index = 0;
    auto& buffer_alloc = indices ? text_tile_index_buffers[index]
                                 : text_tile_count_buffers[index];
    if (buffer_alloc.size >= bytes) return;
    if (buffer_alloc.buffer != VK_NULL_HANDLE) {
        memory_manager.destroy_buffer(buffer_alloc);
    }
    const VkBufferCreateInfo info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, bytes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
    buffer_alloc = memory_manager.create_buffer(info, VulkanMemoryClass::DeviceLocal);
    if (debug_context) {
        debug_context->set_buffer_name(
            buffer_alloc.buffer,
            indices ? "Chronon3D.Buffer.TextTileIndex" :
                      "Chronon3D.Buffer.TextTileCount");
    }
}

} // namespace chronon3d::backends::vulkan
