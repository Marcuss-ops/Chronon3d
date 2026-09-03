// vulkan_descriptor_arena_private.cpp — VulkanBackend::Impl descriptor
// allocation, image/staging helpers, synchronization and CUDA export.

#include "vulkan_backend_impl.hpp"

#include <type_traits>

namespace chronon3d::backends::vulkan {

namespace {

PFN_vkCmdPipelineBarrier2KHR require_pipeline_barrier2(VkDevice device) {
    const auto pipeline_barrier2 = reinterpret_cast<PFN_vkCmdPipelineBarrier2KHR>(
        vkGetDeviceProcAddr(device, "vkCmdPipelineBarrier2KHR"));
    if (!pipeline_barrier2) {
        throw std::runtime_error(
            "Vulkan: vkCmdPipelineBarrier2KHR unavailable after synchronization2 enablement");
    }
    return pipeline_barrier2;
}

VkPipelineStageFlags2KHR to_vk_stage(runtime::PipelineStage stages) {
    using U = std::underlying_type_t<runtime::PipelineStage>;
    U remaining = static_cast<U>(stages);
    VkPipelineStageFlags2KHR result = VK_PIPELINE_STAGE_2_NONE_KHR;
    const auto consume = [&](runtime::PipelineStage bit,
                             VkPipelineStageFlags2KHR native) {
        const U mask = static_cast<U>(bit);
        if ((remaining & mask) != 0) {
            result |= native;
            remaining &= ~mask;
        }
    };
    consume(runtime::PipelineStage::ComputeShader,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR);
    consume(runtime::PipelineStage::Transfer,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR);
    consume(runtime::PipelineStage::Host,
            VK_PIPELINE_STAGE_2_HOST_BIT_KHR);
    consume(runtime::PipelineStage::VertexShader,
            VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR);
    consume(runtime::PipelineStage::FragmentShader,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR);
    consume(runtime::PipelineStage::ColorOutput,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR);
#ifdef VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR
    consume(runtime::PipelineStage::VideoDecode,
            VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR);
#endif
#ifdef VK_PIPELINE_STAGE_2_VIDEO_ENCODE_BIT_KHR
    consume(runtime::PipelineStage::VideoEncode,
            VK_PIPELINE_STAGE_2_VIDEO_ENCODE_BIT_KHR);
#endif
    consume(runtime::PipelineStage::AllGraphics,
            VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT_KHR);
    consume(runtime::PipelineStage::AllCommands,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR);
    if (remaining != 0) {
        throw std::logic_error("Vulkan: unsupported canonical PipelineStage bit");
    }
    return result;
}

VkAccessFlags2KHR to_vk_access(runtime::AccessMask access) {
    using U = std::underlying_type_t<runtime::AccessMask>;
    U remaining = static_cast<U>(access);
    VkAccessFlags2KHR result = VK_ACCESS_2_NONE_KHR;
    const auto consume = [&](runtime::AccessMask bit, VkAccessFlags2KHR native) {
        const U mask = static_cast<U>(bit);
        if ((remaining & mask) != 0) {
            result |= native;
            remaining &= ~mask;
        }
    };
    consume(runtime::AccessMask::ShaderRead, VK_ACCESS_2_SHADER_READ_BIT_KHR);
    consume(runtime::AccessMask::ShaderWrite, VK_ACCESS_2_SHADER_WRITE_BIT_KHR);
    consume(runtime::AccessMask::TransferRead, VK_ACCESS_2_TRANSFER_READ_BIT_KHR);
    consume(runtime::AccessMask::TransferWrite, VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR);
    consume(runtime::AccessMask::HostRead, VK_ACCESS_2_HOST_READ_BIT_KHR);
    consume(runtime::AccessMask::HostWrite, VK_ACCESS_2_HOST_WRITE_BIT_KHR);
    consume(runtime::AccessMask::MemoryRead, VK_ACCESS_2_MEMORY_READ_BIT_KHR);
    consume(runtime::AccessMask::MemoryWrite, VK_ACCESS_2_MEMORY_WRITE_BIT_KHR);
    consume(runtime::AccessMask::ColorRead,
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT_KHR);
    consume(runtime::AccessMask::ColorWrite,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT_KHR);
#ifdef VK_ACCESS_2_VIDEO_DECODE_READ_BIT_KHR
    consume(runtime::AccessMask::VideoDecodeRead,
            VK_ACCESS_2_VIDEO_DECODE_READ_BIT_KHR);
    consume(runtime::AccessMask::VideoDecodeWrite,
            VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR);
#endif
#ifdef VK_ACCESS_2_VIDEO_ENCODE_READ_BIT_KHR
    consume(runtime::AccessMask::VideoEncodeRead,
            VK_ACCESS_2_VIDEO_ENCODE_READ_BIT_KHR);
    consume(runtime::AccessMask::VideoEncodeWrite,
            VK_ACCESS_2_VIDEO_ENCODE_WRITE_BIT_KHR);
#endif
    if (remaining != 0) {
        throw std::logic_error("Vulkan: unsupported canonical AccessMask bit");
    }
    return result;
}

VkImageLayout to_vk_layout(runtime::ResourceLayout layout) {
    switch (layout) {
    case runtime::ResourceLayout::Undefined:
        return VK_IMAGE_LAYOUT_UNDEFINED;
    case runtime::ResourceLayout::General:
        return VK_IMAGE_LAYOUT_GENERAL;
    case runtime::ResourceLayout::ShaderReadOnly:
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case runtime::ResourceLayout::TransferSource:
        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case runtime::ResourceLayout::TransferDestination:
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    case runtime::ResourceLayout::ColorAttachment:
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case runtime::ResourceLayout::Present:
        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    case runtime::ResourceLayout::External:
        return VK_IMAGE_LAYOUT_GENERAL;
    case runtime::ResourceLayout::VideoDecodeSrc:
#ifdef VK_IMAGE_LAYOUT_VIDEO_DECODE_SRC_KHR
        return VK_IMAGE_LAYOUT_VIDEO_DECODE_SRC_KHR;
#else
        break;
#endif
    case runtime::ResourceLayout::VideoDecodeDst:
#ifdef VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR
        return VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR;
#else
        break;
#endif
    case runtime::ResourceLayout::VideoEncodeSrc:
#ifdef VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR
        return VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR;
#else
        break;
#endif
    case runtime::ResourceLayout::VideoEncodeDst:
#ifdef VK_IMAGE_LAYOUT_VIDEO_ENCODE_DST_KHR
        return VK_IMAGE_LAYOUT_VIDEO_ENCODE_DST_KHR;
#else
        break;
#endif
    }
    throw std::logic_error("Vulkan: unsupported canonical ResourceLayout");
}

VkImageAspectFlags to_vk_aspects(runtime::ResourceAspect aspects) {
    using U = std::underlying_type_t<runtime::ResourceAspect>;
    U remaining = static_cast<U>(aspects);
    VkImageAspectFlags result = 0;
    const auto consume = [&](runtime::ResourceAspect bit, VkImageAspectFlags native) {
        const U mask = static_cast<U>(bit);
        if ((remaining & mask) != 0) {
            result |= native;
            remaining &= ~mask;
        }
    };
    consume(runtime::ResourceAspect::Color, VK_IMAGE_ASPECT_COLOR_BIT);
    consume(runtime::ResourceAspect::Depth, VK_IMAGE_ASPECT_DEPTH_BIT);
    consume(runtime::ResourceAspect::Stencil, VK_IMAGE_ASPECT_STENCIL_BIT);
#ifdef VK_IMAGE_ASPECT_PLANE_0_BIT
    consume(runtime::ResourceAspect::Plane0, VK_IMAGE_ASPECT_PLANE_0_BIT);
    consume(runtime::ResourceAspect::Plane1, VK_IMAGE_ASPECT_PLANE_1_BIT);
    consume(runtime::ResourceAspect::Plane2, VK_IMAGE_ASPECT_PLANE_2_BIT);
#endif
    if (remaining != 0 || result == 0) {
        throw std::logic_error("Vulkan: unsupported canonical ResourceAspect");
    }
    return result;
}

runtime::ResourceState state_for_vk_layout(VkImageLayout layout) {
    switch (layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
        return runtime::ResourceState::undefined_state();
    case VK_IMAGE_LAYOUT_GENERAL:
        return runtime::ResourceState{
            .stages = runtime::PipelineStage::ComputeShader,
            .access = runtime::AccessMask::ShaderRead |
                      runtime::AccessMask::ShaderWrite,
            .layout = runtime::ResourceLayout::General,
            .queue = runtime::QueueClass::Compute,
        };
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        return runtime::ResourceState{
            .stages = runtime::PipelineStage::Transfer,
            .access = runtime::AccessMask::TransferRead,
            .layout = runtime::ResourceLayout::TransferSource,
            .queue = runtime::QueueClass::Transfer,
        };
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        return runtime::ResourceState{
            .stages = runtime::PipelineStage::Transfer,
            .access = runtime::AccessMask::TransferWrite,
            .layout = runtime::ResourceLayout::TransferDestination,
            .queue = runtime::QueueClass::Transfer,
        };
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return runtime::ResourceState{
            .stages = runtime::PipelineStage::ComputeShader |
                      runtime::PipelineStage::FragmentShader,
            .access = runtime::AccessMask::ShaderRead,
            .layout = runtime::ResourceLayout::ShaderReadOnly,
            .queue = runtime::QueueClass::GraphicsCompute,
        };
    default:
        throw std::logic_error(
            "Vulkan: standalone layout transition is outside canonical mapper");
    }
}

} // namespace

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
#endif

void VulkanBackend::Impl::ensure_descriptor_set() {
    if (descriptor_set != VK_NULL_HANDLE) return;
    const VkDescriptorSetAllocateInfo allocation{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr,
        descriptor_pool, 1, &descriptor_layout};
    check(vkAllocateDescriptorSets(device, &allocation, &descriptor_set),
          "vkAllocateDescriptorSets");
}

void VulkanBackend::Impl::bind_descriptors(const Image& destination,
                                           const Image& source) {
    ensure_descriptor_set();
    write_descriptors(descriptor_set, destination, source);
}

void VulkanBackend::Impl::write_descriptors(VkDescriptorSet set,
                                            const Image& destination,
                                            const Image& source) {
    const VkDescriptorImageInfo dst_info{
        VK_NULL_HANDLE, destination.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo src_info{
        VK_NULL_HANDLE, source.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkWriteDescriptorSet writes[] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dst_info, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &src_info, nullptr, nullptr}};
    vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
}

void VulkanBackend::Impl::write_fill_rect_descriptors(
    VkDescriptorSet set, const Image& destination) {
    const VkDescriptorImageInfo dst_info{
        VK_NULL_HANDLE, destination.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkWriteDescriptorSet writes[] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dst_info, nullptr, nullptr}};
    vkUpdateDescriptorSets(device, 1, writes, 0, nullptr);
}

