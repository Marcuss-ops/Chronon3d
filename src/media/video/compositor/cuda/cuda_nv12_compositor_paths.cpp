// cuda_nv12_compositor_paths.cpp — CudaNv12Compositor
// composite entry points: decoded YUV import, direct RGBA overlay and the
// RGBA-surface staging conversion for the encoder.

#include "cuda_nv12_compositor_detail.hpp"

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/media/video/nv12_region_policy.hpp>

#include <atomic>
#include <chrono>
#include <algorithm>

namespace chronon3d::media::video {

bool CudaNv12Compositor::composite(
    CUdeviceptr y, int y_pitch, CUdeviceptr uv, int uv_pitch,
    std::uint32_t width, std::uint32_t height, CUstream stream,
    CudaYuvFormat format, YuvToRgbParams params) {
    using cuda_nv12_detail::check_cuda;

    const CUfunction kernel = format == CudaYuvFormat::P010 ? p010_kernel_ : kernel_;
    if (!bridge_ || !kernel || !y || !uv || width == 0 || height == 0) return false;
    const auto started = std::chrono::steady_clock::now();
    check_cuda(cuCtxSetCurrent(context_), "cuCtxSetCurrent");
    CUstream active = stream ? stream : stream_;
    if (!first_write_) bridge_->wait_for_vulkan(active);
    float y_offset = params.y_offset;
    float y_scale = params.y_scale;
    float uv_offset = params.uv_offset;
    float uv_scale = params.uv_scale;
    float r_v = params.r_v;
    float g_u = params.g_u;
    float g_v = params.g_v;
    float b_u = params.b_u;
    std::uint32_t transfer_decode = params.transfer_decode;
    void* args[] = {&y, &y_pitch, &uv, &uv_pitch, nullptr, &width, &height,
                        &y_offset, &y_scale, &uv_offset, &uv_scale,
                        &r_v, &g_u, &g_v, &b_u, &transfer_decode};
    CUsurfObject surface = bridge_->surface_object();
    args[4] = &surface;
    check_cuda(cuLaunchKernel(kernel, (width + 15) / 16, (height + 15) / 16, 1,
                              16, 16, 1, 0, active, args, nullptr),
               format == CudaYuvFormat::P010
                   ? "cuLaunchKernel(p010_to_rgba)"
                   : "cuLaunchKernel(nv12_to_rgba)");
    bridge_->signal_for_vulkan(active);
    first_write_ = false;
    if (auto* counters = profiling::g_current_counters) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count();
        counters->cuda_composite_frames.fetch_add(1, std::memory_order_relaxed);
        counters->cuda_composite_wall_us.fetch_add(
            static_cast<std::uint64_t>(elapsed), std::memory_order_relaxed);
        counters->compositor_wall_ms_total.fetch_add(
            static_cast<std::uint64_t>(elapsed / 1000), std::memory_order_relaxed);
        // Zero-copy gate 3: every NV12/P010→RGBA kernel launch is a decoded
        // frame that left the native YUV domain.  The direct-YUV path
        // (composite_direct_nv12*) keeps this counter at zero.
        counters->nv12_to_rgba_frames.fetch_add(1, std::memory_order_relaxed);
    }
    return true;
}

