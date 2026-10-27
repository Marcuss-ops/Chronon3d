// vulkan_backend_lifecycle_private.cpp — VulkanBackend::Impl device
// lifetime: construction, persistent pipeline cache and destruction.
// Out-of-class definitions; declared in vulkan_backend_impl.hpp.
// Descriptor layouts live in vulkan_backend_lifecycle_layouts_private.cpp,
// kernel pipelines in vulkan_backend_lifecycle_pipelines_private.cpp and
// vulkan_backend_lifecycle_pipelines_draw_private.cpp.

#include "vulkan_backend_impl.hpp"
#include "vulkan_pipeline_cache_io.hpp"
#include "composite_comp_spv.hpp"
#include "transform_comp_spv.hpp"
#include "affine_transform_comp_spv.hpp"
#include "blur_comp_spv.hpp"
#include "directional_blur_comp_spv.hpp"
#include "dof_blur_comp_spv.hpp"
#include "dof_depth_stamp_comp_spv.hpp"
#include "radial_blur_comp_spv.hpp"
#include "warp_bulk_comp_spv.hpp"
#include "generate_bulk_comp_spv.hpp"
#include "lut_bulk_comp_spv.hpp"
#include "sharpen_comp_spv.hpp"
#include "bloom_threshold_comp_spv.hpp"
#include "glow_prepare_comp_spv.hpp"
#include "color_adjust_comp_spv.hpp"
#include "color_matrix_comp_spv.hpp"
#include "chromatic_aberration_comp_spv.hpp"
#include "lighting_comp_spv.hpp"
#include "matte_comp_spv.hpp"
#include "text_run_comp_spv.hpp"
#include "fill_rect_comp_spv.hpp"
#include "layer_batch_comp_spv.hpp"
#include "text_tile_bin_comp_spv.hpp"
#include "text_tile_raster_comp_spv.hpp"
#include <fstream>


namespace chronon3d::backends::vulkan {

    VulkanBackend::Impl::Impl(VkInstance inst, VkPhysicalDevice physical, VkDevice logical, VkQueue graphics,
         std::uint32_t family, VkCommandPool pool,
         bool calibrated_timestamps_supported,
         VulkanDebugContext* dbg_ctx)
        : instance(inst), physical_device(physical), device(logical), queue(graphics),
          queue_family(family), command_pool(pool),
          submission_ring(descriptor_arena),
          frame_batch(submission_ring),
          calibrated_ts_supported(calibrated_timestamps_supported),
          debug_context(dbg_ctx) {
        surfaces.owner_ = this;
        // Retain telemetry capacity across frame retirements and jobs so the
        // completion record itself does not become a per-frame allocator.
        completed_frame_timings.reserve(64);
        memory_manager.initialize(instance, physical_device, device);
        VkPhysicalDeviceProperties device_properties{};
        vkGetPhysicalDeviceProperties(physical_device, &device_properties);
        stats.device_name = device_properties.deviceName;
        stats.discrete_gpu =
            device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        timestamp_period_ns = device_properties.limits.timestampPeriod;
        // timestampValidBits is a property of the selected queue family, not
        // VkPhysicalDeviceLimits.  Read it from the same family used for the
        // graphics queue so timestamp queries are enabled only when that
        // queue can actually report them.
        std::uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, families.data());
        if (queue_family < families.size()) {
            timestamp_valid_bits = families[queue_family].timestampValidBits;
        }

        init_descriptor_layouts();

