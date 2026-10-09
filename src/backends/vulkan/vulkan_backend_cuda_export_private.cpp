// vulkan_backend_cuda_export_private.cpp — VulkanBackend::Impl CUDA interop
// export path: external semaphore/surface creation, memory FD export and
// encoder copies. Compiled only when CHRONON3D_ENABLE_CUDA_INTEROP is set.

#include "vulkan_backend_impl.hpp"
#include "vulkan_external_fds.hpp"

#include <chronon3d/runtime/cuda_surface_contract.hpp>

#include <atomic>
#include <cstdlib>
#include <string>
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

VkSemaphore VulkanBackend::Impl::make_external_timeline_semaphore() {
    VkExportSemaphoreCreateInfo export_info{
        VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};
    export_info.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    const VkSemaphoreTypeCreateInfo timeline_type{
        VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, &export_info,
        VK_SEMAPHORE_TYPE_TIMELINE, 0};
    const VkSemaphoreCreateInfo info{
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &timeline_type, 0};
    VkSemaphore semaphore = VK_NULL_HANDLE;
    check(vkCreateSemaphore(device, &info, nullptr, &semaphore),
          "vkCreateSemaphore(external CUDA timeline interop)");
    if (debug_context) {
        debug_context->set_semaphore_name(
            semaphore, "Chronon3D.Semaphore.CudaInteropTimeline");
    }
    return semaphore;
}

