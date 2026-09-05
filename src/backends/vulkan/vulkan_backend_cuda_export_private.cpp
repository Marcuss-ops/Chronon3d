// vulkan_backend_cuda_export_private.cpp — VulkanBackend::Impl CUDA interop
// export path: external semaphore/surface creation, memory FD export and
// encoder copies. Compiled only when CHRONON3D_ENABLE_CUDA_INTEROP is set.

#include "vulkan_backend_impl.hpp"

#include <atomic>
#include <unistd.h>

namespace chronon3d::backends::vulkan {

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

VkSemaphore VulkanBackend::Impl::make_external_binary_semaphore() {
    VkExportSemaphoreCreateInfo export_info{
        VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};
    export_info.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    const VkSemaphoreCreateInfo info{
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &export_info, 0};
    VkSemaphore semaphore = VK_NULL_HANDLE;
    check(vkCreateSemaphore(device, &info, nullptr, &semaphore),
          "vkCreateSemaphore(external CUDA interop)");
    if (debug_context) {
        debug_context->set_semaphore_name(semaphore,
                                          "Chronon3D.Semaphore.CudaInterop");
    }
    return semaphore;
}

void VulkanBackend::Impl::create_cuda_external_surface(
    runtime::RenderSurfaceHandle handle,
    const runtime::SurfaceDesc& desc) {
    if (handle == runtime::kInvalidRenderSurfaceHandle ||
        desc.width == 0 || desc.height == 0 ||
        (desc.format != runtime::PixelFormat::Rgba32Float &&
         desc.format != runtime::PixelFormat::Rgba8Unorm &&
         desc.format != runtime::PixelFormat::Nv12 &&
         desc.format != runtime::PixelFormat::P010)) {
        throw std::invalid_argument(
            "CUDA external surface requires non-empty supported description");
    }
    if (surfaces.surface_bindings.contains(handle)) {
        throw std::invalid_argument("CUDA external surface handle already exists");
    }
    const auto slot = surfaces.next_slot++;
    auto& physical = surfaces.physical_surfaces[slot];
    make_image(physical.image, desc.width, desc.height, true,
               to_vk_format(desc.format));
    physical.image.cuda_to_vulkan = make_external_binary_semaphore();
    physical.image.vulkan_to_cuda = make_external_binary_semaphore();
    physical.desc = desc;
    surfaces.surface_bindings.emplace(handle, slot);
    surfaces.unplanned_surface_handles.insert(handle);
    ++stats.surface_creations;
}

CudaExternalMemoryInfo VulkanBackend::Impl::export_cuda_external_memory(
    runtime::RenderSurfaceHandle handle) const {
    const auto binding = surfaces.surface_bindings.find(handle);
    if (binding == surfaces.surface_bindings.end()) {
        throw std::invalid_argument("CUDA external surface handle is not bound");
    }
    const auto image_it = surfaces.physical_surfaces.find(binding->second);
    if (image_it == surfaces.physical_surfaces.end() ||
        !image_it->second.image.exportable) {
        throw std::invalid_argument("surface is not exportable to CUDA");
    }
    const auto alloc_info =
        memory_manager.allocation_info(image_it->second.image.allocation);
    if (alloc_info.offset != 0) {
        throw std::runtime_error(
            "External Vulkan allocation must be dedicated (offset == 0)");
    }
    auto get_fd = reinterpret_cast<PFN_vkGetMemoryFdKHR>(
        vkGetDeviceProcAddr(device, "vkGetMemoryFdKHR"));
    if (!get_fd) throw std::runtime_error("vkGetMemoryFdKHR is unavailable");
    VkMemoryGetFdInfoKHR info{VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};
    info.memory = alloc_info.deviceMemory;
    info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    int fd = -1;
    check(get_fd(device, &info, &fd), "vkGetMemoryFdKHR");
    auto get_semaphore_fd = reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(
        vkGetDeviceProcAddr(device, "vkGetSemaphoreFdKHR"));
    if (!get_semaphore_fd) {
        close(fd);
        throw std::runtime_error("vkGetSemaphoreFdKHR is unavailable");
    }
    const auto& image = image_it->second.image;
    VkSemaphoreGetFdInfoKHR cuda_to_vulkan_info{
        VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR, nullptr,
        image.cuda_to_vulkan,
        VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT};
    VkSemaphoreGetFdInfoKHR vulkan_to_cuda_info{
        VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR, nullptr,
        image.vulkan_to_cuda,
        VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT};
    int cuda_to_vulkan_fd = -1;
    int vulkan_to_cuda_fd = -1;
    check(get_semaphore_fd(device, &cuda_to_vulkan_info, &cuda_to_vulkan_fd),
          "vkGetSemaphoreFdKHR(cuda to Vulkan)");
    const auto second_result = get_semaphore_fd(
        device, &vulkan_to_cuda_info, &vulkan_to_cuda_fd);
    if (second_result != VK_SUCCESS) {
        close(fd);
        close(cuda_to_vulkan_fd);
        check(second_result, "vkGetSemaphoreFdKHR(Vulkan to CUDA)");
    }
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, image_it->second.image.image,
                                 &requirements);
    const auto fmt = image_it->second.image.format;
    const bool is_u8 = (fmt == VK_FORMAT_R8G8B8A8_UNORM ||
                        fmt == VK_FORMAT_R8G8B8A8_SRGB ||
                        fmt == VK_FORMAT_B8G8R8A8_UNORM ||
                        fmt == VK_FORMAT_B8G8R8A8_SRGB ||
                        fmt == VK_FORMAT_A8B8G8R8_UNORM_PACK32 ||
                        fmt == VK_FORMAT_A8B8G8R8_SRGB_PACK32);
    return CudaExternalMemoryInfo{
        fd, cuda_to_vulkan_fd, vulkan_to_cuda_fd, requirements.size,
        image_it->second.image.width, image_it->second.image.height,
        is_u8 ? 2u : 1u};
}

