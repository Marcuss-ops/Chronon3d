// vulkan_backend_submission_private.cpp — VulkanBackend::Impl command buffer
// submission flow (begin, fence waits, timeline submit) and the legacy
// Framebuffer-based composite fallback.

#include "vulkan_backend_impl.hpp"
#include <cstring>
#include <vector>

namespace chronon3d::backends::vulkan {

void VulkanBackend::Impl::begin_command_buffer() {
    wait_for_pending();
    const VkCommandBufferBeginInfo begin{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
    check(vkResetCommandBuffer(command_buffer, 0), "vkResetCommandBuffer");
    check(vkBeginCommandBuffer(command_buffer, &begin), "vkBeginCommandBuffer");
}

void VulkanBackend::Impl::wait_for_pending() {
    if (pending_timeline_value != 0) {
        const auto wait_start = profiling::now();
        check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX),
              "vkWaitForFences");
        const auto wait_us = static_cast<std::uint64_t>(
            profiling::elapsed_us(wait_start));
        stats.gpu_wait_cpu_us += wait_us;
        ++stats.standalone_wait_count;
        stats.standalone_wait_us += wait_us;
        check(vkResetFences(device, 1, &fence), "vkResetFences");
        pending_timeline_value = 0;
    }
    for (std::size_t i = 0; i < FrameBatchState::kSlotCount; ++i) {
        if (!frame_batch.in_flight[i]) continue;
        const auto wait_start = profiling::now();
        const VkResult wait_result = vkWaitForFences(
            device, 1, &frame_batch.fences[i], VK_TRUE, UINT64_MAX);
        if (wait_result == VK_ERROR_DEVICE_LOST) {
            spdlog::error(
                "[vulkan] DEVICE LOST REPORT phase=wait_for_pending slot={} "
                "in_flight_slots=[{},{},{}] pending_timeline={}",
                i, frame_batch.in_flight[0], frame_batch.in_flight[1],
                frame_batch.in_flight[2], pending_timeline_value);
        }
        check(wait_result, "vkWaitForFences(frame batch slot)");
        const auto wait_us = static_cast<std::uint64_t>(
            profiling::elapsed_us(wait_start));
        stats.gpu_wait_cpu_us += wait_us;
        ++stats.frame_batch_drain_wait_count;
        stats.frame_batch_drain_wait_us += wait_us;
        check(vkResetFences(device, 1, &frame_batch.fences[i]),
              "vkResetFences(frame batch slot)");
        frame_batch.in_flight[i] = false;
        read_gpu_timestamps(i);
    }
    for (auto& slot : uploads.slots) wait_upload_slot(slot);
}