void VulkanBackend::Impl::bind_fill_rect_descriptors(const Image& destination) {
    ensure_descriptor_set();
    write_fill_rect_descriptors(descriptor_set, destination);
}

void VulkanBackend::Impl::write_matte_descriptors(
    VkDescriptorSet set, const Image& destination,
    const Image& target, const Image& matte) {
    const VkDescriptorImageInfo dst_info{
        VK_NULL_HANDLE, destination.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo target_info{
        VK_NULL_HANDLE, target.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo matte_info{
        VK_NULL_HANDLE, matte.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkWriteDescriptorSet writes[] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dst_info, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &target_info, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &matte_info, nullptr, nullptr}};
    vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
}

void VulkanBackend::Impl::write_text_run_descriptors(
    VkDescriptorSet set, const Image& destination,
    const Image& atlas, VkBuffer instance_buffer) {
    const VkDescriptorImageInfo dst_info{
        VK_NULL_HANDLE, destination.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo atlas_info{
        VK_NULL_HANDLE, atlas.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorBufferInfo buffer_info{instance_buffer, 0, VK_WHOLE_SIZE};
    const VkWriteDescriptorSet writes[] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dst_info, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &atlas_info, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 3, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &buffer_info, nullptr}};
    vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
}

void VulkanBackend::Impl::write_layer_batch_descriptors(
    VkDescriptorSet set, const Image& destination,
    const Image& source, VkBuffer instance_buffer) {
    const VkDescriptorImageInfo dst_info{
        VK_NULL_HANDLE, destination.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo src_info{
        VK_NULL_HANDLE, source.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorBufferInfo buffer_info{instance_buffer, 0, VK_WHOLE_SIZE};
    const VkWriteDescriptorSet writes[] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dst_info, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &src_info, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 3, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &buffer_info, nullptr}};
    vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
}

void VulkanBackend::Impl::write_text_batch_descriptors(
    VkDescriptorSet set, const Image& destination,
    const Image& atlas, VkBuffer glyph_buffer, VkBuffer run_buffer) {
    const VkDescriptorImageInfo dst_info{
        VK_NULL_HANDLE, destination.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo atlas_info{
        VK_NULL_HANDLE, atlas.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorBufferInfo glyph_info{glyph_buffer, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo run_info{run_buffer, 0, VK_WHOLE_SIZE};
    const VkWriteDescriptorSet writes[] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dst_info, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &atlas_info, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 3, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &glyph_info, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 4, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &run_info, nullptr}};
    vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
}

VkDescriptorSet VulkanBackend::Impl::ensure_glow_descriptor_set(std::size_t index) {
    auto& set = glow_descriptor_sets[index];
    if (set != VK_NULL_HANDLE) return set;
    const VkDescriptorSetAllocateInfo allocation{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr,
        descriptor_pool, 1, &descriptor_layout};
    check(vkAllocateDescriptorSets(device, &allocation, &set),
          "vkAllocateDescriptorSets(glow)");
    return set;
}

VkCommandBuffer VulkanBackend::Impl::active_command_buffer() const noexcept {
    return frame_batch.command_buffers[frame_batch.next_slot];
}

VkDescriptorSet VulkanBackend::Impl::allocate_pass_descriptor_set() {
    const auto set =
        frame_batch.descriptor_allocators[frame_batch.next_slot].allocate();
    frame_batch.descriptor_sets.push_back(set);
    ++stats.descriptor_allocations;
    return set;
}

VkDescriptorSet VulkanBackend::Impl::allocate_text_tile_bin_descriptor_set() {
    if (!frame_batch.active) {
        throw std::logic_error("text tile bin requires an active frame batch");
    }
    return frame_batch.text_tile_bin_allocators[frame_batch.next_slot].allocate();
}

VkDescriptorSet VulkanBackend::Impl::allocate_text_tile_raster_descriptor_set() {
    if (!frame_batch.active) {
        throw std::logic_error("text tile raster requires an active frame batch");
    }
    return frame_batch.text_tile_raster_allocators[frame_batch.next_slot].allocate();
}

void VulkanBackend::Impl::write_text_tile_bin_descriptors(
    VkDescriptorSet set, VkBuffer glyph_buffer, VkBuffer run_buffer,
    VkBuffer tile_counts, VkBuffer tile_indices) {
    const VkDescriptorBufferInfo glyph{glyph_buffer, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo runs{run_buffer, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo counts{tile_counts, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo indices{tile_indices, 0, VK_WHOLE_SIZE};
    const VkWriteDescriptorSet writes[] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &glyph, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &runs, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &counts, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 3, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &indices, nullptr}};
    vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
}

void VulkanBackend::Impl::write_text_tile_raster_descriptors(
    VkDescriptorSet set, const Image& destination, const Image& atlas,
    VkBuffer glyph_buffer, VkBuffer run_buffer,
    VkBuffer tile_counts, VkBuffer tile_indices) {
    const VkDescriptorImageInfo dst{
        VK_NULL_HANDLE, destination.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo atlas_info{
        VK_NULL_HANDLE, atlas.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorBufferInfo glyph{glyph_buffer, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo runs{run_buffer, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo counts{tile_counts, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo indices{tile_indices, 0, VK_WHOLE_SIZE};
    const VkWriteDescriptorSet writes[] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dst, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &atlas_info, nullptr, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &glyph, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 3, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &runs, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 4, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &counts, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 5, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &indices, nullptr}};
    vkUpdateDescriptorSets(device, 6, writes, 0, nullptr);
}

void VulkanBackend::Impl::emit_resource_transition(
    VkCommandBuffer command,
    VkImage image,
    const runtime::ResourceTransition& transition_record) {
    if (image == VK_NULL_HANDLE) {
        throw std::logic_error("Vulkan: ResourceTransition targets a null image");
    }

    VkImageSubresourceRange native_range{};
    if (const auto* range =
            std::get_if<runtime::SubresourceRange>(&transition_record.range)) {
        native_range = VkImageSubresourceRange{
            to_vk_aspects(range->aspects),
            range->first_mip,
            range->mip_count,
            range->first_layer,
            range->layer_count};
    } else if (std::holds_alternative<runtime::WholeResource>(
                   transition_record.range)) {
        native_range = VkImageSubresourceRange{
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    } else {
        throw std::logic_error(
            "Vulkan: buffer ResourceRange cannot be translated as image synchronization");
    }

    const VkImageMemoryBarrier2KHR barrier{
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR,
        nullptr,
        to_vk_stage(transition_record.before.stages),
        to_vk_access(transition_record.before.access),
        to_vk_stage(transition_record.after.stages),
        to_vk_access(transition_record.after.access),
        to_vk_layout(transition_record.before.layout),
        to_vk_layout(transition_record.after.layout),
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        image,
        native_range};
    const VkDependencyInfoKHR dependency{
        VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,
        nullptr,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier};
    ++stats.barriers_emitted;
    require_pipeline_barrier2(device)(command, &dependency);
}

void VulkanBackend::Impl::emit_plan_pass_transitions(
    VkCommandBuffer command,
    const runtime::CommandPlan& plan,
    std::size_t pass_index) {
    for (const auto& transition_record : plan.transitions) {
        if (transition_record.consumer_pass != pass_index) continue;
        if (transition_record.resource >= plan.resources.requests.size()) {
            throw std::logic_error(
                "Vulkan: ResourceTransition resource id is outside ResourcePlan");
        }
        const auto surface =
            plan.resources.requests[transition_record.resource].surface;
        if (surface == runtime::kInvalidRenderSurfaceHandle) {
            throw std::logic_error(
                "Vulkan: compiled ResourceTransition has no surface binding");
        }
        const auto binding = surfaces.surface_bindings.find(surface);
        if (binding == surfaces.surface_bindings.end()) {
            throw std::logic_error(
                "Vulkan: compiled ResourceTransition surface is not bound");
        }
        const auto physical = surfaces.physical_surfaces.find(binding->second);
        if (physical == surfaces.physical_surfaces.end()) {
            throw std::logic_error(
                "Vulkan: compiled ResourceTransition physical image is missing");
        }
        emit_resource_transition(command, physical->second.image.image,
                                 transition_record);
    }
}

void VulkanBackend::Impl::emit_unplanned_compute_sync(
    VkCommandBuffer command,
    std::initializer_list<const Image*> images) {
    for (const Image* image : images) {
        if (!image || image->image == VK_NULL_HANDLE) continue;
        runtime::ResourceTransition transition_record;
        transition_record.range =
            runtime::image_range(runtime::ResourceAspect::Color);
        transition_record.before = image->initialized
            ? runtime::ResourceState{
                  .stages = runtime::PipelineStage::ComputeShader,
                  .access = runtime::AccessMask::ShaderRead |
                            runtime::AccessMask::ShaderWrite,
                  .layout = runtime::ResourceLayout::General,
                  .queue = runtime::QueueClass::Compute,
              }
            : runtime::ResourceState::undefined_state();
        transition_record.after = runtime::ResourceState{
            .stages = runtime::PipelineStage::ComputeShader,
            .access = runtime::AccessMask::ShaderRead |
                      runtime::AccessMask::ShaderWrite,
            .layout = runtime::ResourceLayout::General,
            .queue = runtime::QueueClass::Compute,
        };
        emit_resource_transition(command, image->image, transition_record);
    }
}

void VulkanBackend::Impl::emit_pass_sync(
    VkCommandBuffer command,
    std::initializer_list<const Image*> images) {
    if (frame_batch.command_plan) {
        emit_plan_pass_transitions(command, *frame_batch.command_plan,
                                   frame_batch.pass_count);
        for (const Image* image : images) {
            if (!image) continue;
            bool unplanned = false;
            for (const auto handle : surfaces.unplanned_surface_handles) {
                const auto binding = surfaces.surface_bindings.find(handle);
                if (binding == surfaces.surface_bindings.end()) continue;
                const auto physical =
                    surfaces.physical_surfaces.find(binding->second);
                if (physical != surfaces.physical_surfaces.end() &&
                    &physical->second.image == image) {
                    unplanned = true;
                    break;
                }
            }
            if (unplanned) emit_unplanned_compute_sync(command, {image});
        }
        return;
    }
    emit_unplanned_compute_sync(command, images);
}

void VulkanBackend::Impl::emit_buffer_barriers2(
    VkCommandBuffer command,
    std::span<const VkBufferMemoryBarrier2KHR> barriers) {
    if (barriers.empty()) return;
    const VkDependencyInfoKHR dependency{
        VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,
        nullptr,
        0,
        0,
        nullptr,
        static_cast<std::uint32_t>(barriers.size()),
        barriers.data(),
        0,
        nullptr};
    stats.barriers_emitted += barriers.size();
    require_pipeline_barrier2(device)(command, &dependency);
}

void VulkanBackend::Impl::emit_command_batch_boundary() {
    for (auto& [slot, physical] : surfaces.physical_surfaces) {
        (void)slot;
        if (!physical.image.initialized ||
            physical.image.image == VK_NULL_HANDLE) {
            continue;
        }
        runtime::ResourceTransition transition_record;
        transition_record.range =
            runtime::image_range(runtime::ResourceAspect::Color);
        transition_record.before = runtime::ResourceState{
            .stages = runtime::PipelineStage::ComputeShader,
            .access = runtime::AccessMask::ShaderRead |
                      runtime::AccessMask::ShaderWrite,
            .layout = runtime::ResourceLayout::General,
            .queue = runtime::QueueClass::Compute,
        };
        transition_record.after = transition_record.before;
        emit_resource_transition(active_command_buffer(), physical.image.image,
                                 transition_record);
    }
}

void VulkanBackend::Impl::transition(VkCommandBuffer command,
                                     VkImage image,
                                     VkImageLayout old_layout,
                                     VkImageLayout new_layout) {
    runtime::ResourceTransition transition_record;
    transition_record.range =
        runtime::image_range(runtime::ResourceAspect::Color);
    transition_record.before = state_for_vk_layout(old_layout);
    transition_record.after = state_for_vk_layout(new_layout);
    transition_record.queue_ownership_transfer =
        !transition_record.before.undefined() &&
        transition_record.before.queue != transition_record.after.queue;
    emit_resource_transition(command, image, transition_record);
}

} // namespace chronon3d::backends::vulkan
