// vulkan_backend_submission_private.cpp — VulkanBackend::Impl command buffer
// submission flow (begin, fence waits, timeline submit) and the legacy
// Framebuffer-based composite fallback.

#include "vulkan_backend_impl.hpp"
#include <cstring>
#include <vector>

namespace chronon3d::backends::vulkan {

void VulkanBackend::Impl::wait_for_standalone() {
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
}

void VulkanBackend::Impl::begin_command_buffer() {
    wait_for_standalone();
    const VkCommandBufferBeginInfo begin{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
    check(vkResetCommandBuffer(command_buffer, 0), "vkResetCommandBuffer");
    check(vkBeginCommandBuffer(command_buffer, &begin), "vkBeginCommandBuffer");
}

void VulkanBackend::Impl::wait_for_pending() {
    wait_for_standalone();
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
    // HOT-PATH TAX (TICKET-HOTPATH-ZERO-ALLOC-P1): the four vectors below
    // were rebuilt per submit (per frame). They now reuse the Impl-member
    // scratch (submit_wait_semaphores_ etc.), which is safe because Impl
    // methods are serialized on the single submission thread exactly like
    // transient_release_scratch_. Capacity is retained across frames.
    std::vector<VkSemaphore>& wait_semaphores = submit_wait_semaphores_;
    std::vector<VkPipelineStageFlags>& wait_stages = submit_wait_stages_;
    std::vector<VkSemaphore>& signal_semaphores = submit_signal_semaphores_;
    std::vector<std::uint64_t>& signal_values = submit_signal_values_;
    wait_semaphores.clear();
    wait_stages.clear();
    signal_semaphores.clear();
    signal_values.clear();
    signal_semaphores.push_back(timeline_semaphore);
    signal_values.push_back(signal_value);
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
} // namespace chronon3d::backends::vulkan