void VulkanBackend::Impl::create_cuda_external_surface(
    runtime::RenderSurfaceHandle handle,
    const runtime::SurfaceDesc& desc) {
    const auto contract = runtime::validate_cuda_surface_contract(desc);
    if (handle == runtime::kInvalidRenderSurfaceHandle || !contract.valid()) {
        throw std::invalid_argument(
            std::string("CUDA external surface contract rejected: ") +
            contract.reason);
    }
    if (surfaces.surface_bindings.contains(handle)) {
        throw std::invalid_argument("CUDA external surface handle already exists");
    }
    const auto slot = surfaces.next_slot++;
    auto& physical = surfaces.physical_surfaces[slot];
    make_image(physical.image, desc.width, desc.height, true,
               to_vk_format(desc.format));
    physical.image.interop_generation = std::make_shared<InteropGeneration>();
    try {
        // Timeline is the production-preferred CUDA interop protocol. Binary
        // semaphores remain a certified compatibility fallback for drivers
        // that reject external timeline semaphore import/export, or when an
        // operator explicitly disables the protocol for diagnosis.
        const bool disable_timeline =
            std::getenv("CHRONON3D_DISABLE_CUDA_TIMELINE") != nullptr;
        if (disable_timeline) {
            throw std::runtime_error(
                "timeline bridge explicitly disabled via CHRONON3D_DISABLE_CUDA_TIMELINE");
        }
        physical.image.cuda_to_vulkan = make_external_timeline_semaphore();
        physical.image.vulkan_to_cuda = make_external_timeline_semaphore();
        physical.image.cuda_timeline = true;
    } catch (const std::exception& error) {
        if (physical.image.cuda_to_vulkan != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, physical.image.cuda_to_vulkan, nullptr);
            physical.image.cuda_to_vulkan = VK_NULL_HANDLE;
        }
        if (physical.image.vulkan_to_cuda != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, physical.image.vulkan_to_cuda, nullptr);
            physical.image.vulkan_to_cuda = VK_NULL_HANDLE;
        }
        spdlog::warn(
            "[Vulkan] external timeline semaphore unavailable; using binary CUDA bridge: {}",
            error.what());
        physical.image.cuda_to_vulkan = make_external_binary_semaphore();
        physical.image.vulkan_to_cuda = make_external_binary_semaphore();
        physical.image.cuda_timeline = false;
    }
    physical.desc = desc;
    surfaces.surface_bindings.emplace(handle, slot);
    surfaces.note_binding_created();
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
    VulkanExternalFds descriptors;
    check(get_fd(device, &info, &descriptors[0]), "vkGetMemoryFdKHR");
    auto get_semaphore_fd = reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(
        vkGetDeviceProcAddr(device, "vkGetSemaphoreFdKHR"));
    if (!get_semaphore_fd) {
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
    check(get_semaphore_fd(device, &cuda_to_vulkan_info, &descriptors[1]),
          "vkGetSemaphoreFdKHR(cuda to Vulkan)");
    check(get_semaphore_fd(device, &vulkan_to_cuda_info, &descriptors[2]),
          "vkGetSemaphoreFdKHR(Vulkan to CUDA)");
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
    CudaExternalMemoryInfo exported{
        descriptors[0], descriptors[1], descriptors[2], requirements.size,
        image_it->second.image.width, image_it->second.image.height,
        is_u8 ? 2u : 1u, image_it->second.image.cuda_timeline,
        image_it->second.image.interop_generation};
    // A per-surface FIFO preserves the signal value associated with the frame
    // being consumed. Reading the generation's latest counter here can select
    // a future frame whose Vulkan submission is itself waiting for CUDA to
    // release this surface, creating a cross-API wait cycle.
    const auto generation = image_it->second.image.interop_generation;
    const bool timeline_semaphores = image_it->second.image.cuda_timeline;
    exported.wait_vulkan_fence = [generation, timeline_semaphores] {
        if (!timeline_semaphores || !generation) return std::uint64_t{0};
        const auto value = generation->take_next_vulkan_to_cuda_wait();
        if (!value) {
            throw std::logic_error(
                "CUDA requested a Vulkan surface wait without a queued frame signal");
        }
        return *value;
    };
    // std::function assignment above can allocate and throw: ownership must
    // not escape until the complete handoff has been constructed.
    for (std::size_t i = 0; i < 3; ++i) (void)descriptors.release(i);
    return exported;
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
    surfaces.cuda_pending_surfaces.insert(slot);
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
    // The encoder handoff is a typed lane, not a general-purpose image
    // conversion node.  A format or extent mismatch used to fall through to
    // vkCmdBlitImage here, hiding an implicit conversion in the native export
    // hot path.  Reject it before recording any command; callers that need a
    // conversion must insert an explicit graph node and hand this function a
    // matching RGBA surface.
    if (src.format != dst.format || src.width != dst.width ||
        src.height != dst.height) {
        throw std::invalid_argument(
            "CUDA encoder lane requires matching source/destination RGBA "
            "format and extent; use an explicit conversion node");
    }
    const bool record_in_frame_batch = frame_batch.active;
    if (!record_in_frame_batch) begin_command_buffer();
    const VkCommandBuffer command = record_in_frame_batch
        ? active_command_buffer() : command_buffer;
    acquire_cuda_surface_for_recording(command, source_slot);
    if (destination_slot != source_slot)
        acquire_cuda_surface_for_recording(command, destination_slot);

    if (source_slot == destination_slot) {
        // Z3 direct encode-surface binding: source IS destination.
        // No blit or D2D copy needed; gate 6 stays 0.
        // Ensure image layout is GENERAL and register for CUDA export signaling.
        transition(command, dst.image,
                   dst.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_GENERAL);
        dst.initialized = true;
        surfaces.cuda_export_ready_surfaces.insert(destination_slot);
        if (!record_in_frame_batch) submit(wait_for_completion);
        return;
    }

    static std::atomic<int> s_copy_count{0};
    if (s_copy_count.fetch_add(1) < 5) {
        spdlog::info(
            "[copy_cuda_diag] src_slot={} dst_slot={} src_w={} src_h={} src_fmt={} dst_w={} dst_h={} batch_active={}",
            source_slot, destination_slot, src.width, src.height,
            static_cast<int>(src.format), dst.width, dst.height,
            record_in_frame_batch);
    }
    transition(command, src.image, VK_IMAGE_LAYOUT_GENERAL,
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    transition(command, dst.image,
               dst.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageCopy copy_region{};
    copy_region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy_region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy_region.extent = {src.width, src.height, 1};
    vkCmdCopyImage(command, src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &copy_region);
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
