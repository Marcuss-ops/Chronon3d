// vulkan_surface_upload_private.cpp — Vulkan surface upload and readback.
// Out-of-class definitions; surface ownership remains in VulkanSurfaceAuthority.

#include "vulkan_backend_impl.hpp"

#include <cstdlib>
#include <cstring>
#include <spdlog/spdlog.h>

namespace chronon3d::backends::vulkan {

void VulkanBackend::Impl::wait_upload_slot(VulkanUploadRing::UploadSlot& slot) {
        if (!slot.in_flight) return;
        check(vkWaitForFences(device, 1, &slot.fence, VK_TRUE, UINT64_MAX),
              "vkWaitForFences(upload slot)");
        check(vkResetFences(device, 1, &slot.fence), "vkResetFences(upload slot)");
        slot.in_flight = false;
    }
VulkanBackend::Impl::VulkanUploadRing::UploadSlot& VulkanBackend::Impl::acquire_upload_slot(bool wait_for_completion) {
        // Synchronous callers retain the warmed first slot.  Asynchronous
        // callers rotate through the ring and only wait when all slots are
        // occupied, allowing several decoded assets to be queued together.
        if (wait_for_completion) {
            wait_upload_slot(uploads.slots[0]);
            return uploads.slots[0];
        }
        for (std::size_t attempt = 0; attempt < VulkanUploadRing::kSlotCount; ++attempt) {
            auto& slot = uploads.slots[uploads.next_slot];
            uploads.next_slot = (uploads.next_slot + 1) % VulkanUploadRing::kSlotCount;
            if (!slot.in_flight) return slot;
            if (vkGetFenceStatus(device, slot.fence) == VK_SUCCESS) {
                wait_upload_slot(slot);
                return slot;
            }
        }
        auto& oldest = uploads.slots[uploads.next_slot];
        wait_upload_slot(oldest);
        uploads.next_slot = (uploads.next_slot + 1) % VulkanUploadRing::kSlotCount;
        return oldest;
    }
void VulkanBackend::Impl::ensure_upload_slot(VulkanUploadRing::UploadSlot& slot, VkDeviceSize bytes) {
        wait_upload_slot(slot);
        if (slot.buffer_allocation.size < bytes) {
            if (slot.buffer_allocation.buffer != VK_NULL_HANDLE) {
                memory_manager.destroy_buffer(slot.buffer_allocation);
            }
            const VkBufferCreateInfo info{
                VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, bytes,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
            slot.buffer_allocation = memory_manager.create_buffer(info, VulkanMemoryClass::HostUpload);
            if (debug_context) debug_context->set_buffer_name(slot.buffer_allocation.buffer, "Chronon3D.Buffer.SurfaceUpload");
            ++stats.staging_allocations;
        }
        if (slot.command_buffer == VK_NULL_HANDLE) {
            const VkCommandBufferAllocateInfo command_info{
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, command_pool,
                VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
            check(vkAllocateCommandBuffers(device, &command_info, &slot.command_buffer),
                  "vkAllocateCommandBuffers(upload slot)");
            if (debug_context) debug_context->set_command_buffer_name(slot.command_buffer, "Chronon3D.CommandBuffer.SurfaceUploadSlot");
            const VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0};
            check(vkCreateFence(device, &fence_info, nullptr, &slot.fence),
                  "vkCreateFence(upload slot)");
            if (debug_context) debug_context->set_fence_name(slot.fence, "Chronon3D.Fence.SurfaceUploadSlot");
        }
    }
void VulkanBackend::Impl::clear_uninitialized_upload_target(
        VkCommandBuffer command, Image& image) {
        if (image.initialized) return;

        // A retained physical slot can still contain the previous occupant's
        // pixels when its logical binding is recycled. Partial uploads must
        // establish transparent contents before copying their first region.
        transition(command, image.image, VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        const VkClearColorValue clear{{0.0f, 0.0f, 0.0f, 0.0f}};
        const VkImageSubresourceRange range{
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(command, image.image,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &clear, 1, &range);
    }
std::uint64_t VulkanBackend::Impl::submit_upload(VulkanUploadRing::UploadSlot& slot, bool wait_for_completion) {
        check(vkEndCommandBuffer(slot.command_buffer), "vkEndCommandBuffer(upload slot)");
        const auto signal_value = ++next_timeline_value;
        const VkTimelineSemaphoreSubmitInfo timeline_submit{
            VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, nullptr,
            0, nullptr, 1, &signal_value};
        const VkSemaphore signal_semaphores[] = {timeline_semaphore};
        const VkSubmitInfo submit_info{
            VK_STRUCTURE_TYPE_SUBMIT_INFO, &timeline_submit, 0, nullptr, nullptr,
            1, &slot.command_buffer, 1, signal_semaphores};
        check(vkQueueSubmit(queue, 1, &submit_info, slot.fence),
              "vkQueueSubmit(upload slot)");
        ++stats.submissions;
        slot.ticket = signal_value;
        slot.in_flight = true;
        if (wait_for_completion) {
            wait_upload_slot(slot);
            uploads.pending_timeline_ticket = 0;
        } else {
            uploads.pending_timeline_ticket = signal_value;
        }
        return signal_value;
    }
std::uint64_t VulkanBackend::Impl::upload(runtime::RenderSurfaceHandle handle, const runtime::SurfaceDesc& desc,
                         std::span<const float> rgba, bool wait_for_completion) {
        auto& image = ensure_surface(handle, desc);
        // Full uploads use the descriptor's actual storage format.  The old
        // hard-coded Rgba32Float size rejected persistent Rgba8 MTSDF pages
        // before their first glyph region upload could even be issued.
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(runtime::tight_surface_bytes(
            desc.format, desc.width, desc.height));
        if (rgba.size_bytes() != bytes) throw std::invalid_argument("Vulkan upload size does not match surface");
        if (std::getenv("CHRONON3D_TEXT_COMPOSITE_DIAG")) {
            spdlog::info(
                "[vk-upload-diag] handle={} image={} view={} fmt={} {}x{} "
                "init={} exportable={} bytes={} sync={}",
                handle, reinterpret_cast<std::uintptr_t>(image.image),
                reinterpret_cast<std::uintptr_t>(image.view),
                static_cast<int>(image.format), image.width, image.height,
                image.initialized ? 1 : 0, image.exportable ? 1 : 0,
                bytes, wait_for_completion ? 1 : 0);
        }
        ++stats.upload_calls;
        stats.upload_bytes += bytes;
        stats.upload_full_surface_bytes += bytes;
        const auto producer = static_cast<std::size_t>(profiling::g_gpu_upload_producer);
        if (producer < stats.upload_producer_bytes.size()) {
            stats.upload_producer_bytes[producer] += bytes;
            stats.upload_producer_full_count[producer] += 1;
            if (profiling::g_gpu_upload_producer == profiling::GpuUploadProducer::Image) {
                stats.upload_producer_initial_count[producer] += 1;
                stats.upload_producer_initial_bytes[producer] += bytes;
            }
        }
        auto& slot = acquire_upload_slot(wait_for_completion);
        ensure_upload_slot(slot, bytes);
        std::memcpy(slot.buffer_allocation.mapped, rgba.data(), static_cast<std::size_t>(bytes));
        const VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
        check(vkResetCommandBuffer(slot.command_buffer, 0),
              "vkResetCommandBuffer(upload slot)");
        check(vkBeginCommandBuffer(slot.command_buffer, &begin),
              "vkBeginCommandBuffer(upload slot)");
        // A full-surface upload replaces every texel, so the previous contents
        // are irrelevant. Always declare the source layout UNDEFINED: a
        // recycled physical slot can still be marked initialized while its
        // actual layout is UNDEFINED. Claiming GENERAL here leaves the image
        // in the wrong layout for the first shader read and the composite
        // samples an undefined surface (observed as a black text layer).
        transition(slot.command_buffer, image.image,
                   VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        const VkBufferImageCopy copy{0, desc.width, desc.height,
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {desc.width, desc.height, 1}};
        vkCmdCopyBufferToImage(slot.command_buffer, slot.buffer_allocation.buffer, image.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        transition(slot.command_buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_GENERAL);
        image.initialized = true;
        return submit_upload(slot, wait_for_completion);
    }
std::uint64_t VulkanBackend::Impl::upload_region(runtime::RenderSurfaceHandle handle,
                                const runtime::SurfaceDesc& desc,
                                std::int32_t x, std::int32_t y,
                                std::uint32_t width, std::uint32_t height,
                                std::span<const float> rgba,
                                bool wait_for_completion) {
        if (x < 0 || y < 0 || width == 0 || height == 0 ||
            static_cast<std::uint32_t>(x) + width > desc.width ||
            static_cast<std::uint32_t>(y) + height > desc.height) {
            throw std::invalid_argument("Vulkan region upload is outside surface bounds");
        }
        auto& image = ensure_surface(handle, desc);
        const auto px_bytes = pixel_format_bytes(desc.format);
        const VkDeviceSize byte_count = static_cast<VkDeviceSize>(width) * height * px_bytes;
        const VkDeviceSize src_byte_count = static_cast<VkDeviceSize>(rgba.size_bytes());
        if (desc.format == runtime::PixelFormat::R8Unorm) {
            // R8Unorm: one float per pixel (coverage 0..1), packed to 1 byte.
            // Accept the float span and pack during upload.
            const auto expected_floats = static_cast<VkDeviceSize>(width) * height;
            if (static_cast<VkDeviceSize>(rgba.size()) != expected_floats) {
                throw std::invalid_argument(
                    "Vulkan region upload size does not match rectangle for R8Unorm");
            }
        } else if (src_byte_count != byte_count) {
            throw std::invalid_argument("Vulkan region upload size does not match rectangle");
        }
        ++stats.upload_calls;
        stats.upload_bytes += byte_count;
        stats.upload_region_bytes += byte_count;
        const auto producer = static_cast<std::size_t>(profiling::g_gpu_upload_producer);
        if (producer < stats.upload_producer_bytes.size()) {
            stats.upload_producer_bytes[producer] += byte_count;
            stats.upload_producer_region_count[producer] += 1;
        }
        auto& slot = acquire_upload_slot(wait_for_completion);
        ensure_upload_slot(slot, byte_count);
        if (desc.format == runtime::PixelFormat::R8Unorm) {
            // Pack float→R8: clamp to [0,1], convert to uint8
            auto* dst = static_cast<std::uint8_t*>(slot.buffer_allocation.mapped);
            for (std::size_t i = 0; i < rgba.size(); ++i) {
                dst[i] = static_cast<std::uint8_t>(
                    std::clamp(rgba[i], 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        } else {
            std::memcpy(slot.buffer_allocation.mapped, rgba.data(), static_cast<std::size_t>(byte_count));
        }
        const VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
        check(vkResetCommandBuffer(slot.command_buffer, 0), "vkResetCommandBuffer(region upload slot)");
        check(vkBeginCommandBuffer(slot.command_buffer, &begin), "vkBeginCommandBuffer(region upload slot)");
        clear_uninitialized_upload_target(slot.command_buffer, image);
        if (image.initialized) {
            transition(slot.command_buffer, image.image,
                       VK_IMAGE_LAYOUT_GENERAL,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        }
        const VkBufferImageCopy copy{
            0, width, height,
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            {x, y, 0}, {width, height, 1}};
        vkCmdCopyBufferToImage(slot.command_buffer, slot.buffer_allocation.buffer, image.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        transition(slot.command_buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_GENERAL);
        image.initialized = true;
        return submit_upload(slot, wait_for_completion);
    }
std::uint64_t VulkanBackend::Impl::upload_regions(
        runtime::RenderSurfaceHandle handle,
        const runtime::SurfaceDesc& desc,
        std::span<const graph::SurfaceRegionUpload> regions,
        bool wait_for_completion) {
        if (regions.empty()) return 0;
        auto& image = ensure_surface(handle, desc);
        const auto px_bytes = pixel_format_bytes(desc.format);
        constexpr VkDeviceSize kRegionAlignment = 16;
        const auto align_up = [](VkDeviceSize value) noexcept {
            return (value + kRegionAlignment - 1) & ~(kRegionAlignment - 1);
        };

        struct PreparedRegion {
            VkDeviceSize offset{0};
            VkDeviceSize bytes{0};
            const graph::SurfaceRegionUpload* region{nullptr};
        };
        std::vector<PreparedRegion> prepared;
        prepared.reserve(regions.size());
        VkDeviceSize total_bytes = 0;
        for (const auto& region : regions) {
            if (region.x < 0 || region.y < 0 ||
                region.width == 0 || region.height == 0 ||
                static_cast<std::uint32_t>(region.x) + region.width > desc.width ||
                static_cast<std::uint32_t>(region.y) + region.height > desc.height) {
                throw std::invalid_argument(
                    "Vulkan region upload is outside surface bounds");
            }
            const VkDeviceSize byte_count =
                static_cast<VkDeviceSize>(region.width) * region.height * px_bytes;
            if (desc.format == runtime::PixelFormat::R8Unorm) {
                const auto expected_floats =
                    static_cast<VkDeviceSize>(region.width) * region.height;
                if (static_cast<VkDeviceSize>(region.rgba.size()) != expected_floats) {
                    throw std::invalid_argument(
                        "Vulkan region upload size does not match rectangle for R8Unorm");
                }
            } else if (region.rgba.size_bytes() != byte_count) {
                throw std::invalid_argument(
                    "Vulkan region upload size does not match rectangle");
            }
            const VkDeviceSize offset = align_up(total_bytes);
            prepared.push_back(PreparedRegion{offset, byte_count, &region});
            total_bytes = offset + byte_count;
            ++stats.upload_calls;
            stats.upload_bytes += byte_count;
            stats.upload_region_bytes += byte_count;
            const auto producer = static_cast<std::size_t>(profiling::g_gpu_upload_producer);
            if (producer < stats.upload_producer_bytes.size()) {
                stats.upload_producer_bytes[producer] += byte_count;
                stats.upload_producer_region_count[producer] += 1;
            }
        }

        auto& slot = acquire_upload_slot(wait_for_completion);
        ensure_upload_slot(slot, total_bytes);
        auto* dst = static_cast<std::uint8_t*>(slot.buffer_allocation.mapped);
        for (const auto& entry : prepared) {
            auto* target = dst + entry.offset;
            if (desc.format == runtime::PixelFormat::R8Unorm) {
                const auto& rgba = entry.region->rgba;
                for (std::size_t i = 0; i < rgba.size(); ++i) {
                    target[i] = static_cast<std::uint8_t>(
                        std::clamp(rgba[i], 0.0f, 1.0f) * 255.0f + 0.5f);
                }
            } else {
                std::memcpy(target, entry.region->rgba.data(),
                            static_cast<std::size_t>(entry.bytes));
            }
        }

        const VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
        check(vkResetCommandBuffer(slot.command_buffer, 0),
              "vkResetCommandBuffer(region batch upload slot)");
        check(vkBeginCommandBuffer(slot.command_buffer, &begin),
              "vkBeginCommandBuffer(region batch upload slot)");
        clear_uninitialized_upload_target(slot.command_buffer, image);
        if (image.initialized) {
            transition(slot.command_buffer, image.image,
                       VK_IMAGE_LAYOUT_GENERAL,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        }
        for (const auto& entry : prepared) {
            const auto& region = *entry.region;
            const VkBufferImageCopy copy{
                entry.offset, 0, 0,
                {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                {region.x, region.y, 0},
                {region.width, region.height, 1}};
            vkCmdCopyBufferToImage(slot.command_buffer,
                                   slot.buffer_allocation.buffer, image.image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                   &copy);
        }
        transition(slot.command_buffer, image.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_GENERAL);
        image.initialized = true;
        return submit_upload(slot, wait_for_completion);
    }
std::uint64_t VulkanBackend::Impl::upload_rows(
        runtime::RenderSurfaceHandle handle,
        const runtime::SurfaceDesc& desc,
        const raster::BBox& clip,
        const graph::RenderBackend::UploadRows& rows,
        bool wait_for_completion) {
        if (clip.x0 < 0 || clip.y0 < 0 || clip.x1 <= clip.x0 || clip.y1 <= clip.y0 ||
            static_cast<std::uint32_t>(clip.x1) > desc.width ||
            static_cast<std::uint32_t>(clip.y1) > desc.height) {
            throw std::invalid_argument("Vulkan rows upload is outside surface bounds");
        }
        const auto rw = static_cast<std::uint32_t>(clip.x1 - clip.x0);
        const auto rh = static_cast<std::uint32_t>(clip.y1 - clip.y0);
        if (rows.first_row.size() < rw || rows.stride < rw) {
            throw std::invalid_argument("Vulkan rows upload span or stride smaller than clip width");
        }
        auto& image = ensure_surface(handle, desc);
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(rw) * rh * sizeof(Color);
        const bool full_upload = (clip.x0 == 0 && clip.y0 == 0 &&
                                  rw == desc.width && rh == desc.height);
        ++stats.upload_calls;
        stats.upload_bytes += bytes;
        if (full_upload) {
            stats.upload_full_surface_bytes += bytes;
        } else {
            stats.upload_region_bytes += bytes;
        }
        const auto producer = static_cast<std::size_t>(profiling::g_gpu_upload_producer);
        if (producer < stats.upload_producer_bytes.size()) {
            stats.upload_producer_bytes[producer] += bytes;
            if (full_upload) {
                stats.upload_producer_full_count[producer] += 1;
            } else {
                stats.upload_producer_region_count[producer] += 1;
            }
        }
        auto& slot = acquire_upload_slot(wait_for_completion);
        ensure_upload_slot(slot, bytes);
        auto* dst = static_cast<char*>(slot.buffer_allocation.mapped);
        const auto* src = reinterpret_cast<const char*>(rows.first_row.data());
        const std::size_t row_bytes = rw * sizeof(Color);
        const std::size_t stride_bytes = rows.stride * sizeof(Color);
        if (rows.stride == rw) {
            std::memcpy(dst, src, row_bytes * rh);
        } else {
            for (std::size_t y = 0; y < rh; ++y) {
                std::memcpy(dst + y * row_bytes, src + y * stride_bytes, row_bytes);
            }
        }
        const VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
        check(vkResetCommandBuffer(slot.command_buffer, 0),
              "vkResetCommandBuffer(rows upload slot)");
        check(vkBeginCommandBuffer(slot.command_buffer, &begin),
              "vkBeginCommandBuffer(rows upload slot)");
        transition(slot.command_buffer, image.image,
                   image.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        const VkBufferImageCopy copy{0, rw, rh,
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            {clip.x0, clip.y0, 0},
            {rw, rh, 1}};
        vkCmdCopyBufferToImage(slot.command_buffer, slot.buffer_allocation.buffer, image.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        transition(slot.command_buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_GENERAL);
        image.initialized = true;
        return submit_upload(slot, wait_for_completion);
    }
void VulkanBackend::Impl::wait_upload_ticket(std::uint64_t ticket) {
        const VkSemaphoreWaitInfo wait_info{
            VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, nullptr, 0, 1,
            &timeline_semaphore, &ticket};
        check(vkWaitSemaphores(device, &wait_info, UINT64_MAX),
              "vkWaitSemaphores(upload ticket)");
        for (auto& slot : uploads.slots) {
            if (slot.in_flight && slot.ticket == ticket) wait_upload_slot(slot);
        }
    }
void VulkanBackend::Impl::download(runtime::RenderSurfaceHandle handle, std::span<float> rgba) {
        if (surfaces.surface_bindings.count(handle) == 0) {
            throw std::invalid_argument("Vulkan download references an uninitialized surface");
        }
        auto& image = resolve_image(handle);
        if (!image.initialized) {
            throw std::invalid_argument("Vulkan download references an uninitialized surface");
        }
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(runtime::tight_surface_bytes(
            runtime::PixelFormat::Rgba32Float, image.width, image.height));
        if (rgba.size_bytes() != bytes) throw std::invalid_argument("Vulkan download size does not match surface");
        ++stats.readback_calls;
        stats.readback_bytes += bytes;
        // Gate 7 (cpu_pixel_readback_bytes) canary — this function is the
        // single GPU→CPU pixel download authority on the Vulkan backend.
        // Instrumented here (2026-09-07, Z0 census) so the zero-copy gate is
        // measured instead of passing vacuously; the native video lane never
        // reaches download() (mark_gpu_authoritative + refusal paths), so a
        // non-zero value is an honest video regression canary.
        if (auto* counters = profiling::g_current_counters) {
            counters->cpu_pixel_readback_count.fetch_add(
                1, std::memory_order_relaxed);
            counters->cpu_pixel_readback_bytes.fetch_add(
                static_cast<std::uint64_t>(bytes), std::memory_order_relaxed);
        }
        ensure_staging(bytes);
        begin_command_buffer();
        transition(command_buffer, image.image, VK_IMAGE_LAYOUT_GENERAL,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        const VkBufferImageCopy copy{0, image.width, image.height,
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {image.width, image.height, 1}};
        vkCmdCopyImageToBuffer(command_buffer, image.image,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.buffer, 1, &copy);
        transition(command_buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_IMAGE_LAYOUT_GENERAL);
        const VkBufferMemoryBarrier2KHR buffer_barrier{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR,
            nullptr,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
            VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
            VK_PIPELINE_STAGE_2_HOST_BIT_KHR,
            VK_ACCESS_2_HOST_READ_BIT_KHR,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            staging.buffer,
            0,
            bytes};
        emit_buffer_barriers2(command_buffer, std::span<const VkBufferMemoryBarrier2KHR>{&buffer_barrier, 1});
        // CPU reads this persistently mapped destination immediately below.
        // Complete the image-to-buffer copy before consuming its bytes.
        submit(true);
        vmaInvalidateAllocation(memory_manager.native_allocator(), staging.allocation, 0, bytes);
        const auto readback_start = profiling::now();
        std::memcpy(rgba.data(), staging.mapped, static_cast<std::size_t>(bytes));
        stats.readback_us += static_cast<std::uint64_t>(profiling::elapsed_us(readback_start));
    }

} // namespace chronon3d::backends::vulkan