        // ── Persistent VkPipelineCache on disk ────────────────────
        // Key the driver hint by device/driver/UUID and actual embedded SPIR-V
        // bytes (including shader ABI). Disk failure must not prevent rendering.
        std::uint64_t shader_hash = 14695981039346656037ULL;
        const auto hash_shader = [&](const void* data, std::size_t size) {
            const auto* bytes = static_cast<const unsigned char*>(data);
            for (std::size_t i = 0; i < size; ++i)
                shader_hash = (shader_hash ^ bytes[i]) * 1099511628211ULL;
        };
#define CHRONON_HASH_SHADER(name) hash_shader(shaders::chronon3d_##name##_comp_spv, shaders::chronon3d_##name##_comp_spv_size)
        CHRONON_HASH_SHADER(composite);
        CHRONON_HASH_SHADER(transform);
        CHRONON_HASH_SHADER(affine_transform);
        CHRONON_HASH_SHADER(blur);
        CHRONON_HASH_SHADER(sharpen);
        CHRONON_HASH_SHADER(bloom_threshold);
        CHRONON_HASH_SHADER(color_adjust);
        CHRONON_HASH_SHADER(color_matrix);
        CHRONON_HASH_SHADER(chromatic_aberration);
        CHRONON_HASH_SHADER(lighting);
        CHRONON_HASH_SHADER(matte);
        CHRONON_HASH_SHADER(text_run);
        CHRONON_HASH_SHADER(fill_rect);
        CHRONON_HASH_SHADER(layer_batch);
        CHRONON_HASH_SHADER(text_tile_bin);
        CHRONON_HASH_SHADER(text_tile_raster);
#undef CHRONON_HASH_SHADER
        std::filesystem::path cache_file;
        std::vector<char> cache_data;
        try {
            const auto cache_dir = std::filesystem::path(
                std::getenv("HOME") ? std::getenv("HOME") : "/tmp") / ".cache" / "chronon";
            cache_file = cache_dir / pipeline_cache_io::filename(device_properties, shader_hash);
            cache_data = pipeline_cache_io::read(cache_file, device_properties);
        } catch (const std::exception& error) {
            spdlog::warn("[Vulkan] pipeline cache read unavailable: {}", error.what());
        }

