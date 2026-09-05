// cuda_nv12_surface_compositor_batch.cpp — CudaNv12SurfaceCompositor
// GpuLayerBatch -> direct NV12/P010 precursor path.

#include "cuda_nv12_compositor_detail.hpp"

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

#include <chronon3d/core/profiling/profiling.hpp>

#include <span>
#include <vector>

namespace chronon3d::backends::vulkan {

bool CudaNv12SurfaceCompositor::composite_direct_nv12_batch(
    const runtime::GpuLayerBatch& batch,
    std::span<const CudaLayerResource> resources,
    CUdeviceptr bg_y, int bg_yp, CUdeviceptr bg_uv, int bg_uvp,
    CUdeviceptr out_y, int out_yp, CUdeviceptr out_uv, int out_uvp,
    std::uint32_t width, std::uint32_t height, CUstream stream) {
    using cuda_nv12_detail::DirectYuvLayerHost;
    using cuda_nv12_detail::check_cuda;

    if (!direct_nv12_batch_kernel_ || !bg_y || !bg_uv || !out_y || !out_uv ||
        width == 0 || height == 0 || batch.instances.empty()) return false;

    std::vector<DirectYuvLayerHost> host_layers;
    host_layers.reserve(batch.instances.size());
    for (const auto& instance : batch.instances) {
        if (instance.resource_index >= resources.size()) return false;
        const auto& resource = resources[instance.resource_index];
        if (!resource.rgba || resource.pitch_bytes <= 0 ||
            resource.width == 0 || resource.height == 0) return false;
        if (instance.blend != BlendMode::Normal && instance.blend != BlendMode::Add) {
            return false;
        }
        DirectYuvLayerHost layer;
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
        host_layers.push_back(layer);
    }

    check_cuda(cuCtxSetCurrent(context_), "cuCtxSetCurrent");
    const CUstream active = stream ? stream : stream_;
    const std::size_t bytes = host_layers.size() * sizeof(DirectYuvLayerHost);
    if (layer_batch_capacity_ < bytes) {
        if (layer_batch_buffer_) check_cuda(cuMemFree(layer_batch_buffer_), "cuMemFree(layer batch)");
        check_cuda(cuMemAlloc(&layer_batch_buffer_, bytes), "cuMemAlloc(layer batch)");
        layer_batch_capacity_ = bytes;
    }
    const auto started = std::chrono::steady_clock::now();
    check_cuda(cuMemcpyHtoDAsync(
                   layer_batch_buffer_, host_layers.data(), bytes, active),
               "cuMemcpyHtoDAsync(layer batch)");
    CUdeviceptr layers = layer_batch_buffer_;
    int layer_count = static_cast<int>(host_layers.size());
    void* args[] = {&bg_y, &bg_yp, &bg_uv, &bg_uvp, &out_y, &out_yp,
                    &out_uv, &out_uvp, &layers, &layer_count, &width, &height};
    check_cuda(cuLaunchKernel(
                   direct_nv12_batch_kernel_, (width + 31) / 32,
                   (height + 31) / 32, 1, 16, 16, 1, 0, active, args, nullptr),
               "cuLaunchKernel(nv12_composite_layer_batch_2x2)");
    if (auto* counters = profiling::g_current_counters) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count();
        counters->cuda_composite_frames.fetch_add(1, std::memory_order_relaxed);
        counters->cuda_composite_wall_us.fetch_add(
            static_cast<std::uint64_t>(elapsed), std::memory_order_relaxed);
    }
    return true;
}

} // namespace chronon3d::backends::vulkan

#endif
