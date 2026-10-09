// vulkan_kernel_replay_private.cpp — VulkanBackend::Impl compiled-pass
// timing, replay-slot recording and batched submission.
// Out-of-class definitions; declared in vulkan_backend_impl.hpp.
// Per-operation record_* primitives live in vulkan_kernel_record_private.cpp.

#include "vulkan_backend_impl.hpp"

#include <cstdlib>

namespace chronon3d::backends::vulkan {

// Resolve timestamp queries only after the slot fence has signaled. No
// query-result wait flag is needed here: fence completion is the sole
// synchronization point for both aggregate and compiled per-pass timing.
void VulkanBackend::Impl::read_gpu_timestamps(std::size_t slot) {
        if (timestamp_pool == VK_NULL_HANDLE || slot >= FrameBatchState::kSlotCount) {
            return;
        }
        const auto queries_per_slot = static_cast<std::uint32_t>(
            2 + 2 * FrameBatchState::kCompiledPassTimingCapacity);
        const auto slot_base = static_cast<std::uint32_t>(slot) * queries_per_slot;
        const auto pass_count = frame_batch.submitted_pass_counts[slot];
        frame_batch.submitted_pass_counts[slot] = 0;
        const bool frame_valid = frame_batch.submitted_frame_valid[slot];
        const auto frame_index = frame_batch.submitted_frame_indices[slot];
        frame_batch.submitted_frame_valid[slot] = false;

        const auto gpu_to_cpu = [this](std::uint64_t gpu) {
            return calibration_cpu_trace_ns +
                static_cast<std::int64_t>(
                    static_cast<double>(gpu - calibration_gpu_ts) *
                    timestamp_period_ns);
        };

        const auto publish_timing = [&](double execute_ms,
                                        std::uint64_t start,
                                        std::uint64_t end,
                                        bool timestamp_valid) {
            if (!frame_valid) return;
            VulkanFrameGpuTiming timing;
            timing.frame_index = frame_index;
            timing.frame_batch_slot = static_cast<std::uint32_t>(slot);
            timing.timestamp_valid = timestamp_valid;
            timing.execute_ms = execute_ms;
            if (timestamp_valid && end >= start) {
                timing.device_span_ms = static_cast<double>(end - start) *
                    timestamp_period_ns / 1'000'000.0;
                if (gpu_timestamps_calibrated &&
                    start >= calibration_gpu_ts && end >= start) {
                    timing.device_start_trace_ns = gpu_to_cpu(start);
                    timing.device_end_trace_ns = gpu_to_cpu(end);
                    const auto submit_end =
                        frame_batch.submitted_submit_end_trace_ns[slot];
                    if (submit_end > 0 &&
                        timing.device_start_trace_ns > submit_end) {
                        timing.queue_wait_ms = static_cast<double>(
                            timing.device_start_trace_ns - submit_end) /
                            1'000'000.0;
                    }
                }
            }
            completed_frame_timings.push_back(timing);
        };

        if (pass_count != 0) {
            std::vector<std::uint64_t> stamps(2 * pass_count, 0);
            const auto query_count = static_cast<std::uint32_t>(2 * pass_count);
            const VkResult result = vkGetQueryPoolResults(
                device, timestamp_pool, slot_base + 2, query_count,
                stamps.size() * sizeof(std::uint64_t), stamps.data(),
                sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT);
            if (result != VK_SUCCESS) {
                publish_timing(0.0, 0, 0, false);
                return;
            }

            std::uint64_t first_start = 0;
            std::uint64_t last_end = 0;
            bool have_range = false;
            double execute_ms = 0.0;
            for (std::size_t pass = 0; pass < pass_count; ++pass) {
                const auto start = stamps[2 * pass];
                const auto end = stamps[2 * pass + 1];
                if (end < start) continue;
                const double elapsed_ns =
                    static_cast<double>(end - start) * timestamp_period_ns;
                execute_ms += elapsed_ns / 1'000'000.0;
                stats.gpu_execute_us +=
                    static_cast<std::uint64_t>(elapsed_ns / 1000.0);
                if (!have_range) {
                    first_start = start;
                    have_range = true;
                }
                last_end = end;
                if (gpu_timestamps_calibrated && start >= calibration_gpu_ts &&
                    end > start) {
                    const auto start_ns = gpu_to_cpu(start);
                    const auto end_ns = gpu_to_cpu(end);
                    if (end_ns > start_ns) {
                        CHRONON_TRACE_GPU_BEGIN("VulkanCompiledPass", start_ns);
                        CHRONON_TRACE_GPU_END(end_ns);
                    }
                }
            }
            publish_timing(execute_ms, first_start, last_end, have_range);
            return;
        }

        std::uint64_t stamps[2] = {0, 0};
        const VkResult result = vkGetQueryPoolResults(
            device, timestamp_pool, slot_base, 2,
            sizeof(stamps), stamps, sizeof(std::uint64_t),
            VK_QUERY_RESULT_64_BIT);
        if (result != VK_SUCCESS) {
            publish_timing(0.0, 0, 0, false);
            return;
        }
        const bool valid = stamps[1] >= stamps[0];
        const double execute_ms = valid
            ? static_cast<double>(stamps[1] - stamps[0]) * timestamp_period_ns /
                  1'000'000.0
            : 0.0;
        if (stamps[1] >= stamps[0]) {
            const double elapsed_ns =
                static_cast<double>(stamps[1] - stamps[0]) * timestamp_period_ns;
            stats.gpu_execute_us += static_cast<std::uint64_t>(elapsed_ns / 1000.0);
        }
        if (gpu_timestamps_calibrated && stamps[0] >= calibration_gpu_ts &&
            stamps[1] > stamps[0]) {
            const auto start_ns = gpu_to_cpu(stamps[0]);
            const auto end_ns = gpu_to_cpu(stamps[1]);
            if (end_ns > start_ns) {
                CHRONON_TRACE_GPU_BEGIN("VulkanExecute", start_ns);
                CHRONON_TRACE_GPU_END(end_ns);
            }
        }
        publish_timing(execute_ms, stamps[0], stamps[1], valid);
    }