        VkPipelineCacheCreateInfo cache_ci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, nullptr, 0,
                                           cache_data.size(), cache_data.empty() ? nullptr : cache_data.data()};
        auto cache_result = vkCreatePipelineCache(device, &cache_ci, nullptr, &kernels.pipeline_cache);
        if (cache_result != VK_SUCCESS && !cache_data.empty() &&
            cache_result != VK_ERROR_DEVICE_LOST) {
            spdlog::warn("[Vulkan] driver rejected persisted pipeline cache; retrying empty");
            cache_ci.initialDataSize = 0;
            cache_ci.pInitialData = nullptr;
            cache_result = vkCreatePipelineCache(device, &cache_ci, nullptr, &kernels.pipeline_cache);
        }
        check(cache_result, "vkCreatePipelineCache");


        init_core_kernel_pipelines();
        init_draw_kernel_pipelines();

        // ── Save compiled pipeline cache to disk for instant warm starts ──
        if (kernels.pipeline_cache != VK_NULL_HANDLE && !cache_file.empty()) {
            try {
                size_t cache_size = 0;
                if (vkGetPipelineCacheData(device, kernels.pipeline_cache, &cache_size, nullptr) == VK_SUCCESS &&
                    cache_size >= pipeline_cache_io::kHeaderBytes &&
                    cache_size <= pipeline_cache_io::kMaximumBytes) {
                    std::vector<char> cache_out(cache_size);
                    if (vkGetPipelineCacheData(device, kernels.pipeline_cache, &cache_size, cache_out.data()) == VK_SUCCESS) {
                        cache_out.resize(cache_size);
                        if (!pipeline_cache_io::write_atomic(cache_file, cache_out, device_properties))
                            spdlog::warn("[Vulkan] pipeline cache persistence unavailable: {}", cache_file.string());
                    }
                }
            } catch (const std::exception& error) {
                spdlog::warn("[Vulkan] pipeline cache persistence unavailable: {}", error.what());
            }
        }

        const VkCommandBufferAllocateInfo command_info{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, command_pool,
            VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
        check(vkAllocateCommandBuffers(device, &command_info, &command_buffer),
              "vkAllocateCommandBuffers");
        if (debug_context) debug_context->set_command_buffer_name(command_buffer, "Chronon3D.CommandBuffer.Main");
        // Dedicated command buffers + fences + descriptor pools for the
        // frame-batch ring so batch recording never conflicts with the
        // standalone command buffer used by uploads, downloads and
        // single-pass operations, and each slot can be synchronized and its
        // descriptor pool reset independently of the others.
        const VkCommandBufferAllocateInfo batch_command_info{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, command_pool,
            VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
        for (auto& slot_buffer : frame_batch.command_buffers) {
            check(vkAllocateCommandBuffers(device, &batch_command_info, &slot_buffer),
                  "vkAllocateCommandBuffers(frame batch slot)");
            if (debug_context) debug_context->set_command_buffer_name(slot_buffer, "Chronon3D.CommandBuffer.FrameBatchSlot");
        }
        const VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0};
        for (auto& slot_fence : frame_batch.fences) {
            check(vkCreateFence(device, &fence_info, nullptr, &slot_fence),
                  "vkCreateFence(frame batch slot)");
            if (debug_context) debug_context->set_fence_name(slot_fence, "Chronon3D.Fence.FrameBatchSlot");
        }
        for (auto& allocator : descriptor_arena.pass) {
            allocator.create(device, descriptor_layout);
        }
        for (auto& allocator : descriptor_arena.text_tile_bin) {
            allocator.create(device, text_tile_bin_descriptor_layout, 0, 4);
        }
        for (auto& allocator : descriptor_arena.text_tile_raster) {
            allocator.create(device, text_tile_raster_descriptor_layout, 2, 5);
        }
        check(vkCreateFence(device, &fence_info, nullptr, &fence), "vkCreateFence");
        if (debug_context) debug_context->set_fence_name(fence, "Chronon3D.Fence.Main");
        const VkSemaphoreTypeCreateInfo timeline_type{
            VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, nullptr,
            VK_SEMAPHORE_TYPE_TIMELINE, 0};
        const VkSemaphoreCreateInfo timeline_info{
            VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &timeline_type, 0};
        check(vkCreateSemaphore(device, &timeline_info, nullptr, &timeline_semaphore),
              "vkCreateSemaphore(timeline)");
        if (debug_context) debug_context->set_semaphore_name(timeline_semaphore, "Chronon3D.Semaphore.Timeline");

        if (timestamp_valid_bits != 0) {
            // Each ring slot reserves one legacy frame [start,end] pair plus
            // one [start,end] pair for every compiled pass.
            const auto queries_per_slot = static_cast<std::uint32_t>(
                2 + 2 * FrameBatchState::kCompiledPassTimingCapacity);
            const VkQueryPoolCreateInfo query_pool_info{
                VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, nullptr, 0,
                VK_QUERY_TYPE_TIMESTAMP,
                static_cast<std::uint32_t>(
                    FrameBatchState::kSlotCount * queries_per_slot), 0};
            check(vkCreateQueryPool(device, &query_pool_info, nullptr, &timestamp_pool),
                  "vkCreateQueryPool(timestamp)");
            if (debug_context) debug_context->set_query_pool_name(timestamp_pool, "Chronon3D.QueryPool.PassTiming");
        }

        // VK_EXT_calibrated_timestamps: sample (device GPU timestamp, Perfetto
        // trace-clock ns) back-to-back to anchor GPU work on the CPU timeline.
        // One anchor taken at backend construction; completed pass query
        // results are then converted with this single (G0, C0) pair.  When
        // the extension or timestamp queries are unavailable the flag stays
        // false and only CPU-side submit/fence-wait events are traced — no
        // fake GPU bars.
        if (calibrated_ts_supported && timestamp_valid_bits != 0) {
            pfn_get_calibrated_timestamps =
                reinterpret_cast<PFN_vkGetCalibratedTimestampsEXT>(
                    vkGetDeviceProcAddr(device,
                                        "vkGetCalibratedTimestampsEXT"));
            if (pfn_get_calibrated_timestamps) {
                VkCalibratedTimestampInfoEXT device_domain{
                    VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT, nullptr,
                    VK_TIME_DOMAIN_DEVICE_EXT};
                std::uint64_t gpu_ts = 0;
                std::uint64_t max_deviation = 0;
                if (pfn_get_calibrated_timestamps(device, 1, &device_domain,
                                                  &gpu_ts, &max_deviation) ==
                    VK_SUCCESS) {
                    calibration_gpu_ts = gpu_ts;
                    calibration_max_deviation = max_deviation;
                    calibration_cpu_trace_ns = chronon3d::tracing::TraceTimeNs();
                    gpu_timestamps_calibrated = true;
                }
            }
        }
    }