std::uint64_t VulkanBackend::Impl::submit(bool wait_for_completion) {
    check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");
    const auto signal_value = ++next_timeline_value;
    std::vector<VkSemaphore> wait_semaphores;
    std::vector<VkPipelineStageFlags> wait_stages;
    std::vector<VkSemaphore> signal_semaphores{timeline_semaphore};
    std::vector<std::uint64_t> signal_values{signal_value};
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    for (const auto physical_slot : surfaces.cuda_ready_surfaces) {
        const auto it = surfaces.physical_surfaces.find(physical_slot);
        if (it == surfaces.physical_surfaces.end()) continue;
        wait_semaphores.push_back(it->second.image.cuda_to_vulkan);
        wait_stages.push_back(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        signal_semaphores.push_back(it->second.image.vulkan_to_cuda);
        signal_values.push_back(0);
    }
    for (const auto physical_slot : surfaces.cuda_export_ready_surfaces) {
        if (surfaces.cuda_ready_surfaces.contains(physical_slot)) continue;
        const auto it = surfaces.physical_surfaces.find(physical_slot);
        if (it == surfaces.physical_surfaces.end()) continue;
        signal_semaphores.push_back(it->second.image.vulkan_to_cuda);
        signal_values.push_back(0);
    }
#endif
    const VkTimelineSemaphoreSubmitInfo timeline_submit{
        VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, nullptr,
        0, nullptr, static_cast<std::uint32_t>(signal_values.size()),
        signal_values.data()};
    const VkSubmitInfo submit_info{
        VK_STRUCTURE_TYPE_SUBMIT_INFO, &timeline_submit,
        static_cast<std::uint32_t>(wait_semaphores.size()),
        wait_semaphores.data(), wait_stages.data(), 1, &command_buffer,
        static_cast<std::uint32_t>(signal_semaphores.size()),
        signal_semaphores.data()};
    const auto submit_start = profiling::now();
    check(vkQueueSubmit(queue, 1, &submit_info, fence), "vkQueueSubmit");
    stats.gpu_submit_cpu_us += static_cast<std::uint64_t>(
        profiling::elapsed_us(submit_start));
    ++stats.submissions;
    pending_timeline_value = signal_value;
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    surfaces.cuda_ready_surfaces.clear();
    surfaces.cuda_export_ready_surfaces.clear();
#endif
    if (wait_for_completion) wait_for_pending();
    return signal_value;
}

void VulkanBackend::Impl::composite(Framebuffer& destination,
                                    const Framebuffer& source) {
    const auto width = static_cast<std::uint32_t>(destination.width());
    const auto height = static_cast<std::uint32_t>(destination.height());
    const VkDeviceSize image_bytes = static_cast<VkDeviceSize>(
        runtime::tight_surface_bytes(runtime::PixelFormat::Rgba32Float,
                                     width, height));
    ensure_images(width, height);
    ensure_staging(image_bytes * 3);

    std::vector<float> packed(static_cast<std::size_t>(width) * height * 8);
    auto pack = [&](const Framebuffer& framebuffer, std::size_t offset) {
        std::size_t index = offset / sizeof(float);
        for (int y = 0; y < framebuffer.height(); ++y) {
            for (int x = 0; x < framebuffer.width(); ++x) {
                const auto color = framebuffer.get_pixel(x, y);
                packed[index++] = color.r;
                packed[index++] = color.g;
                packed[index++] = color.b;
                packed[index++] = color.a;
            }
        }
    };
    pack(source, 0);
    pack(destination, static_cast<std::size_t>(image_bytes));
    std::memcpy(staging.mapped, packed.data(),
                static_cast<std::size_t>(image_bytes * 2));

    const VkCommandBufferBeginInfo begin{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
    check(vkResetCommandBuffer(command_buffer, 0), "vkResetCommandBuffer");
    check(vkBeginCommandBuffer(command_buffer, &begin), "vkBeginCommandBuffer");
    transition(command_buffer, src.image,
               src.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    transition(command_buffer, dst.image,
               dst.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    const VkBufferImageCopy source_copy{
        0, width, height,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        {0, 0, 0}, {width, height, 1}};
    const VkBufferImageCopy destination_copy{
        image_bytes, width, height,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        {0, 0, 0}, {width, height, 1}};
    vkCmdCopyBufferToImage(command_buffer, staging.buffer, src.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &source_copy);
    vkCmdCopyBufferToImage(command_buffer, staging.buffer, dst.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &destination_copy);
    transition(command_buffer, src.image,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               VK_IMAGE_LAYOUT_GENERAL);
    transition(command_buffer, dst.image,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               VK_IMAGE_LAYOUT_GENERAL);
    vkCmdBindPipeline(
        command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
        reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::Composite)));
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            kernels.general_layout, 0, 1,
                            &descriptor_set, 0, nullptr);
    vkCmdDispatch(command_buffer, (width + 15) / 16, (height + 15) / 16, 1);
    transition(command_buffer, dst.image, VK_IMAGE_LAYOUT_GENERAL,
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    const VkBufferImageCopy output_copy{
        image_bytes * 2, width, height,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        {0, 0, 0}, {width, height, 1}};
    vkCmdCopyImageToBuffer(command_buffer, dst.image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           staging.buffer, 1, &output_copy);
    transition(command_buffer, dst.image,
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               VK_IMAGE_LAYOUT_GENERAL);
    src.initialized = true;
    dst.initialized = true;
    submit();

    const float* output = static_cast<const float*>(staging.mapped) +
        (image_bytes * 2 / sizeof(float));
    for (int y = 0; y < destination.height(); ++y) {
        for (int x = 0; x < destination.width(); ++x) {
            const std::size_t index =
                (static_cast<std::size_t>(y) * width + x) * 4;
            destination.set_pixel(
                x, y,
                Color{output[index], output[index + 1],
                      output[index + 2], output[index + 3]});
        }
    }
}

} // namespace chronon3d::backends::vulkan
