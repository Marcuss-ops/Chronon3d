// cuda_nv12_compositor_detail.hpp — internal declarations shared by the
// cuda_nv12_surface_compositor_*.cpp translation units. Not installed and
// not part of any public API.

#pragma once

#include <chronon3d/backends/vulkan/cuda_nv12_surface_compositor.hpp>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

#include "cuda_nv12_composite_kernels.hpp"

#include <nvrtc.h>

#include <spdlog/spdlog.h>

#include <stdexcept>
#include <string>

namespace chronon3d::backends::vulkan::cuda_nv12_detail {

struct alignas(8) DirectYuvLayerHost {
    CUdeviceptr rgba{0};
    int pitch{0};
    int source_width{0};
    int source_height{0};
    float dst_x0{0.0f}, dst_y0{0.0f}, dst_x1{0.0f}, dst_y1{0.0f};
    float src_x0{0.0f}, src_y0{0.0f}, src_x1{1.0f}, src_y1{1.0f};
    float opacity{1.0f};
    int blend_mode{0};
};
static_assert(sizeof(DirectYuvLayerHost) == 64);

// Defined in cuda_nv12_surface_compositor.cpp.
[[noreturn]] void fail(const char* operation, const std::string& detail);
void check_cuda(CUresult result, const char* operation);
void check_nvrtc(nvrtcResult result, const char* operation);
std::string get_compiled_nv12_ptx();

} // namespace chronon3d::backends::vulkan::cuda_nv12_detail

#endif // CHRONON3D_ENABLE_CUDA_INTEROP