VulkanBackend::Impl::~Impl() {
        if (device != VK_NULL_HANDLE) vkDeviceWaitIdle(device);
        surfaces.destroy_all(*this);
        destroy_image(dst);
        destroy_image(src);
        if (staging.buffer != VK_NULL_HANDLE) memory_manager.destroy_buffer(staging);
        for (std::size_t i = 0; i < kGlyphInstanceRingSize; ++i) {
            if (glyph_instance_buffers[i].buffer != VK_NULL_HANDLE) {
                memory_manager.destroy_buffer(glyph_instance_buffers[i]);
            }
            if (layer_instance_buffers[i].buffer != VK_NULL_HANDLE) {
                memory_manager.destroy_buffer(layer_instance_buffers[i]);
            }
            if (text_run_dynamic_buffers[i].buffer != VK_NULL_HANDLE) {
                memory_manager.destroy_buffer(text_run_dynamic_buffers[i]);
            }
            if (text_glyph_dynamic_buffers[i].buffer != VK_NULL_HANDLE) {
                memory_manager.destroy_buffer(text_glyph_dynamic_buffers[i]);
            }
            if (text_tile_count_buffers[i].buffer != VK_NULL_HANDLE) {
                memory_manager.destroy_buffer(text_tile_count_buffers[i]);
            }
            if (text_tile_index_buffers[i].buffer != VK_NULL_HANDLE) {
                memory_manager.destroy_buffer(text_tile_index_buffers[i]);
            }
        }
        for (auto& buffer : path_geometry_buffers) {
            if (buffer.buffer != VK_NULL_HANDLE) memory_manager.destroy_buffer(buffer);
        }
        for (auto& slot : uploads.slots) destroy_upload_slot(slot);
        descriptor_arena.destroy();
        for (auto& slot_fence : frame_batch.fences) {
            if (slot_fence != VK_NULL_HANDLE) vkDestroyFence(device, slot_fence, nullptr);
        }
        for (auto& slot_buffer : frame_batch.command_buffers) {
            if (slot_buffer != VK_NULL_HANDLE) {
                vkFreeCommandBuffers(device, command_pool, 1, &slot_buffer);
            }
        }
        // ── Replay slot teardown ───────────────────────────────────
        for (auto& slot : replay_slots) {
            if (slot.command_buffer != VK_NULL_HANDLE) {
                vkFreeCommandBuffers(device, command_pool, 1, &slot.command_buffer);
            }
            if (slot.fence != VK_NULL_HANDLE) {
                vkDestroyFence(device, slot.fence, nullptr);
            }
            if (slot.params.buffer != VK_NULL_HANDLE) {
                memory_manager.destroy_buffer(slot.params);
            }
        }
        if (fence != VK_NULL_HANDLE) vkDestroyFence(device, fence, nullptr);
        if (timeline_semaphore != VK_NULL_HANDLE) vkDestroySemaphore(device, timeline_semaphore, nullptr);
        kernels.destroy(device);
        // Descriptor pools are destroyed by descriptor_arena.destroy().
        if (descriptor_pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
        if (descriptor_layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
        if (text_tile_bin_descriptor_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, text_tile_bin_descriptor_layout, nullptr);
        }
        if (text_tile_raster_descriptor_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, text_tile_raster_descriptor_layout, nullptr);
        }
        if (timestamp_pool != VK_NULL_HANDLE) vkDestroyQueryPool(device, timestamp_pool, nullptr);
    }

} // namespace chronon3d::backends::vulkan