bool CudaNv12Compositor::composite_direct_nv12(
    CUdeviceptr bg_y, int bg_yp, CUdeviceptr bg_uv, int bg_uvp,
    CUdeviceptr out_y, int out_yp, CUdeviceptr out_uv, int out_uvp,
    std::uint32_t width, std::uint32_t height, CUstream stream) {
    using cuda_nv12_detail::check_cuda;

    if (!bridge_ || !direct_nv12_kernel_ || !bg_y || !bg_uv || !out_y || !out_uv || width == 0 || height == 0) {
        return false;
    }
    const auto started = std::chrono::steady_clock::now();
    check_cuda(cuCtxSetCurrent(context_), "cuCtxSetCurrent");
    CUstream active = stream ? stream : stream_;
    if (!first_write_) bridge_->wait_for_vulkan(active);

    CUsurfObject surface = bridge_->surface_object();
    void* args[] = {
        &bg_y, &bg_yp, &bg_uv, &bg_uvp,
        &surface,
        &out_y, &out_yp, &out_uv, &out_uvp,
        &width, &height
    };
    check_cuda(cuLaunchKernel(direct_nv12_kernel_, (width + 31) / 32, (height + 31) / 32, 1,
                              16, 16, 1, 0, active, args, nullptr),
               "cuLaunchKernel(nv12_composite_overlay_2x2)");
    bridge_->signal_for_vulkan(active);
    first_write_ = false;

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

bool CudaNv12Compositor::composite_surface_to_nv12(
    CUdeviceptr out_y, int out_yp, CUdeviceptr out_uv, int out_uvp,
    std::uint32_t width, std::uint32_t height, CUstream stream,
    std::span<const raster::BBox> dirty_regions,
    std::uint32_t transfer_encode,
    bool destination_retains_previous_frame) {
    using cuda_nv12_detail::check_cuda;

    const CUfunction kernel = surface_u8_ ? rgba_u8_to_nv12_kernel_ : rgba_to_nv12_kernel_;
    if (!bridge_ || !kernel || !out_y || !out_uv ||
        width == 0 || height == 0) {
        spdlog::error("[nv12_diag] invalid compositor args: bridge={} kernel={} out_y={} out_uv={} width={} height={}",
                      static_cast<void*>(bridge_.get()), static_cast<void*>(kernel),
                      out_y, out_uv, width, height);
        return false;
    }
    const auto started = std::chrono::steady_clock::now();
    check_cuda(cuCtxSetCurrent(context_), "cuCtxSetCurrent");
    const CUstream active = stream ? stream : stream_;
    // The imported surface was populated by Vulkan immediately before the
    // encoder handoff, including on the first frame. Always consume the
    // Vulkan→CUDA semaphore for this read path.
    bridge_->wait_for_vulkan(active);
    CUsurfObject surface = bridge_->surface_object();
    // Region authority (nv12_region_policy.hpp): a sub-region is converted only
    // when the caller proved the destination already holds the previous frame
    // outside it. The predicted dirty rect of the CPU-side renderer is not that
    // proof, and neither is a pooled encoder frame — so the frame is widened to
    // full instead of publishing a torn frame.
    const auto conversion = plan_nv12_conversion(
        dirty_regions, width, height, destination_retains_previous_frame);
    if (conversion.partial_rejected) {
        static std::atomic<bool> rejection_warned{false};
        bool expected = false;
        if (rejection_warned.compare_exchange_strong(expected, true)) {
            spdlog::error(
                "[nv12_region] partial handoff rejected ({} requested regions, "
                "destination continuity unproven): converting the full frame",
                dirty_regions.size());
        }
    }
    const bool partial_handoff = !conversion.full_frame;
    const auto launch_region = [&](raster::BBox region) {
        int x0 = region.x0;
        int y0 = region.y0;
        int x1 = region.x1;
        int y1 = region.y1;
        // The u8 kernel reads an already-encoded 8-bit surface; only the
        // RGBA32F (canonical linear) kernel takes the transfer encode.
        std::uint32_t transfer = surface_u8_ ? 0u : transfer_encode;
        void* args[] = {&surface, &out_y, &out_yp, &out_uv, &out_uvp,
                        &width, &height, &x0, &y0, &x1, &y1,
                        &transfer};
        check_cuda(cuLaunchKernel(kernel, (x1 - x0 + 31) / 32,
                                  (y1 - y0 + 31) / 32, 1, 16, 16, 1, 0,
                                  active, args, nullptr),
                   "cuLaunchKernel(rgba_surface_to_nv12_2x2)");
    };
    for (const auto& region : conversion.regions) launch_region(region);

    // The conversion only reads the imported Vulkan image. Return the
    // ownership token so Vulkan cannot rewrite this ring surface while the
    // asynchronous CUDA conversion is still using it.
    bridge_->signal_for_vulkan(active);

    first_write_ = false;
    if (auto* counters = profiling::g_current_counters) {
        // This is the FullGraph encoder's explicit GPU RGBA→NV12 conversion.
        // Count its host launch interval alongside the direct-YUV compositor
        // timings, while the per-frame Vulkan timestamp remains the authority
        // for device execution time.
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count();
        counters->cuda_composite_frames.fetch_add(1, std::memory_order_relaxed);
        counters->cuda_composite_wall_us.fetch_add(
            static_cast<std::uint64_t>(elapsed), std::memory_order_relaxed);
        counters->compositor_wall_ms_total.fetch_add(
            static_cast<std::uint64_t>(elapsed / 1000), std::memory_order_relaxed);
        // Zero-copy gate 4: every RGBA→NV12 kernel launch is an encoder
        // staging conversion.  composite_direct_nv12* keeps this at zero.
        counters->rgba_to_nv12_frames.fetch_add(1, std::memory_order_relaxed);
        if (partial_handoff) {
            // Non-zero means a producer claimed destination continuity. On the
            // pooled encoder-frame path that claim is not established by the
            // pipeline, so this counter is a defect signal, not a usage stat.
            counters->nv12_partial_handoff_frames.fetch_add(
                1, std::memory_order_relaxed);
        }
    }
    return true;
}

} // namespace chronon3d::media::video

#endif
