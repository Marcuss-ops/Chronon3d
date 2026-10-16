// vulkan_backend_sync_private.cpp — VulkanBackend::Impl synchronization:
// canonical ResourceState -> Vulkan Sync2 translation and command-stream
// barrier emission.

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
#if defined(VK_KHR_video_decode_queue)
    consume(runtime::PipelineStage::VideoDecode,
            VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR);
#endif
#if defined(VK_KHR_video_encode_queue)
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
#if defined(VK_KHR_video_decode_queue)
    consume(runtime::AccessMask::VideoDecodeRead,
            VK_ACCESS_2_VIDEO_DECODE_READ_BIT_KHR);
    consume(runtime::AccessMask::VideoDecodeWrite,
            VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR);
#endif
#if defined(VK_KHR_video_encode_queue)
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
#if defined(VK_KHR_video_decode_queue)
        return VK_IMAGE_LAYOUT_VIDEO_DECODE_SRC_KHR;
#else
        break;
#endif
    case runtime::ResourceLayout::VideoDecodeDst:
#if defined(VK_KHR_video_decode_queue)
        return VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR;
#else
        break;
#endif
    case runtime::ResourceLayout::VideoEncodeSrc:
#if defined(VK_KHR_video_encode_queue)
        return VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR;
#else
        break;
#endif
    case runtime::ResourceLayout::VideoEncodeDst:
#if defined(VK_KHR_video_encode_queue)
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
#if defined(VK_VERSION_1_1) || defined(VK_KHR_sampler_ycbcr_conversion)
    consume(runtime::ResourceAspect::Plane0, VK_IMAGE_ASPECT_PLANE_0_BIT);
    consume(runtime::ResourceAspect::Plane1, VK_IMAGE_ASPECT_PLANE_1_BIT);
    consume(runtime::ResourceAspect::Plane2, VK_IMAGE_ASPECT_PLANE_2_BIT);
#endif
    if (remaining != 0 || result == 0) {
        throw std::logic_error("Vulkan: unsupported canonical ResourceAspect");
    }
    return result;
}

std::uint32_t to_vk_queue_family(runtime::QueueClass queue,
                                 std::uint32_t internal_family) noexcept {
    // Chronon currently owns one Vulkan queue family for all internal queue
    // classes. External ownership is the only native family boundary; logical
    // internal queue-class changes therefore need visibility, not ownership.
    return queue == runtime::QueueClass::External
        ? VK_QUEUE_FAMILY_EXTERNAL
        : internal_family;
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

    std::uint32_t source_family = VK_QUEUE_FAMILY_IGNORED;
    std::uint32_t destination_family = VK_QUEUE_FAMILY_IGNORED;
    if (transition_record.queue_ownership_transfer &&
        !transition_record.before.undefined()) {
        const auto before_family =
            to_vk_queue_family(transition_record.before.queue, queue_family);
        const auto after_family =
            to_vk_queue_family(transition_record.after.queue, queue_family);
        if (before_family != after_family) {
            source_family = before_family;
            destination_family = after_family;
        }
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
        source_family,
        destination_family,
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
        // Audit N4: resolve through the recording lease. The compiled surface
        // set is leased for the whole plan batch, so the transition stream no
        // longer reads the surface store while the submission ring is driven.
        const auto& physical = resolve_image(surface);
        runtime::ResourceTransition adjusted = transition_record;
        // A surface already written outside the compiled plan (for example a
        // CPU fallback upload) really is in GENERAL. Declaring UNDEFINED here
        // lets the driver discard those pixels before the pass reads them.
        if (physical.initialized && adjusted.before.undefined()) {
            adjusted.before = runtime::ResourceState{
                .stages = runtime::PipelineStage::ComputeShader,
                .access = runtime::AccessMask::ShaderRead |
                          runtime::AccessMask::ShaderWrite,
                .layout = runtime::ResourceLayout::General,
                .queue = runtime::QueueClass::Compute,
            };
        }
        emit_resource_transition(command, physical.image, adjusted);
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
        // Compiled execution never falls back to the direct/unplanned state
        // path. The canonical transition stream is the only synchronization
        // authority for the pass, including imported/external resources.
        if (timestamp_pool != VK_NULL_HANDLE && !command_batch_active) {
            const auto queries_per_slot = static_cast<std::uint32_t>(
                2 + 2 * FrameBatchState::kCompiledPassTimingCapacity);
            const auto slot_base =
                static_cast<std::uint32_t>(frame_batch.next_slot) * queries_per_slot;
            const auto pass_query = slot_base + 2 + static_cast<std::uint32_t>(
                2 * frame_batch.pass_count);
            if (frame_batch.pass_count != 0) {
                vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                    timestamp_pool, pass_query - 1);
            }
            vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                timestamp_pool, pass_query);
        }
        emit_plan_pass_transitions(command, *frame_batch.command_plan,
                                   frame_batch.pass_count);
        // Asset textures promoted outside the compiled graph (for example
        // ImageCache/GpuAssetCache sources) are not present in the plan's
        // resource table. Keep their upload-to-compute dependency explicit;
        // otherwise a native image pass may legally execute while the
        // imported storage image still has no compute visibility edge.
        emit_unplanned_compute_sync(command, images);
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

