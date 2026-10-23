// cuda_nv12_kernels.hpp — assemble embedded NVRTC kernels by operation.
#pragma once

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include "cuda_nv12_decode_kernels.hpp"
#include "cuda_nv12_composite_kernels.hpp"
#include "cuda_nv12_layer_kernels.hpp"
#include "cuda_nv12_encode_kernels.hpp"

#include <string>

namespace chronon3d::media::video::cuda_nv12_detail {
inline const std::string kNv12Kernel = [] {
    std::string source;
    source.reserve(sizeof(kNv12DecodeKernel) + sizeof(kNv12CompositeKernel) +
                   sizeof(kNv12LayerKernel) + sizeof(kNv12EncodeKernel) - 4);
    source += kNv12DecodeKernel;
    source += kNv12CompositeKernel;
    source += kNv12LayerKernel;
    source += kNv12EncodeKernel;
    return source;
}();
} // namespace chronon3d::media::video::cuda_nv12_detail
#endif
