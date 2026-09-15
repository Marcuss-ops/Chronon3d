// cuda_nv12_compositor_detail.hpp — internal declarations shared by the
// cuda_nv12_compositor_*.cpp translation units. Not installed and
// not part of any public API.

#pragma once

#include <chronon3d/media/video/compositor/cuda/cuda_nv12_compositor.hpp>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

#include "cuda_nv12_kernels.hpp"
#include <chronon3d/text/mtsdf_profile.hpp>

#include <nvrtc.h>

#include <spdlog/spdlog.h>

#include <stdexcept>
#include <string>

namespace chronon3d::media::video::cuda_nv12_detail {

struct alignas(8) DirectYuvLayerHost {
    CUdeviceptr rgba{0};
    int pitch{0};
    int source_width{0};
    int source_height{0};
    float dst_x0{0.0f}, dst_y0{0.0f}, dst_x1{0.0f}, dst_y1{0.0f};
    float src_x0{0.0f}, src_y0{0.0f}, src_x1{1.0f}, src_y1{1.0f};
    float opacity{1.0f};
    int blend_mode{0};
    int resource_kind{0};
    float distance_range_px{
        static_cast<float>(::chronon3d::text::kCanonicalMtsdfProfile.distance_range_px)};
    unsigned int color{0xFFFFFFFFu};
    unsigned int stroke_color{0};
    float stroke_width{0.0f};
    unsigned int shadow_color{0};
    float shadow_opacity{0.0f};
    float shadow_dx{0.0f};
    float shadow_dy{0.0f};
    float shadow_blur{0.0f};
};
// Keep the host staging ABI byte-identical to the embedded CUDA
// DirectYuvLayer. The text material fields plus the canonical single shadow
// bring the record to 104 bytes.
static_assert(sizeof(DirectYuvLayerHost) == 104);

// Defined in cuda_nv12_compositor.cpp.
[[noreturn]] void fail(const char* operation, const std::string& detail);
void check_cuda(CUresult result, const char* operation);
void check_nvrtc(nvrtcResult result, const char* operation);
/// Load the canonical NV12 PTX artifact from ProgramCache, compiling and
/// publishing it there on a miss. The returned string is request-local; no
/// process-global PTX owner exists.
std::string get_compiled_nv12_ptx();

} // namespace chronon3d::media::video::cuda_nv12_detail

#endif // CHRONON3D_ENABLE_CUDA_INTEROP
