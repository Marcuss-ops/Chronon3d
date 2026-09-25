// cuda_nv12_compositor_batch.cpp — CudaNv12Compositor
// GpuLayerBatch -> direct NV12/P010 precursor path.

#include "cuda_nv12_compositor_detail.hpp"

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

#include <chronon3d/core/profiling/profiling.hpp>

#include <span>
#include <vector>

namespace chronon3d::media::video {

bool CudaNv12Compositor::composite_direct_nv12_batch(
    const runtime::GpuLayerBatch& batch,
    std::span<const CudaLayerResource> resources,
    CUdeviceptr bg_y, int bg_yp, CUdeviceptr bg_uv, int bg_uvp,
    CUdeviceptr out_y, int out_yp, CUdeviceptr out_uv, int out_uvp,
    std::uint32_t width, std::uint32_t height, CUstream stream,
    CudaYuvFormat format, std::uint32_t transfer_encode) {
    using cuda_nv12_detail::DirectYuvLayerHost;
    using cuda_nv12_detail::check_cuda;

    const CUfunction kernel = format == CudaYuvFormat::P010
        ? direct_p010_batch_kernel_ : direct_nv12_batch_kernel_;
    if (!kernel || !bg_y || !bg_uv || !out_y || !out_uv ||
        width == 0 || height == 0 || batch.instances.empty()) return false;

    // AUDIT-02A: the pinned host staging buffer is allocated once per slot
    // and grown by power-of-two steps; no std::vector allocation per frame.
    // AUDIT-02B: slot index ping-pongs every call, so consecutive calls never
    // overwrite the same device slot while a previous stream still owns it.
    const int slot = layer_batch_slot_;
    layer_batch_slot_ = (slot + 1) % kLayerBatchSlots;
    const std::size_t required_bytes = batch.instances.size() * sizeof(DirectYuvLayerHost);
    std::size_t capacity = layer_batch_capacity_[slot];
    if (capacity < required_bytes) {
        std::size_t next = capacity ? capacity : sizeof(DirectYuvLayerHost) * 8;
        while (next < required_bytes) next *= 2;
        if (layer_batch_host_[slot])
            check_cuda(cuMemFreeHost(reinterpret_cast<void*>(layer_batch_host_[slot])),
                       "cuMemFreeHost(layer batch host)");
        // cuMemAllocHost hands back a host void*; store it as CUdeviceptr.
        void* host_ptr = nullptr;
        check_cuda(cuMemAllocHost(&host_ptr, next),
                   "cuMemAllocHost(layer batch host)");
        layer_batch_host_[slot] = reinterpret_cast<CUdeviceptr>(host_ptr);
        layer_batch_capacity_[slot] = next;
        capacity = next;
    }

    auto* host_layers = reinterpret_cast<DirectYuvLayerHost*>(layer_batch_host_[slot]);
    std::size_t layer_count_host = 0;
    for (const auto& instance : batch.instances) {
        if (instance.resource_index >= resources.size()) return false;
        const auto& resource = resources[instance.resource_index];
        if (!resource.rgba || resource.pitch_bytes <= 0 ||
            resource.width == 0 || resource.height == 0) return false;
        if (instance.blend != BlendMode::Normal && instance.blend != BlendMode::Add) {
            return false;
        }
        DirectYuvLayerHost& layer = host_layers[layer_count_host++];
        layer.rgba = resource.rgba;
        layer.pitch = resource.pitch_bytes;
        layer.source_width = static_cast<int>(resource.width);
        layer.source_height = static_cast<int>(resource.height);
        layer.dst_x0 = instance.dst_x0;
        layer.dst_y0 = instance.dst_y0;
        layer.dst_x1 = instance.dst_x1;
        layer.dst_y1 = instance.dst_y1;
        if (layer.dst_x1 <= 1.0f && layer.dst_y1 <= 1.0f &&
            (layer.dst_x1 > layer.dst_x0 || layer.dst_y1 > layer.dst_y0)) {
            layer.dst_x0 *= static_cast<float>(width);
            layer.dst_y0 *= static_cast<float>(height);
            layer.dst_x1 *= static_cast<float>(width);
            layer.dst_y1 *= static_cast<float>(height);
        }
        layer.src_x0 = instance.src_x0;
        layer.src_y0 = instance.src_y0;
        layer.src_x1 = (instance.src_x1 == 0.0f && instance.src_y1 == 0.0f)
            ? 1.0f : instance.src_x1;
        layer.src_y1 = (instance.src_x1 == 0.0f && instance.src_y1 == 0.0f)
            ? 1.0f : instance.src_y1;
        layer.opacity = instance.opacity;
        layer.blend_mode = instance.blend == BlendMode::Add ? 1 : 0;
        layer.resource_kind = static_cast<int>(resource.kind);
        layer.distance_range_px = resource.distance_range_px;
        layer.color = resource.color;
        layer.stroke_color = resource.stroke_color;
        layer.stroke_width = resource.stroke_width;
        layer.shadow_color = resource.shadow_color;
        layer.shadow_opacity = resource.shadow_opacity;
        layer.shadow_dx = resource.shadow_dx;
        layer.shadow_dy = resource.shadow_dy;
        layer.shadow_blur = resource.shadow_blur;
    }

    check_cuda(cuCtxSetCurrent(context_), "cuCtxSetCurrent");
    const CUstream active = stream ? stream : stream_;
    const std::size_t bytes = layer_count_host * sizeof(DirectYuvLayerHost);
    if (layer_batch_buffer_[slot] && layer_batch_capacity_dev_[slot] < bytes) {
        check_cuda(cuMemFree(layer_batch_buffer_[slot]), "cuMemFree(layer batch)");
        layer_batch_buffer_[slot] = 0;
    }
    if (!layer_batch_buffer_[slot]) {
        std::size_t dev_bytes = bytes ? bytes : sizeof(DirectYuvLayerHost) * 8;
        check_cuda(cuMemAlloc(&layer_batch_buffer_[slot], dev_bytes), "cuMemAlloc(layer batch)");
        layer_batch_capacity_dev_[slot] = dev_bytes;
    }
    const auto started = std::chrono::steady_clock::now();
    check_cuda(cuMemcpyHtoDAsync(
                   layer_batch_buffer_[slot], host_layers, bytes, active),
               "cuMemcpyHtoDAsync(layer batch)");
    CUdeviceptr layers = layer_batch_buffer_[slot];
    int layer_count = static_cast<int>(layer_count_host);
    void* args[] = {&bg_y, &bg_yp, &bg_uv, &bg_uvp, &out_y, &out_yp,
                    &out_uv, &out_uvp, &layers, &layer_count, &width, &height,
                    &transfer_encode};
    check_cuda(cuLaunchKernel(
                   kernel, (width + 31) / 32,
                   (height + 31) / 32, 1, 16, 16, 1, 0, active, args, nullptr),
               format == CudaYuvFormat::P010
                   ? "cuLaunchKernel(p010_composite_layer_batch_2x2)"
                   : "cuLaunchKernel(nv12_composite_layer_batch_2x2)");
    if (auto* counters = profiling::g_current_counters) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count();
        counters->cuda_composite_frames.fetch_add(1, std::memory_order_relaxed);
        counters->cuda_composite_wall_us.fetch_add(
            static_cast<std::uint64_t>(elapsed), std::memory_order_relaxed);
        counters->compositor_wall_ms_total.fetch_add(
            static_cast<std::uint64_t>(elapsed / 1000), std::memory_order_relaxed);
    }
    return true;
}

} // namespace chronon3d::media::video

#endif
