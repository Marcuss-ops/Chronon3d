// vulkan_backend_layer_ops_private.cpp — VulkanBackend::Impl layer batch
// execution (instanced layer draw dispatch).

#include "vulkan_backend_impl.hpp"
#include <algorithm>
#include <cmath>

namespace chronon3d::backends::vulkan {

namespace {

struct alignas(16) GpuLayerInstance {
    float dst_rect[4]{0.0f, 0.0f, 0.0f, 0.0f};
    float src_rect[4]{0.0f, 0.0f, 1.0f, 1.0f};
    float affine_row0[4]{0.0f, 0.0f, 0.0f, 0.0f};
    float affine_row1[4]{0.0f, 0.0f, 0.0f, 0.0f};
    float limits[4]{0.0f, 0.0f, 1.0f, 1.0f};
    std::uint32_t has_affine{0};
    std::uint32_t blend_mode{0};
    std::uint32_t resource_idx{0};
    std::uint32_t pad{0};
};
static_assert(sizeof(GpuLayerInstance) == 96,
              "GpuLayerInstance must be 96 bytes");

} // namespace

void VulkanBackend::Impl::execute_layer_batch(
    runtime::RenderSurfaceHandle destination,
    std::span<const runtime::LayerInstance> instances,
    std::span<const runtime::RenderSurfaceHandle> resources,
    std::span<const float> transforms,
    std::span<const float> paints) {
    (void)paints;
    if (instances.empty()) return;
    ++stats.layer_batch_calls;
    stats.layer_instances_processed += instances.size();

    auto& dst_image = resolve_image(destination);
    if (!dst_image.initialized || dst_image.width == 0 || dst_image.height == 0) {
        throw std::invalid_argument(
            "Vulkan layer batch references an invalid destination surface");
    }
    if (!frame_batch.active) {
        throw std::logic_error("Vulkan layer batch requires an active frame batch");
    }

    const auto cmd = active_command_buffer();
    bool pass_sync_emitted = false;
    std::size_t group_index = 0;
    std::size_t start_idx = 0;
    while (start_idx < instances.size()) {
        const auto res_idx = instances[start_idx].resource_index;
        if (res_idx >= resources.size()) {
            throw std::invalid_argument(
                "Vulkan layer batch resource_index out of range");
        }
        const auto source_handle = resources[res_idx];
        if (source_handle == runtime::kInvalidRenderSurfaceHandle) {
            throw std::invalid_argument(
                "Vulkan layer batch references an invalid image resource");
        }

        std::size_t end_idx = start_idx + 1;
        while (end_idx < instances.size() &&
               instances[end_idx].resource_index == res_idx) {
            ++end_idx;
        }

        auto& src_image = resolve_image(source_handle);
        if (!src_image.initialized || src_image.width == 0 || src_image.height == 0) {
            throw std::invalid_argument(
                "Vulkan layer batch source image is not initialized");
        }

        const std::size_t count = end_idx - start_idx;
        std::array<GpuLayerInstance, 512> local_instances;
        std::vector<GpuLayerInstance> heap_instances;
        GpuLayerInstance* gpu_instances = local_instances.data();
        if (count > local_instances.size()) {
            heap_instances.resize(count);
            gpu_instances = heap_instances.data();
        }

        std::int32_t dispatch_x0 = static_cast<std::int32_t>(dst_image.width);
        std::int32_t dispatch_y0 = static_cast<std::int32_t>(dst_image.height);
        std::int32_t dispatch_x1 = 0;
        std::int32_t dispatch_y1 = 0;

        for (std::size_t i = 0; i < count; ++i) {
            const auto& inst = instances[start_idx + i];
            auto& gpu_inst = gpu_instances[i];
            gpu_inst = GpuLayerInstance{};
            float dx0 = inst.dst_x0;
            float dy0 = inst.dst_y0;
            float dx1 = inst.dst_x1;
            float dy1 = inst.dst_y1;
            if (dx1 <= 1.0f && dy1 <= 1.0f &&
                (dx1 > dx0 || dy1 > dy0)) {
                dx0 *= static_cast<float>(dst_image.width);
                dy0 *= static_cast<float>(dst_image.height);
                dx1 *= static_cast<float>(dst_image.width);
                dy1 *= static_cast<float>(dst_image.height);
            }
            gpu_inst.dst_rect[0] = dx0;
            gpu_inst.dst_rect[1] = dy0;
            gpu_inst.dst_rect[2] = dx1;
            gpu_inst.dst_rect[3] = dy1;
            gpu_inst.src_rect[0] = inst.src_x0;
            gpu_inst.src_rect[1] = inst.src_y0;
            gpu_inst.src_rect[2] =
                (inst.src_x1 == 0.0f && inst.src_y1 == 0.0f) ? 1.0f : inst.src_x1;
            gpu_inst.src_rect[3] =
                (inst.src_x1 == 0.0f && inst.src_y1 == 0.0f) ? 1.0f : inst.src_y1;
            gpu_inst.limits[0] = static_cast<float>(src_image.width);
            gpu_inst.limits[1] = static_cast<float>(src_image.height);
            gpu_inst.limits[2] = inst.opacity;
            gpu_inst.limits[3] = 1.0f;

            if (inst.transform_index > 0 &&
                (inst.transform_index + 1) * 16 <= transforms.size()) {
                gpu_inst.has_affine = 1;
                const float* m = transforms.data() + inst.transform_index * 16;
                gpu_inst.affine_row0[0] = m[0];
                gpu_inst.affine_row0[1] = m[4];
                gpu_inst.affine_row0[2] = m[12];
                gpu_inst.affine_row1[0] = m[1];
                gpu_inst.affine_row1[1] = m[5];
                gpu_inst.affine_row1[2] = m[13];
                dispatch_x0 = dispatch_y0 = 0;
                dispatch_x1 = static_cast<std::int32_t>(dst_image.width);
                dispatch_y1 = static_cast<std::int32_t>(dst_image.height);
            } else {
                gpu_inst.has_affine = 0;
                dispatch_x0 = std::min(
                    dispatch_x0, static_cast<std::int32_t>(std::floor(dx0)));
                dispatch_y0 = std::min(
                    dispatch_y0, static_cast<std::int32_t>(std::floor(dy0)));
                dispatch_x1 = std::max(
                    dispatch_x1, static_cast<std::int32_t>(std::ceil(dx1)));
                dispatch_y1 = std::max(
                    dispatch_y1, static_cast<std::int32_t>(std::ceil(dy1)));
            }
            gpu_inst.blend_mode = (inst.blend == BlendMode::Add) ? 1u : 0u;
            gpu_inst.resource_idx = inst.resource_index;
        }

        if (dispatch_x0 >= dispatch_x1 || dispatch_y0 >= dispatch_y1) {
            dispatch_x0 = dispatch_y0 = 0;
            dispatch_x1 = static_cast<std::int32_t>(dst_image.width);
            dispatch_y1 = static_cast<std::int32_t>(dst_image.height);
        }

        const VkDeviceSize bytes =
            static_cast<VkDeviceSize>(count * sizeof(GpuLayerInstance));
        const std::size_t instance_slot =
            frame_batch.next_slot * kGlyphInstancePassesPerSlot +
            ((frame_batch.pass_count + group_index) % kGlyphInstancePassesPerSlot);
        ensure_layer_instance_buffer(bytes, instance_slot);
        const VkBuffer instance_buffer = layer_instance_buffers[instance_slot].buffer;

        std::uint64_t instance_hash = 1469598103934665603ull;
        const auto* byte_ptr =
            reinterpret_cast<const std::uint8_t*>(gpu_instances);
        for (std::size_t b = 0; b < bytes; ++b) {
            instance_hash ^= byte_ptr[b];
            instance_hash *= 1099511628211ull;
        }
        const bool instance_updated =
            layer_instance_sizes[instance_slot] != bytes ||
            layer_instance_hashes[instance_slot] != instance_hash;

        const auto descriptors = allocate_pass_descriptor_set();
        write_layer_batch_descriptors(descriptors, dst_image, src_image,
                                      instance_buffer);
        if (!pass_sync_emitted) {
            emit_pass_sync(cmd, {&dst_image, &src_image});
            pass_sync_emitted = true;
        }
        if (instance_updated) {
            vkCmdUpdateBuffer(cmd, instance_buffer, 0, bytes, gpu_instances);
            layer_instance_hashes[instance_slot] = instance_hash;
            layer_instance_sizes[instance_slot] = bytes;
        }
        record_layer_batch(cmd, descriptors, dst_image,
                           static_cast<std::int32_t>(count),
                           instance_buffer, instance_updated,
                           dispatch_x0, dispatch_y0, dispatch_x1, dispatch_y1);
        dst_image.initialized = true;
        start_idx = end_idx;
        ++group_index;
    }

    ++frame_batch.pass_count;
    ++stats.passes_executed;
}

} // namespace chronon3d::backends::vulkan