void VulkanBackend::Impl::prepare_cuda_surface_for_vulkan(
    runtime::RenderSurfaceHandle handle) {
    const auto slot = bound_slot(handle);
    const auto it = surfaces.physical_surfaces.find(slot);
    if (it == surfaces.physical_surfaces.end() ||
        !it->second.image.exportable ||
        it->second.image.cuda_to_vulkan == VK_NULL_HANDLE ||
        it->second.image.vulkan_to_cuda == VK_NULL_HANDLE) {
        throw std::invalid_argument("surface is not a CUDA external surface");
    }
    it->second.image.initialized = true;
    surfaces.cuda_ready_surfaces.insert(slot);
}

void VulkanBackend::Impl::copy_surface_to_cuda_encoder(
    runtime::RenderSurfaceHandle source,
    runtime::RenderSurfaceHandle destination,
    bool wait_for_completion) {
    const auto source_slot = bound_slot(source);
    const auto destination_slot = bound_slot(destination);
    auto& src = surfaces.physical_surfaces.at(source_slot).image;
    auto& dst = surfaces.physical_surfaces.at(destination_slot).image;
    if (!dst.exportable ||
        (dst.format != VK_FORMAT_B8G8R8A8_UNORM &&
         dst.format != VK_FORMAT_R32G32B32A32_SFLOAT)) {
        throw std::invalid_argument(
            "CUDA encoder destination must be exportable B8G8R8A8 or R32G32B32A32_SFLOAT");
    }
    if (!src.initialized) {
        spdlog::error(
            "[copy_cuda_diag] FAILED: src_handle={} src_slot={} src_init={} dst_slot={}",
            source, source_slot, src.initialized, destination_slot);
        throw std::runtime_error(
            "copy_surface_to_cuda_encoder: source surface is uninitialized");
    }
    const bool record_in_frame_batch = frame_batch.active;
    if (!record_in_frame_batch) begin_command_buffer();
    static std::atomic<int> s_copy_count{0};
    if (s_copy_count.fetch_add(1) < 5) {
        spdlog::info(
            "[copy_cuda_diag] src_slot={} dst_slot={} src_w={} src_h={} src_fmt={} dst_w={} dst_h={} batch_active={}",
            source_slot, destination_slot, src.width, src.height,
            static_cast<int>(src.format), dst.width, dst.height,
            record_in_frame_batch);
    }
    const VkCommandBuffer command = record_in_frame_batch
        ? active_command_buffer() : command_buffer;
    transition(command, src.image, VK_IMAGE_LAYOUT_GENERAL,
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    transition(command, dst.image,
               dst.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    if (src.format == dst.format && src.width == dst.width && src.height == dst.height) {
        VkImageCopy copy_region{};
        copy_region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy_region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy_region.extent = {src.width, src.height, 1};
        vkCmdCopyImage(command, src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &copy_region);
    } else {
        VkImageBlit region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.srcOffsets[1] = {static_cast<int>(src.width),
                                static_cast<int>(src.height), 1};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.dstOffsets[1] = {static_cast<int>(dst.width),
                                static_cast<int>(dst.height), 1};
        vkCmdBlitImage(command, src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &region, VK_FILTER_NEAREST);
    }
    transition(command, src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               VK_IMAGE_LAYOUT_GENERAL);
    transition(command, dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               VK_IMAGE_LAYOUT_GENERAL);
    dst.initialized = true;
    surfaces.cuda_export_ready_surfaces.insert(destination_slot);
    if (auto* counters = profiling::g_current_counters) {
        counters->gpu_surface_copy_frames.fetch_add(1,
                                                    std::memory_order_relaxed);
    }
    if (!record_in_frame_batch) submit(wait_for_completion);
}

#endif // CHRONON3D_ENABLE_CUDA_INTEROP

} // namespace chronon3d::backends::vulkan