void VulkanBackend::Impl::emit_cuda_surface_ownership_transfer(
    VkCommandBuffer command, std::size_t physical_slot, bool acquire) {
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    const auto it = surfaces.physical_surfaces.find(physical_slot);
    if (it == surfaces.physical_surfaces.end() ||
        !it->second.image.exportable ||
        it->second.image.image == VK_NULL_HANDLE) {
        throw std::logic_error(
            "CUDA surface ownership transfer references a non-exportable image");
    }
    const auto& image = it->second.image;
    const VkImageMemoryBarrier2KHR barrier{
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR,
        nullptr,
        acquire ? VK_PIPELINE_STAGE_2_NONE_KHR
                : (VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR |
                   VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR),
        acquire ? 0
                : (VK_ACCESS_2_SHADER_READ_BIT_KHR |
                   VK_ACCESS_2_SHADER_WRITE_BIT_KHR |
                   VK_ACCESS_2_TRANSFER_READ_BIT_KHR |
                   VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR),
        acquire ? (VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR |
                   VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR)
                : VK_PIPELINE_STAGE_2_NONE_KHR,
        acquire ? (VK_ACCESS_2_SHADER_READ_BIT_KHR |
                   VK_ACCESS_2_SHADER_WRITE_BIT_KHR |
                   VK_ACCESS_2_TRANSFER_READ_BIT_KHR |
                   VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR)
                : 0,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_GENERAL,
        acquire ? VK_QUEUE_FAMILY_EXTERNAL : queue_family,
        acquire ? queue_family : VK_QUEUE_FAMILY_EXTERNAL,
        image.image,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    const VkDependencyInfoKHR dependency{
        VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR, nullptr, 0, 0, nullptr,
        0, nullptr, 1, &barrier};
    require_pipeline_barrier2(device)(command, &dependency);
    ++stats.barriers_emitted;
#else
    (void)command;
    (void)physical_slot;
    (void)acquire;
#endif
}

void VulkanBackend::Impl::acquire_cuda_surface_for_recording(
    VkCommandBuffer command, std::size_t physical_slot) {
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    if (surfaces.acquire_pending_cuda_surface(physical_slot)) {
        emit_cuda_surface_ownership_transfer(command, physical_slot, true);
    }
#else
    (void)command;
    (void)physical_slot;
#endif
}

void VulkanBackend::Impl::emit_cuda_surface_releases(VkCommandBuffer command) {
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    for (const auto physical_slot : surfaces.cuda_ready_surfaces) {
        emit_cuda_surface_ownership_transfer(command, physical_slot, false);
    }
    for (const auto physical_slot : surfaces.cuda_export_ready_surfaces) {
        if (surfaces.cuda_ready_surfaces.contains(physical_slot)) continue;
        emit_cuda_surface_ownership_transfer(command, physical_slot, false);
    }
#else
    (void)command;
#endif
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