    // End the active frame batch's command buffer and submit it exactly once
    // with the current slot's fence. The completed slot records whether its
    // query range contains aggregate timing or canonical per-pass timings.
    void VulkanBackend::Impl::submit_batch() {
        const auto slot = frame_batch.next_slot;
        if (timestamp_pool != VK_NULL_HANDLE) {
            const auto queries_per_slot = static_cast<std::uint32_t>(
                2 + 2 * FrameBatchState::kCompiledPassTimingCapacity);
            const auto slot_base = static_cast<std::uint32_t>(slot) * queries_per_slot;
            const bool compiled_per_pass =
                frame_batch.command_plan != nullptr &&
                !command_batch_active &&
                frame_batch.pass_count != 0;
            if (compiled_per_pass) {
                const auto final_query = slot_base + 2 +
                    static_cast<std::uint32_t>(2 * frame_batch.pass_count - 1);
                vkCmdWriteTimestamp(frame_batch.command_buffers[slot],
                                    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                    timestamp_pool, final_query);
                frame_batch.submitted_pass_counts[slot] = frame_batch.pass_count;
            } else {
                vkCmdWriteTimestamp(frame_batch.command_buffers[slot],
                                    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                    timestamp_pool, slot_base + 1);
                frame_batch.submitted_pass_counts[slot] = 0;
            }
        } else {
            frame_batch.submitted_pass_counts[slot] = 0;
        }
        emit_cuda_surface_releases(frame_batch.command_buffers[slot]);
        check(vkEndCommandBuffer(frame_batch.command_buffers[slot]),
              "vkEndCommandBuffer(frame batch)");
        const auto signal_value = ++next_timeline_value;
        // Reuse the Impl-owned submission scratch just like submit(). The
        // frame-batch and command-replay paths are serialized on the single
        // Vulkan submission owner, so this retains capacity without creating
        // vectors in the steady-state frame loop.
        auto& wait_semaphores = submit_wait_semaphores_;
        auto& wait_stages = submit_wait_stages_;
        auto& wait_values = submit_wait_values_;
        auto& signal_semaphores = submit_signal_semaphores_;
        auto& signal_values = submit_signal_values_;
        wait_semaphores.clear();
        wait_stages.clear();
        wait_values.clear();
        signal_semaphores.clear();
        signal_values.clear();
        signal_semaphores.push_back(timeline_semaphore);
        signal_values.push_back(signal_value);
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
        for (const auto physical_slot : surfaces.cuda_ready_surfaces) {
            const auto it = surfaces.physical_surfaces.find(physical_slot);
            if (it == surfaces.physical_surfaces.end()) continue;
            auto& image = it->second.image;
            wait_semaphores.push_back(image.cuda_to_vulkan);
            // Gate transfer clears/copies as well as compute on the CUDA
            // ownership token before rewriting an imported encoder surface.
            wait_stages.push_back(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
            wait_values.push_back(image.cuda_timeline
                ? image.interop_generation->cuda_to_vulkan() : 0);
            signal_semaphores.push_back(image.vulkan_to_cuda);
            const auto value = image.cuda_timeline
                ? image.interop_generation->reserve_vulkan_to_cuda_signal() : 0;
            signal_values.push_back(value);
            image.last_vulkan_to_cuda_value = value;
            image.cuda_export_fence_slot = slot;
            image.cuda_export_standalone_fence = false;
            if (std::getenv("CHRONON3D_CUDA_INTEROP_DIAG")) {
                spdlog::info("[cuda-interop-diag] frame={} slot={} surface={} lane=decode c2v_wait={} v2c_signal={} timeline={}",
                    active_frame_index, slot, physical_slot,
                    image.cuda_timeline ? image.interop_generation->cuda_to_vulkan() : 0,
                    value, image.cuda_timeline);
            }
        }
        for (const auto physical_slot : surfaces.cuda_export_ready_surfaces) {
            // A reused surface is already waiting on CUDA completion above;
            // that path also emits the single Vulkan->CUDA release signal.
            // Do not signal the same binary semaphore twice in one submit.
            if (surfaces.cuda_ready_surfaces.contains(physical_slot)) continue;
            const auto it = surfaces.physical_surfaces.find(physical_slot);
            if (it == surfaces.physical_surfaces.end()) continue;
            auto& image = it->second.image;
            signal_semaphores.push_back(image.vulkan_to_cuda);
            const auto value = image.cuda_timeline
                ? image.interop_generation->reserve_vulkan_to_cuda_signal() : 0;
            signal_values.push_back(value);
            image.last_vulkan_to_cuda_value = value;
            image.cuda_export_fence_slot = slot;
            image.cuda_export_standalone_fence = false;
            if (std::getenv("CHRONON3D_CUDA_INTEROP_DIAG")) {
                spdlog::info("[cuda-interop-diag] frame={} slot={} surface={} lane=encode v2c_signal={} timeline={}",
                    active_frame_index, slot, physical_slot, value,
                    image.cuda_timeline);
            }
        }
#endif
        const VkTimelineSemaphoreSubmitInfo timeline_submit{
            VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, nullptr,
            static_cast<std::uint32_t>(wait_values.size()), wait_values.data(),
            static_cast<std::uint32_t>(signal_values.size()),
            signal_values.data()};
        const VkSubmitInfo submit_info{
            VK_STRUCTURE_TYPE_SUBMIT_INFO, &timeline_submit,
            static_cast<std::uint32_t>(wait_semaphores.size()),
            wait_semaphores.data(), wait_stages.data(),
            1, &frame_batch.command_buffers[slot],
            static_cast<std::uint32_t>(signal_semaphores.size()),
            signal_semaphores.data()};
        const auto submit_start = profiling::now();
        CHRONON_TRACE_SCOPE("chronon.gpu", "VulkanSubmit");
        check(vkQueueSubmit(queue, 1, &submit_info, frame_batch.fences[slot]),
              "vkQueueSubmit(frame batch)");
        stats.gpu_submit_cpu_us += static_cast<std::uint64_t>(profiling::elapsed_us(submit_start));
        ++stats.submissions;
        frame_batch.submitted_frame_indices[slot] = active_frame_index;
        frame_batch.submitted_frame_valid[slot] = true;
        frame_batch.submitted_submit_end_trace_ns[slot] =
            chronon3d::tracing::TraceTimeNs();
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
        const auto mark_frame_export = [this, slot](const std::uint64_t physical_slot) {
            const auto it = surfaces.physical_surfaces.find(physical_slot);
            if (it == surfaces.physical_surfaces.end()) return;
            it->second.image.cuda_export_fence_slot = slot;
            it->second.image.cuda_export_standalone_fence = false;
        };
        for (const auto physical_slot : surfaces.cuda_ready_surfaces)
            mark_frame_export(physical_slot);
        for (const auto physical_slot : surfaces.cuda_export_ready_surfaces)
            if (!surfaces.cuda_ready_surfaces.contains(physical_slot))
                mark_frame_export(physical_slot);
        // Binary CUDA interop used to wait for this fence immediately, which
        // serialized every frame behind CUDA/NVENC.  Reuse is already gated
        // by the next submission's Vulkan wait on C2V; the ring-slot fence is
        // waited only when that slot comes around in begin_frame_batch().
        // Keeping the fence in flight preserves the six-slot pipeline while
        // retaining the same ownership order for the exported surface.
        frame_batch.in_flight[slot] = true;
#else
        frame_batch.in_flight[slot] = true;
#endif
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
        surfaces.cuda_ready_surfaces.clear();
        surfaces.cuda_export_ready_surfaces.clear();
#endif
        frame_batch.pass_count = 0;
        frame_batch.next_slot = (slot + 1) % FrameBatchState::kSlotCount;
    }

    // ── Command-replay: record once, submit with param writes ────────
    //
    // prepare() calls begin_replay_recording() / end_replay_recording()
    // for each pre-planned pass to bake a VkCommandBuffer.  At frame time
    // replay_submit() writes the per-frame params into the slot's mapped
    // buffer and submits the pre-recorded command buffer — zero vkCmd*
    // calls in the render loop.
    //
    // The params buffer is a simple flat allocation; the caller is
    // responsible for the layout (typically a struct matching the shader's
    // uniform block).  Capacity grows on demand but never shrinks.

    void VulkanBackend::Impl::ensure_replay_params_capacity(ReplaySlot& slot, VkDeviceSize bytes) {
        if (slot.params.size >= bytes) return;
        if (slot.params.buffer != VK_NULL_HANDLE) {
            memory_manager.destroy_buffer(slot.params);
        }
        const VkBufferCreateInfo buffer_info{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0,
            bytes, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
        slot.params = memory_manager.create_buffer(buffer_info, VulkanMemoryClass::HostUpload);
        if (debug_context) debug_context->set_buffer_name(slot.params.buffer, "Chronon3D.Buffer.ReplayParams");
    }

    /// Open the replay slot's command buffer.  The caller records all commands
    /// for one frame into the returned command buffer, then calls
    /// end_replay_recording().  Must not be called while a frame batch
    /// or another replay recording is active.
    VkCommandBuffer VulkanBackend::Impl::begin_replay_recording(std::size_t slot_index) {
        if (slot_index >= kReplaySlotCount) {
            throw std::out_of_range("begin_replay_recording: slot out of range");
        }
        auto& slot = replay_slots[slot_index];
        if (slot.command_buffer == VK_NULL_HANDLE) {
            const VkCommandBufferAllocateInfo alloc_info{
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr,
                command_pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
            check(vkAllocateCommandBuffers(device, &alloc_info,
                                           &slot.command_buffer),
                  "vkAllocateCommandBuffers(replay slot)");
            if (debug_context) debug_context->set_command_buffer_name(slot.command_buffer, "Chronon3D.CommandBuffer.ReplaySlot");
        }
        if (slot.fence == VK_NULL_HANDLE) {
            const VkFenceCreateInfo fence_info{
                VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0};
            check(vkCreateFence(device, &fence_info, nullptr, &slot.fence),
                  "vkCreateFence(replay slot)");
            if (debug_context) debug_context->set_fence_name(slot.fence, "Chronon3D.Fence.ReplaySlot");
        }
        const VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
            VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT, nullptr};
        check(vkBeginCommandBuffer(slot.command_buffer, &begin),
              "vkBeginCommandBuffer(replay slot)");
        return slot.command_buffer;
    }

    /// Close the replay slot's command buffer.  After this call the slot
    /// holds a pre-recorded, reusable VkCommandBuffer.
    void VulkanBackend::Impl::end_replay_recording(std::size_t slot_index) {
        if (slot_index >= kReplaySlotCount) {
            throw std::out_of_range("end_replay_recording: slot out of range");
        }
        auto& slot = replay_slots[slot_index];
        check(vkEndCommandBuffer(slot.command_buffer),
              "vkEndCommandBuffer(replay slot)");
    }

    /// Submit a pre-recorded replay slot with the given per-frame params.
    /// Waits on the slot's fence if it's still in flight (same ring-depth
    /// as the frame batch), writes `params` into the mapped buffer, then
    /// calls vkQueueSubmit exactly once.
    void VulkanBackend::Impl::replay_submit(std::size_t slot_index,
                       const void* params, VkDeviceSize params_size) {
        if (slot_index >= kReplaySlotCount) {
            throw std::out_of_range("replay_submit: slot out of range");
        }
        auto& slot = replay_slots[slot_index];
        // Wait for previous frame using this slot.
        if (slot.in_flight) {
            CHRONON_TRACE_SCOPE("chronon.gpu", "ReplayFenceWait");
            check(vkWaitForFences(device, 1, &slot.fence, VK_TRUE, UINT64_MAX),
                  "vkWaitForFences(replay slot)");
            check(vkResetFences(device, 1, &slot.fence),
                  "vkResetFences(replay slot)");
            slot.in_flight = false;
        }
        // Write per-frame params into the persistently-mapped buffer.
        if (params && params_size > 0) {
            ensure_replay_params_capacity(slot, params_size);
            std::memcpy(slot.params.mapped, params,
                        static_cast<std::size_t>(params_size));
        }
        // Single submit of the pre-recorded command buffer.
        const auto signal_value = ++next_timeline_value;
        const VkTimelineSemaphoreSubmitInfo timeline_submit{
            VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, nullptr,
            0, nullptr, 1, &signal_value};
        const VkSubmitInfo submit_info{
            VK_STRUCTURE_TYPE_SUBMIT_INFO, &timeline_submit,
            0, nullptr, nullptr,
            1, &slot.command_buffer,
            1, &timeline_semaphore};
        const auto submit_start = profiling::now();
        CHRONON_TRACE_SCOPE("chronon.gpu", "ReplaySubmit");
        check(vkQueueSubmit(queue, 1, &submit_info, slot.fence),
              "vkQueueSubmit(replay slot)");
        stats.gpu_submit_cpu_us +=
            static_cast<std::uint64_t>(profiling::elapsed_us(submit_start));
        ++stats.submissions;
        slot.in_flight = true;
    }

} // namespace chronon3d::backends::vulkan
